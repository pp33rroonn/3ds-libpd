/* pd_bridge.c - libpd Initialisierung + Callbacks + soundfiler-Shim */

#include "pd_bridge.h"
#include "../fs/soundfiler_shim.h"
#include "../ui/pd_log.h"
#include "../fs/pd_parser.h"
#include "pd_rewrite.h"

#include <z_libpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>   /* rmdir */

#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

static SyncClock *s_clk   = NULL;
static bool       s_pd_initialised = false;

/* ---- patch slots ---------------------------------------------------------
 * All open patches run in the single libpd instance; libpd_process_short()
 * sums their outputs. To get a real per-slot volume (and level meter) for
 * ANY patch, the patch is copied to SLOT_TMP_DIR/s<N+1>/ on load with every
 * plain [dac~] replaced by an inline subpatch [pd slotdac] (gain + level
 * meter + dac~); see make_slot_copy() below. */
static void *s_slots[PD_SLOT_COUNT]          = {NULL};
static float s_slot_vol[PD_SLOT_COUNT]       = {1, 1, 1, 1, 1};
static float s_slot_lvl[PD_SLOT_COUNT]       = {0};
static char  s_slot_name[PD_SLOT_COUNT][24]  = {{0}};
/* slot that receives the input (buttons, touch, motion, CTRL), -1 = none */
static int   s_input_slot = -1;
/* CTRL receivers of the patch being copied (renamed per slot, see pd_rewrite.h) */
static PdCtrlList s_copy_ctrls;

#define SLOT_TMP_DIR "sdmc:/3ds/libpd_3ds/.slots"

/* CTRL list per slot (read on load, shown by the panel). Every entry's
 * routed name "s<N>-<name>" is bound BEFORE the patch is opened, so values
 * the patch sends itself (loadbang, [s cutoff], sequencer) reach the list. */
static PdCtrlList  s_slot_ctrls[PD_SLOT_COUNT];
static void       *s_slot_bind[PD_SLOT_COUNT][PD_CTRL_MAX];
static bool        s_ctrl_dirty[PD_SLOT_COUNT];

/* ---- Log-Ringpuffer (Anzeige auf dem unteren Bildschirm) ---------------
 * PD-Meldungen gehen NICHT mehr direkt auf stderr (das zerschiesst das
 * Layout), sondern hierher. main.c zeichnet die Zeilen im Patch-Modus. */
static char     s_log[PD_LOG_LINES][PD_LOG_COLS + 1];
static int      s_log_head  = 0;     /* naechste zu schreibende Zeile */
static int      s_log_count = 0;
static unsigned s_log_serial = 0;
static char     s_partial[PD_LOG_COLS + 1];
static int      s_partial_len = 0;

static void log_flush_partial(void)
{
    s_partial[s_partial_len] = '\0';
    memcpy(s_log[s_log_head], s_partial, (size_t)s_partial_len + 1);
    s_log_head = (s_log_head + 1) % PD_LOG_LINES;
    if (s_log_count < PD_LOG_LINES) s_log_count++;
    s_partial_len = 0;
    s_log_serial++;
    pd_log_push(s_log[(s_log_head + PD_LOG_LINES - 1) % PD_LOG_LINES]);  /* top screen PD Log page */
}

/* Text anhaengen; '\n' beendet eine Zeile, zu lange Zeilen werden umgebrochen */
static void log_append(const char *msg)
{
    for (; *msg; msg++) {
        char c = *msg;
        if (c == '\n') { log_flush_partial(); continue; }
        if (c == '\r') continue;
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
        s_partial[s_partial_len++] = c;
        if (s_partial_len >= PD_LOG_COLS) log_flush_partial();
    }
}

void pd_bridge_log(const char *msg)
{
    log_append(msg);
    if (s_partial_len > 0) log_flush_partial();
}

unsigned pd_bridge_log_serial(void) { return s_log_serial; }

const char *pd_bridge_log_line(int i)
{
    if (i < 0 || i >= s_log_count) return "";
    int idx = (s_log_head - s_log_count + i + PD_LOG_LINES) % PD_LOG_LINES;
    return s_log[idx];
}

/* ---- libpd print callback ---------------------------------------------- */
static void pd_print(const char *msg)
{
    log_append(msg);   /* libpd liefert evtl. Teilstuecke -> bis '\n' sammeln */
}

static void log_fmt(const char *fmt, const char *a, const char *b)
{
    char buf[160];
    snprintf(buf, sizeof(buf), fmt, a, b);
    pd_bridge_log(buf);
}

/* CTRL feedback: "s<N>-<name>" -> entry of slot N-1 */
static bool ctrl_feedback(const char *recv, float val)
{
    if (recv[0] != 's' || recv[1] < '1' || recv[1] > '9' || recv[2] != '-') return false;
    int slot = recv[1] - '1';
    if (slot >= PD_SLOT_COUNT) return false;
    PdCtrlList *l = &s_slot_ctrls[slot];
    const char *name = recv + 3;
    for (int i = 0; i < l->count; i++) {
        if (strcmp(name, l->ctrls[i].name) != 0) continue;
        PdCtrl *c = &l->ctrls[i];
        float lo = c->lo < c->hi ? c->lo : c->hi;
        float hi = c->lo < c->hi ? c->hi : c->lo;
        if (c->type == CTRL_TOGGLE) val = (val != 0.0f) ? 1.0f : 0.0f;
        else if (c->type == CTRL_RADIO) val = (float)(int)(val + 0.5f);
        if (c->type != CTRL_BANG) {   /* a bang has no value to show */
            if (val < lo) val = lo;
            if (val > hi) val = hi;
            if (val != c->val) {      /* echo of our own send: no redraw */
                c->val = val;
                s_ctrl_dirty[slot] = true;
            }
        }
        return true;
    }
    return false;
}

/* ---- libpd float message callback -------------------------------------- */
static void pd_float_msg(const char *recv, float val)
{
    if (ctrl_feedback(recv, val)) return;
    /* PD-Patch kann Tempo steuern: [send pd-bpm 130] */
    if (strcmp(recv, "pd-bpm") == 0 && s_clk) {
        /* gleicher Bereich wie bei den Tasten (BPM_MIN/BPM_MAX = 20..300) */
        if (val < 20.0f)  val = 20.0f;
        if (val > 300.0f) val = 300.0f;
        sync_clock_set_bpm(s_clk, val);
        return;
    }
    /* per-slot level from [pd slotdac]: env~ dB (100 = full scale) */
    int n;
    if (strstr(recv, "-lvl") && sscanf(recv, "slot-%d-lvl", &n) == 1 &&
        n >= 0 && n < PD_SLOT_COUNT) {
        float l = (val - 40.0f) / 60.0f;   /* 40..100 dB -> 0..1 */
        if (l < 0.0f) l = 0.0f;
        if (l > 1.0f) l = 1.0f;
        s_slot_lvl[n] = l;
    }
}

/* ---- Suchpfade --------------------------------------------------------- */
/* libpd hat KEINEN open-file-Hook (libpd_set_openfilehook existiert nicht).
 * Stattdessen: Suchpfade per libpd_add_to_search_path() registrieren.
 * devkitARM-newlib oeffnet "sdmc:/" und "romfs:/" direkt via fopen/open,
 * daher finden [soundfiler], [readsf~] und Abstractions die Dateien so.
 * Reihenfolge = Prioritaet (zuerst eingetragen = zuerst durchsucht). */
static const char *const PD_SEARCH_PATHS[] = {
    "sdmc:/3ds/libpd_3ds",
    "sdmc:/3ds/libpd_3ds/samples",
    "sdmc:/3ds/libpd_3ds/patches",
    "romfs:/samples",
    "romfs:/patches",
};

/* ---- oeffentliche API -------------------------------------------------- */

bool pd_bridge_init(SyncClock *clk)
{
    s_clk = clk;

    /* soundfiler-Shim initialisieren (Verzeichnisse anlegen, Pfade setzen)
     * MUSS vor libpd_init() geschehen damit der Hook gleich aktiv ist */
    soundfiler_shim_init();

    /* libpd Callbacks registrieren */
    libpd_set_printhook(pd_print);
    libpd_set_floathook(pd_float_msg);

    /* libpd starten (nur einmal) */
    if (!s_pd_initialised) {
        libpd_init();
        libpd_init_audio(0, 2, SAMPLERATE); /* 0 In, 2 Out (Stereo, interleaved L/R) */

        /* Suchpfade fuer Samples/Abstractions (nach libpd_init) */
        for (size_t i = 0; i < sizeof(PD_SEARCH_PATHS) / sizeof(PD_SEARCH_PATHS[0]); i++) {
            libpd_add_to_search_path(PD_SEARCH_PATHS[i]);
        }

        /* BPM-Receiver abonnieren */
        libpd_bind("pd-bpm");
        for (int i = 0; i < PD_SLOT_COUNT; i++) {
            char r[16];
            snprintf(r, sizeof(r), "slot-%d-lvl", i);
            libpd_bind(r);
        }
        s_pd_initialised = true;
    }

    /* no patch at start: every slot is empty until chosen in OPTIONS.
     * DSP on, so a loaded slot plays at once. */
    pd_bridge_set_dsp(true);
    return true;
}

void pd_bridge_deinit(void)
{
    for (int i = 0; i < PD_SLOT_COUNT; i++) pd_slot_unload(i);
}

/* ---- input routing ------------------------------------------------------- */

void pd_bridge_set_input_slot(int slot)
{
    s_input_slot = (slot >= 0 && slot < PD_SLOT_COUNT) ? slot : -1;
}

int pd_bridge_get_input_slot(void) { return s_input_slot; }

/* receiver name for the input slot; false if no loaded slot gets input */
static bool input_target(int slot, const char *name, char *out, size_t len)
{
    if (!name || slot < 0 || slot >= PD_SLOT_COUNT || !s_slots[slot]) return false;
    pd_rewrite_slot_name(out, len, slot, name);
    return true;
}

void pd_input_float(const char *name, float v)
{
    char r[PD_REWRITE_NAME_MAX + 8];
    if (input_target(s_input_slot, name, r, sizeof(r))) libpd_float(r, v);
}

void pd_input_bang(const char *name)
{
    char r[PD_REWRITE_NAME_MAX + 8];
    if (input_target(s_input_slot, name, r, sizeof(r))) libpd_bang(r);
}

void pd_slot_input_float(int slot, const char *name, float v)
{
    char r[PD_REWRITE_NAME_MAX + 8];
    if (input_target(slot, name, r, sizeof(r))) libpd_float(r, v);
}

void pd_slot_input_bang(int slot, const char *name)
{
    char r[PD_REWRITE_NAME_MAX + 8];
    if (input_target(slot, name, r, sizeof(r))) libpd_bang(r);
}

/* ---- CTRL list per slot ----------------------------------------------------- */

static void ctrl_unbind(int slot)
{
    for (int i = 0; i < PD_CTRL_MAX; i++) {
        if (s_slot_bind[slot][i]) libpd_unbind(s_slot_bind[slot][i]);
        s_slot_bind[slot][i] = NULL;
    }
    s_slot_ctrls[slot].count = 0;
    s_ctrl_dirty[slot] = false;
}

/* read the CTRL entries of dir/file into the slot list and bind them */
static void ctrl_load(int slot, const char *dir, const char *file)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    PdCtrlList *l = &s_slot_ctrls[slot];
    int n = pd_parse(path, l);
    for (int i = 0; i < l->count; i++) {
        char r[PD_REWRITE_NAME_MAX + 8];
        pd_rewrite_slot_name(r, sizeof(r), slot, l->ctrls[i].name);
        s_slot_bind[slot][i] = libpd_bind(r);
    }
    char msg[64];
    if (n < 0) snprintf(msg, sizeof(msg), "CTRL: patch not readable");
    else       snprintf(msg, sizeof(msg), "CTRL: %d controls found", n);
    pd_bridge_log(msg);
}

PdCtrlList *pd_slot_ctrls(int slot)
{
    if (slot < 0 || slot >= PD_SLOT_COUNT || !s_slots[slot]) return NULL;
    return &s_slot_ctrls[slot];
}

bool pd_slot_ctrls_take_dirty(int slot)
{
    if (slot < 0 || slot >= PD_SLOT_COUNT) return false;
    bool d = s_ctrl_dirty[slot];
    s_ctrl_dirty[slot] = false;
    return d;
}

/* ---- slot API ------------------------------------------------------------ */

#define MAX_EXTRA_PATHS 16
static char s_extra_paths[MAX_EXTRA_PATHS][128];
static int  s_extra_count = 0;

static void add_patch_search_path(const char *dir)
{
    for (int i = 0; i < s_extra_count; i++)
        if (strcmp(s_extra_paths[i], dir) == 0) return;
    if (s_extra_count >= MAX_EXTRA_PATHS || strlen(dir) >= sizeof(s_extra_paths[0])) return;
    snprintf(s_extra_paths[s_extra_count++], sizeof(s_extra_paths[0]), "%s", dir);
    libpd_add_to_search_path(dir);
}

static bool slot_ok(int slot) { return slot >= 0 && slot < PD_SLOT_COUNT; }

static void slot_send_vol(int slot)
{
    char recv[16];
    snprintf(recv, sizeof(recv), "slot-%d-vol", slot);
    libpd_float(recv, s_slot_vol[slot]);
}

/* Inline replacement for [dac~]: a subpatch "pd slotdac" with 2 signal
 * inlets -> *~ gain -> dac~. Gain from [r slot-N-vol] smoothed by line~
 * (30 ms), level -> slot-N-lvl. A subpatch occupies exactly one object index
 * in its parent (like the dac~ it replaces), so all connections stay valid,
 * and no abstraction file has to be found on the SD card. */
static void write_slot_subpatch(FILE *out, int slot)
{
    fprintf(out,
        "#N canvas 0 0 400 300 slotdac 0;\n"
        "#X obj 10 10 inlet~;\n"            /* 0 */
        "#X obj 120 10 inlet~;\n"           /* 1 */
        "#X obj 10 120 *~;\n"               /* 2 */
        "#X obj 120 120 *~;\n"              /* 3 */
        "#X obj 10 200 dac~;\n"             /* 4 */
        "#X obj 230 10 r slot-%d-vol;\n"    /* 5 */
        "#X obj 230 40 pack f 30;\n"        /* 6 */
        "#X obj 230 70 line~;\n"            /* 7 */
        "#X obj 230 150 +~;\n"              /* 8 */
        "#X obj 230 180 env~ 2048 1024;\n"  /* 9 */
        "#X obj 230 210 s slot-%d-lvl;\n"   /* 10 */
        "#X connect 0 0 2 0;\n#X connect 1 0 3 0;\n"
        "#X connect 7 0 2 1;\n#X connect 7 0 3 1;\n"
        "#X connect 2 0 4 0;\n#X connect 3 0 4 1;\n"
        "#X connect 5 0 6 0;\n#X connect 6 0 7 0;\n"
        "#X connect 2 0 8 0;\n#X connect 3 0 8 1;\n"
        "#X connect 8 0 9 0;\n#X connect 9 0 10 0;\n",
        slot, slot);
}

static void skip_ws(const char **p, const char *end)
{
    while (*p < end && (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t')) (*p)++;
}

/* Copy one "#X obj x y dac~ ...;" record with dac~ -> [pd slotdac] subpatch.
 * Only plain [dac~] / [dac~ 1 2] (optionally ", f N") are replaced; other
 * channel layouts are copied unchanged. Returns true if replaced. */
static bool rewrite_record(FILE *out, const char *rec, const char *end, int slot)
{
    const char *p = rec;
    skip_ws(&p, end);
    if (end - p < 7 || strncmp(p, "#X obj ", 7) != 0) return false;
    const char *q = p + 7;
    const char *xy0 = NULL;
    for (int k = 0; k < 2; k++) {            /* x y */
        skip_ws(&q, end);
        if (k == 0) xy0 = q;
        while (q < end && *q != ' ' && *q != '\n') q++;
    }
    const char *xy1 = q;
    skip_ws(&q, end);
    const char *name = q;
    if (end - name < 4 || strncmp(name, "dac~", 4) != 0) return false;
    const char *a = name + 4;
    if (a < end && *a != ' ' && *a != '\n' && *a != ',') return false;  /* e.g. dac~x */

    /* args up to ',' (width suffix) or end */
    const char *comma = a;
    while (comma < end && *comma != ',') comma++;
    char args[32] = {0};
    size_t al = 0;
    for (const char *s = a; s < comma && al < sizeof(args) - 1; s++)
        if (*s != '\n' && *s != '\r') args[al++] = *s;
    char *t = args;
    while (*t == ' ') t++;
    size_t tl = strlen(t);
    while (tl > 0 && t[tl - 1] == ' ') t[--tl] = '\0';
    if (tl != 0 && strcmp(t, "1 2") != 0) return false;

    char xy[32];
    size_t xl = (size_t)(xy1 - xy0);
    if (xl >= sizeof(xy)) return false;
    memcpy(xy, xy0, xl);
    xy[xl] = '\0';
    for (char *c = xy; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';

    fwrite(rec, 1, (size_t)(p - rec), out);  /* leading whitespace */
    write_slot_subpatch(out, slot);
    fprintf(out, "#X restore %s pd slotdac", xy);  /* caller appends ';' */
    return true;
}

/* ---- per-slot copy with inlined abstractions ---------------------------
 * Slot N (0-based) is opened from SLOT_TMP_DIR/s<N+1>/_slot_main.pd:
 *   - plain [dac~] -> [pd slotdac] (slot volume + meter)
 *   - hardware / CTRL receivers renamed s<N+1>-... (pd_rewrite.h)
 *   - every abstraction ([voice 220], [lib/voice]) is pasted in as a
 *     subpatch [pd voice] with $1..$n replaced by its creation arguments and
 *     $0 by a number unique to that instance; nested ones the same way.
 * libpd on the 3DS does not create abstractions from files (Pd prints
 * "couldn't create" without even searching, although the files are readable),
 * so the opened patch must not depend on the abstraction loader at all.
 * Lookup order mirrors Pd: folder of the calling file, patch folder, search
 * paths. The slot folder is emptied on every load. */

#define SLOT_MAIN_NAME   "_slot_main.pd"
#define ABS_MAX_DEPTH    8
#define ABS_MAX_INST     64
#define ABS_MAX_TRIED    128
#define ABS_PATH_MAX     192
#define ABS_MAX_ARGS     16
#define ABS_DOLLAR0_BASE 700000L   /* far above Pd's own $0 counter */

typedef struct {
    const char *arg[ABS_MAX_ARGS];   /* raw tokens of the calling [obj] */
    size_t      len[ABS_MAX_ARGS];
    int         n;
    long        dollar0;
} AbsArgs;

typedef struct {
    int  slot;
    char slot_dir[64];
    const char *main_dir;                       /* folder of the loaded patch */
    char tried[ABS_MAX_TRIED][ABS_PATH_MAX];    /* lookups without a file */
    int  n_tried;
    int  inlined, replaced, routed, skipped;
    bool failed;                                /* out of memory / write error */
} SlotCopy;

typedef struct { char *p; size_t n, cap; bool err; } Buf;

static void buf_put(Buf *b, const char *s, size_t n)
{
    if (b->err) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *q = realloc(b->p, cap);
        if (!q) { b->err = true; return; }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

static bool file_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return S_ISREG(st.st_mode);
    FILE *f = fopen(path, "rb");   /* stat on sdmc:/ is less tested than fopen */
    if (!f) return false;
    fclose(f);
    return true;
}

/* whole file, NUL-terminated; NULL if missing / empty / > 512 KB */
static char *read_file(const char *path, size_t *len)
{
    FILE *in = fopen(path, "rb");
    if (!in) return NULL;
    fseek(in, 0, SEEK_END);
    long n = ftell(in);
    fseek(in, 0, SEEK_SET);
    char *buf = (n > 0 && n <= 512 * 1024) ? malloc((size_t)n + 1) : NULL;
    if (buf) {
        *len = fread(buf, 1, (size_t)n, in);
        buf[*len] = '\0';
    }
    fclose(in);
    return buf;
}

/* delete the contents of dir (depth levels of sub folders) */
static void clear_dir(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    char path[ABS_PATH_MAX + 320];
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        int n = snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth > 0) { clear_dir(path, depth - 1); rmdir(path); }
        } else {
            remove(path);
        }
    }
    closedir(d);
}

static bool list_has(char (*list)[ABS_PATH_MAX], int n, const char *s)
{
    for (int i = 0; i < n; i++) if (strcmp(list[i], s) == 0) return true;
    return false;
}

/* Replace $N / \$N (creation arguments, missing -> 0) and $0 in one record.
 * In message boxes $1.. are message arguments: only $0 is replaced there.
 * Single pass, so a "\$0" passed in as an argument stays the caller's $0
 * (= the root canvas, which subpatches share). */
static void subst_record(Buf *b, const char *rec, const char *end, const AbsArgs *a)
{
    b->n = 0;
    b->err = false;
    const char *t = rec;
    skip_ws(&t, end);
    bool is_msg = end - t >= 7 && strncmp(t, "#X msg ", 7) == 0;

    const char *p = rec, *copied = rec;
    while (p < end) {
        const char *d = p;
        if (*p == '\\' && p + 1 < end && p[1] == '$') p++;
        if (*p != '$') { p++; continue; }
        const char *q = p + 1;
        if (q >= end || *q < '0' || *q > '9') { p = q; continue; }
        int idx = 0;
        while (q < end && *q >= '0' && *q <= '9') { if (idx < 10000) idx = idx * 10 + (*q - '0'); q++; }
        if (is_msg && idx != 0) { p = q; continue; }

        buf_put(b, copied, (size_t)(d - copied));
        if (idx == 0) {
            char num[24];
            int k = snprintf(num, sizeof(num), "%ld", a->dollar0);
            buf_put(b, num, (size_t)k);
        } else if (idx <= a->n) {
            buf_put(b, a->arg[idx - 1], a->len[idx - 1]);
        } else {
            buf_put(b, "0", 1);
        }
        copied = p = q;
    }
    buf_put(b, copied, (size_t)(end - copied));
}

/* "#X obj x y cls a1 a2 ..., f 12": x y span and the argument tokens */
static bool parse_obj(const char *rec, const char *end,
                      const char **xy, size_t *xy_len, AbsArgs *a)
{
    const char *p = rec, *xy0 = NULL;
    a->n = 0;
    for (int idx = 0; p < end; idx++) {
        skip_ws(&p, end);
        if (p >= end) break;
        const char *t = p;
        while (p < end && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
        size_t len = (size_t)(p - t);
        if (idx == 2) xy0 = t;
        if (idx == 3) { *xy = xy0; *xy_len = (size_t)(p - xy0); }
        if (idx < 4) continue;
        /* unescaped ',' = start of the ", f 12" width suffix */
        bool stop = t[len - 1] == ',' && (len < 2 || t[len - 2] != '\\');
        if (stop) len--;
        if (idx > 4 && len > 0) {
            if (a->n >= ABS_MAX_ARGS) return false;
            a->arg[a->n] = t;
            a->len[a->n++] = len;
        }
        if (stop) break;
    }
    return xy0 != NULL;
}

/* Abstraction file for [cls] used by a file in src_root/rel_dir. On success
 * path is the file, *root/child_dir where its own children are looked up. */
static bool find_abs(SlotCopy *sc, const char *src_root, const char *rel_dir,
                     const char *cls, char *path, size_t path_len,
                     const char **root, char *child_dir, size_t child_len)
{
    char key[ABS_PATH_MAX];
    snprintf(key, sizeof(key), "%.80s|%.40s|%.60s", src_root, rel_dir, cls);
    if (list_has(sc->tried, sc->n_tried, key)) return false;

    enum { NSP = sizeof(PD_SEARCH_PATHS) / sizeof(PD_SEARCH_PATHS[0]) };
    const char *roots[2 + NSP], *dirs[2 + NSP];
    int nr = 0;
    roots[nr] = src_root;     dirs[nr++] = rel_dir;
    roots[nr] = sc->main_dir; dirs[nr++] = "";
    for (int i = 0; i < NSP; i++) { roots[nr] = PD_SEARCH_PATHS[i]; dirs[nr++] = ""; }

    char rel[ABS_PATH_MAX];
    for (int i = 0; i < nr; i++) {
        int n = dirs[i][0] ? snprintf(rel, sizeof(rel), "%s/%s.pd", dirs[i], cls)
                           : snprintf(rel, sizeof(rel), "%s.pd", cls);
        if (n < 0 || (size_t)n >= sizeof(rel)) continue;
        n = snprintf(path, path_len, "%s/%s", roots[i], rel);
        if (n < 0 || (size_t)n >= path_len || !file_exists(path)) continue;
        *root = roots[i];
        snprintf(child_dir, child_len, "%s", rel);
        char *slash = strrchr(child_dir, '/');
        if (slash) *slash = '\0'; else child_dir[0] = '\0';
        return true;
    }

    /* built-in object (osc~, ...) or external: remember, don't stat again */
    if (sc->n_tried < ABS_MAX_TRIED)
        snprintf(sc->tried[sc->n_tried++], ABS_PATH_MAX, "%s", key);
    if (strchr(cls, '/')) {      /* "lib/x" can only be an abstraction */
        char msg[128];
        snprintf(msg, sizeof(msg), "abs missing: %.60s.pd", cls);
        pd_bridge_log(msg);
    }
    return false;
}

static bool emit_patch(SlotCopy *sc, FILE *out, const char *buf, size_t got,
                       const char *src_root, const char *rel_dir, int depth,
                       const AbsArgs *args, const char *sub_name);

/* Write [cls ...] (record rec..end) as an inline subpatch [pd cls].
 * Returns false if cls is no abstraction (caller writes the record as is). */
static bool try_inline(SlotCopy *sc, FILE *out, const char *rec, const char *end,
                       const char *src_root, const char *rel_dir, const char *cls,
                       int depth)
{
    char path[ABS_PATH_MAX + 128], child_dir[ABS_PATH_MAX];
    const char *root = NULL;
    if (!find_abs(sc, src_root, rel_dir, cls, path, sizeof(path),
                  &root, child_dir, sizeof(child_dir)))
        return false;

    AbsArgs a;
    const char *xy = NULL;
    size_t xy_len = 0;
    if (depth >= ABS_MAX_DEPTH || sc->inlined >= ABS_MAX_INST ||
        !parse_obj(rec, end, &xy, &xy_len, &a)) {
        sc->skipped++;
        return false;
    }
    size_t len = 0;
    char *buf = read_file(path, &len);
    const char *h = buf;
    if (buf) skip_ws(&h, buf + len);
    if (!buf || buf + len - h < 10 || strncmp(h, "#N canvas ", 10) != 0) {
        free(buf);
        sc->skipped++;
        return false;
    }
    a.dollar0 = ABS_DOLLAR0_BASE + sc->slot * 1000L + (++sc->inlined);

    const char *ws = rec;
    skip_ws(&ws, end);
    fwrite(rec, 1, (size_t)(ws - rec), out);            /* leading whitespace */
    if (!emit_patch(sc, out, buf, len, root, child_dir, depth + 1, &a, cls))
        sc->failed = true;
    fprintf(out, "\n#X restore %.*s pd %s", (int)xy_len, xy, cls);  /* caller adds ';' */
    free(buf);
    return true;
}

/* "#N canvas x y w h font" of an abstraction -> "#N canvas x y w h name 0" */
static void write_sub_header(FILE *out, const char *rec, const char *end, const char *name)
{
    const char *p = rec;
    for (int idx = 0; idx < 6 && p < end; idx++) {
        skip_ws(&p, end);
        while (p < end && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    }
    fwrite(rec, 1, (size_t)(p - rec), out);
    fprintf(out, " %s 0", name);
}

/* Write the records of one patch file. args == NULL: the loaded patch itself
 * (its $ stay for Pd). Otherwise an abstraction being inlined as sub_name. */
static bool emit_patch(SlotCopy *sc, FILE *out, const char *buf, size_t got,
                       const char *src_root, const char *rel_dir, int depth,
                       const AbsArgs *args, const char *sub_name)
{
    Buf sb = {0};
    char cls[ABS_PATH_MAX];
    bool first = true;
    const char *p = buf, *end = buf + got;
    while (p < end && !sc->failed) {
        /* record ends at an unescaped ';' */
        const char *r = p;
        while (r < end && !(*r == ';' && (r == buf || r[-1] != '\\'))) r++;
        const char *rec = p, *rend = r;
        if (args && r < end) {
            subst_record(&sb, p, r, args);
            if (sb.err) { sc->failed = true; break; }
            rec = sb.p;
            rend = sb.p + sb.n;
        }

        if (args && first && r < end) {
            write_sub_header(out, rec, rend, sub_name);
        } else if (r < end && rewrite_record(out, rec, rend, sc->slot)) {
            sc->replaced++;
        } else if (!(r < end && pd_rewrite_obj_class(rec, rend, cls, sizeof(cls)) &&
                     try_inline(sc, out, rec, rend, src_root, rel_dir, cls, depth))) {
            sc->routed += pd_rewrite_record(out, rec, rend, sc->slot,
                                            s_copy_ctrls.ctrls[0].name,
                                            sizeof(s_copy_ctrls.ctrls[0]),
                                            s_copy_ctrls.count);
        }
        if (r < end) { fputc(';', out); first = false; }
        p = r + 1;
    }
    free(sb.p);
    return !sc->failed && !ferror(out);
}

/* Build SLOT_TMP_DIR/s<N+1>/_slot_main.pd from dir/file.
 * out_dir receives the folder to open the copy from. Returns true if usable. */
static bool make_slot_copy(int slot, const char *dir, const char *file,
                           char *out_dir, size_t out_len)
{
    static SlotCopy sc;          /* ~25 KB: keep it off the thread stack */
    memset(&sc, 0, sizeof(sc));
    sc.slot = slot;
    sc.main_dir = dir;
    snprintf(sc.slot_dir, sizeof(sc.slot_dir), SLOT_TMP_DIR "/s%d", slot + 1);

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/libpd_3ds", 0777);
    mkdir(SLOT_TMP_DIR, 0777);
    mkdir(sc.slot_dir, 0777);
    clear_dir(sc.slot_dir, 3);   /* also removes lib/ copies of older builds */

    char src[256], dst[128];
    snprintf(src, sizeof(src), "%s/%s", dir, file);
    snprintf(dst, sizeof(dst), "%s/" SLOT_MAIN_NAME, sc.slot_dir);

    /* CTRL receivers of this patch: renamed per slot like the hardware names
     * (also inside its abstractions, so [r cutoff] there follows the slider) */
    if (pd_parse(src, &s_copy_ctrls) < 0) s_copy_ctrls.count = 0;

    size_t len = 0;
    char *buf = read_file(src, &len);
    if (!buf) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { free(buf); return false; }
    bool ok = emit_patch(&sc, out, buf, len, dir, "", 0, NULL, NULL);
    free(buf);
    if (fclose(out) != 0 || !ok) return false;
    snprintf(out_dir, out_len, "%s", sc.slot_dir);

    char msg[96];
    snprintf(msg, sizeof(msg), "slot %d: %d dac~ -> volume, %d inputs -> s%d-, %d abs inline",
             slot, sc.replaced, sc.routed, slot + 1, sc.inlined);
    pd_bridge_log(msg);
    if (sc.skipped) {
        snprintf(msg, sizeof(msg), "slot %d: %d abs not inlined (too deep / unreadable)",
                 slot, sc.skipped);
        pd_bridge_log(msg);
    }
    return true;
}

bool pd_slot_load(int slot, const char *dir, const char *file)
{
    if (!slot_ok(slot) || !dir || !file || !s_pd_initialised) return false;

    pd_slot_unload(slot);

    /* the copy lives in SLOT_TMP_DIR: add the patch folder to the search
     * path so abstractions / samples next to the patch are still found
     * (each folder is added once) */
    add_patch_search_path(dir);

    /* CTRL list + feedback bindings first: loadbang values are sent while
     * the patch is opened */
    ctrl_load(slot, dir, file);

    /* volume-wrapped copy; fall back to the original if SD is not writable */
    char copy_dir[64];
    bool copied = make_slot_copy(slot, dir, file, copy_dir, sizeof(copy_dir));
    if (copied)
        s_slots[slot] = libpd_openfile(SLOT_MAIN_NAME, copy_dir);
    if (!s_slots[slot]) {
        pd_bridge_log("slot copy failed, volume fixed at 100%");
        s_slots[slot] = libpd_openfile(file, dir);
    }
    if (!s_slots[slot]) {
        log_fmt("ERROR opening: %s/%s", dir, file);
        ctrl_unbind(slot);
        return false;
    }

    /* display name = basename without ".pd", max 20 chars */
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;
    size_t n = strlen(base);
    if (n > 3 && strcmp(base + n - 3, ".pd") == 0) n -= 3;
    if (n > 20) n = 20;
    memcpy(s_slot_name[slot], base, n);
    s_slot_name[slot][n] = '\0';

    slot_send_vol(slot);

    char msg[64];
    snprintf(msg, sizeof(msg), "slot %d: %s", slot, file);
    pd_bridge_log(msg);
    return true;
}

void pd_slot_unload(int slot)
{
    if (!slot_ok(slot)) return;
    ctrl_unbind(slot);
    if (s_slots[slot]) {
        libpd_closefile(s_slots[slot]);
        s_slots[slot] = NULL;
    }
    s_slot_name[slot][0] = '\0';
    s_slot_lvl[slot] = 0.0f;
}

float pd_slot_get_level(int slot)
{
    return pd_slot_is_loaded(slot) ? s_slot_lvl[slot] : 0.0f;
}

bool pd_slot_is_loaded(int slot)
{
    return slot_ok(slot) && s_slots[slot] != NULL;
}

const char *pd_slot_display_name(int slot)
{
    if (!pd_slot_is_loaded(slot)) return "";
    return s_slot_name[slot];
}

void pd_slot_set_vol(int slot, float v)
{
    if (!slot_ok(slot)) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    s_slot_vol[slot] = v;
    if (s_pd_initialised) slot_send_vol(slot);
}

float pd_slot_get_vol(int slot)
{
    return slot_ok(slot) ? s_slot_vol[slot] : 0.0f;
}

void pd_bridge_set_dsp(bool on)
{
    libpd_start_message(1);
    libpd_add_float(on ? 1.0f : 0.0f);
    libpd_finish_message("pd", "dsp");
}

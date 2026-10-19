/* pd_bridge.c - libpd Initialisierung + Callbacks + soundfiler-Shim */

#include "pd_bridge.h"
#include "../fs/soundfiler_shim.h"
#include "../ui/pd_log.h"

#include <z_libpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

static SyncClock *s_clk   = NULL;
static bool       s_pd_initialised = false;

/* ---- patch slots ---------------------------------------------------------
 * All open patches run in the single libpd instance; libpd_process_short()
 * sums their outputs. To get a real per-slot volume (and level meter) for
 * ANY patch, the patch is copied to SLOT_TMP_DIR on load with every plain
 * [dac~] replaced by an inline subpatch [pd slotdac] (gain + level meter + dac~). */
static void *s_slots[PD_SLOT_COUNT]          = {NULL};
static float s_slot_vol[PD_SLOT_COUNT]       = {1, 1, 1, 1, 1};
static float s_slot_lvl[PD_SLOT_COUNT]       = {0};
static char  s_slot_name[PD_SLOT_COUNT][24]  = {{0}};

#define SLOT_TMP_DIR "sdmc:/3ds/libpd_3ds/.slots"

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

/* ---- libpd float message callback -------------------------------------- */
static void pd_float_msg(const char *recv, float val)
{
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

bool pd_bridge_init(const char *patch_dir,
                    const char *patch_file,
                    SyncClock  *clk)
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

    /* Patch in Slot 0 oeffnen */
    if (!pd_slot_load(0, patch_dir, patch_file)) return false;

    /* DSP einschalten: [pd dsp 1] */
    pd_bridge_set_dsp(true);
    return true;
}

void pd_bridge_deinit(void)
{
    for (int i = 0; i < PD_SLOT_COUNT; i++) pd_slot_unload(i);
}

bool pd_bridge_reload(const char *patch_dir, const char *patch_file)
{
    pd_slot_unload(0);
    pd_bridge_log("---");
    if (!pd_slot_load(0, patch_dir, patch_file)) return false;

    /* DSP wieder einschalten */
    pd_bridge_set_dsp(true);
    return true;
}

/* ---- slot API ------------------------------------------------------------ */

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

/* Build SLOT_TMP_DIR/slotN.pd from dir/file. Returns true if usable. */
static bool make_slot_copy(int slot, const char *dir, const char *file,
                           char *out_name, size_t out_len)
{
    char src[256];
    snprintf(src, sizeof(src), "%s/%s", dir, file);
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    fseek(in, 0, SEEK_END);
    long len = ftell(in);
    fseek(in, 0, SEEK_SET);
    if (len <= 0 || len > 512 * 1024) { fclose(in); return false; }
    char *buf = malloc((size_t)len);
    if (!buf) { fclose(in); return false; }
    size_t got = fread(buf, 1, (size_t)len, in);
    fclose(in);

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/libpd_3ds", 0777);
    mkdir(SLOT_TMP_DIR, 0777);

    snprintf(out_name, out_len, "slot%d.pd", slot);
    char dst[128];
    snprintf(dst, sizeof(dst), SLOT_TMP_DIR "/%s", out_name);
    FILE *out = fopen(dst, "wb");
    if (!out) { free(buf); return false; }

    int replaced = 0;
    const char *p = buf, *end = buf + got;
    while (p < end) {
        /* record ends at an unescaped ';' */
        const char *r = p;
        while (r < end && !(*r == ';' && (r == buf || r[-1] != '\\'))) r++;
        if (r < end && rewrite_record(out, p, r, slot)) replaced++;
        else fwrite(p, 1, (size_t)(r - p), out);
        if (r < end) fputc(';', out);
        p = r + 1;
    }
    free(buf);
    if (fclose(out) != 0) return false;

    char msg[64];
    snprintf(msg, sizeof(msg), "slot %d: %d dac~ -> volume", slot, replaced);
    pd_bridge_log(msg);
    return true;
}

bool pd_slot_load(int slot, const char *dir, const char *file)
{
    if (!slot_ok(slot) || !dir || !file || !s_pd_initialised) return false;

    pd_slot_unload(slot);

    /* volume-wrapped copy; fall back to the original if SD is not writable */
    char copy[32];
    if (make_slot_copy(slot, dir, file, copy, sizeof(copy)))
        s_slots[slot] = libpd_openfile(copy, SLOT_TMP_DIR);
    if (!s_slots[slot]) {
        pd_bridge_log("slot copy failed, volume fixed at 100%");
        s_slots[slot] = libpd_openfile(file, dir);
    }
    if (!s_slots[slot]) {
        log_fmt("ERROR opening: %s/%s", dir, file);
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

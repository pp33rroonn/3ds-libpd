/* pd_bridge.c - libpd Initialisierung + Callbacks + soundfiler-Shim */

#include "pd_bridge.h"
#include "../fs/soundfiler_shim.h"
#include "../ui/pd_log.h"

#include <z_libpd.h>
#include <stdio.h>
#include <string.h>

#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

static SyncClock *s_clk   = NULL;
static void      *s_patch = NULL;

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

    /* libpd starten */
    libpd_init();
    libpd_init_audio(0, 2, SAMPLERATE); /* 0 In, 2 Out (Stereo, interleaved L/R) */

    /* Suchpfade fuer Samples/Abstractions (nach libpd_init) */
    for (size_t i = 0; i < sizeof(PD_SEARCH_PATHS) / sizeof(PD_SEARCH_PATHS[0]); i++) {
        libpd_add_to_search_path(PD_SEARCH_PATHS[i]);
    }

    /* BPM-Receiver abonnieren */
    libpd_bind("pd-bpm");

    /* Patch oeffnen */
    s_patch = libpd_openfile(patch_file, patch_dir);
    if (!s_patch) {
        log_fmt("FEHLER oeffnen: %s/%s", patch_dir, patch_file);
        return false;
    }

    /* DSP einschalten: [pd dsp 1] */
    libpd_start_message(1);
    libpd_add_float(1.0f);
    libpd_finish_message("pd", "dsp");

    log_fmt("geladen: %s/%s", patch_dir, patch_file);

    return true;
}

void pd_bridge_deinit(void)
{
    if (s_patch) {
        libpd_closefile(s_patch);
        s_patch = NULL;
    }
}

bool pd_bridge_reload(const char *patch_dir, const char *patch_file)
{
    /* Alten Patch schliessen */
    if (s_patch) {
        libpd_closefile(s_patch);
        s_patch = NULL;
    }

    /* Neuen Patch oeffnen */
    pd_bridge_log("---");
    s_patch = libpd_openfile(patch_file, patch_dir);
    if (!s_patch) {
        log_fmt("FEHLER oeffnen: %s/%s", patch_dir, patch_file);
        return false;
    }

    /* DSP wieder einschalten */
    libpd_start_message(1);
    libpd_add_float(1.0f);
    libpd_finish_message("pd", "dsp");

    log_fmt("geladen: %s/%s", patch_dir, patch_file);
    return true;
}

void pd_bridge_set_dsp(bool on)
{
    libpd_start_message(1);
    libpd_add_float(on ? 1.0f : 0.0f);
    libpd_finish_message("pd", "dsp");
}

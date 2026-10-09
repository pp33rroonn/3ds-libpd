/* pd_bridge.c - libpd Initialisierung + Callbacks + soundfiler-Shim */

#include "pd_bridge.h"
#include "../fs/soundfiler_shim.h"

#include <z_libpd.h>
#include <stdio.h>
#include <string.h>

#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

static SyncClock *s_clk   = NULL;
static void      *s_patch = NULL;

/* ---- libpd print callback ---------------------------------------------- */
static void pd_print(const char *msg)
{
    fprintf(stderr, "[PD] %s", msg);
}

/* ---- libpd float message callback -------------------------------------- */
static void pd_float_msg(const char *recv, float val)
{
    /* PD-Patch kann Tempo steuern: [send pd-bpm 130] */
    if (strcmp(recv, "pd-bpm") == 0 && s_clk) {
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
    libpd_init_audio(0, 1, SAMPLERATE); /* 0 In, 1 Out (Mono -> linker Kanal) */

    /* Suchpfade fuer Samples/Abstractions (nach libpd_init) */
    for (size_t i = 0; i < sizeof(PD_SEARCH_PATHS) / sizeof(PD_SEARCH_PATHS[0]); i++) {
        libpd_add_to_search_path(PD_SEARCH_PATHS[i]);
    }

    /* BPM-Receiver abonnieren */
    libpd_bind("pd-bpm");

    /* Patch oeffnen */
    s_patch = libpd_openfile(patch_file, patch_dir);
    if (!s_patch) {
        fprintf(stderr, "[pd_bridge] Patch nicht gefunden: %s/%s\n",
                patch_dir, patch_file);
        return false;
    }

    /* DSP einschalten: [pd dsp 1] */
    libpd_start_message(1);
    libpd_add_float(1.0f);
    libpd_finish_message("pd", "dsp");

    fprintf(stderr, "[pd_bridge] OK: %s/%s\n", patch_dir, patch_file);
    fprintf(stderr, "[pd_bridge] soundfiler schreibt nach: sdmc:/3ds/libpd_3ds/recordings/\n");

    return true;
}

void pd_bridge_deinit(void)
{
    if (s_patch) {
        libpd_closefile(s_patch);
        s_patch = NULL;
    }
}

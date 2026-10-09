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

/* ---- soundfiler open-hook ---------------------------------------------- */
/* libpd ruft diesen Hook fuer JEDEN Dateioeffnungsversuch auf:
 *   [soundfiler], [readsf~], [writesf~]
 *
 * Signatur muss exakt dieser libpd-Typedef entsprechen:
 *   typedef FILE* (*t_libpd_openfilehook)(const char *path, const char *mode);
 *
 * Der Shim loest relative Pfade auf und findet Dateien auf sdmc:/ oder romfs:/
 */
static FILE *pd_openfile_hook(const char *path, const char *mode)
{
    return soundfiler_shim_open(path, mode);
}

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

    /* soundfiler open-hook registrieren:
     * libpd_set_openfilehook() ersetzt intern das fopen() fuer alle
     * Dateioperationen (soundfiler, readsf~, writesf~) */
    libpd_set_openfilehook(pd_openfile_hook);

    /* libpd starten */
    libpd_init();
    libpd_init_audio(0, 1, SAMPLERATE); /* 0 In, 1 Out (Mono -> linker Kanal) */

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

#pragma once
/* pd_bridge.h – libpd initialisation + print/message callbacks
 *
 * Abstracts libpd setup so main.c stays lean.
 * BPM messages from PD patches can drive the sync clock:
 *
 *   In your PD patch:   [send pd-bpm] connected to a number
 *   C side:             pd_bridge registers a receiver; on "pd-bpm"
 *                       it calls sync_clock_set_bpm().
 */

#include "sync_clock.h"
#include <stdbool.h>

/**
 * pd_bridge_init – Initialise libpd and open a patch.
 *
 * @param patch_dir   Directory containing the .pd file (e.g. "romfs:/patches").
 * @param patch_file  Filename (e.g. "main.pd").
 * @param clk         SyncClock instance so PD can control tempo via [send pd-bpm].
 *                    Pass NULL to disable tempo-from-PD.
 * @return true on success.
 */
bool pd_bridge_init(const char *patch_dir,
                    const char *patch_file,
                    SyncClock  *clk);

/** pd_bridge_deinit – Close patch and shut down libpd. */
void pd_bridge_deinit(void);

/**
 * pd_bridge_reload – Aktuellen Patch schliessen und neuen oeffnen.
 * DSP bleibt aktiv (kein libpd_init() noetig).
 * @return true bei Erfolg.
 */
bool pd_bridge_reload(const char *patch_dir, const char *patch_file);

/* ---- Diagnose-Log (PD-Meldungen fuer den unteren Bildschirm) ----------- */
#define PD_LOG_LINES 20   /* gespeicherte Zeilen */
#define PD_LOG_COLS  39   /* Zeichen pro Zeile (untere Konsole = 40 Spalten) */

/** Eigene Meldung ins Log schreiben (eine Zeile). */
void pd_bridge_log(const char *msg);
/** Zaehler, der sich bei jeder neuen Log-Zeile erhoeht (fuer Neuzeichnen). */
unsigned pd_bridge_log_serial(void);
/** Zeile i (0 = aelteste) oder "" wenn nicht vorhanden. */
const char *pd_bridge_log_line(int i);

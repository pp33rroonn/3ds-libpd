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

/** pd_bridge_set_dsp - DSP ein-/ausschalten ([; pd dsp 1/0]).
 *  Wird bei CPU-Ueberlast benutzt, damit die App bedienbar bleibt. */
void pd_bridge_set_dsp(bool on);

/* ---- Patch slots -------------------------------------------------------
 * Up to PD_SLOT_COUNT patches run simultaneously in the single libpd
 * instance (outputs are summed). Slot 0 is the main patch used by
 * pd_bridge_init / pd_bridge_reload. */
#define PD_SLOT_COUNT 5

/** Load a patch into slot (0..4), replacing what was there. true on success. */
bool pd_slot_load(int slot, const char *dir, const char *file);
/** Unload slot (close its patch). Slot stays empty. */
void pd_slot_unload(int slot);
bool        pd_slot_is_loaded(int slot);
/** Short display name (<= 20 chars), "" if empty. */
const char *pd_slot_display_name(int slot);
/** Volume 0..1, sent as libpd_float("slot-N-vol", v). Patches need
 *  [receive slot-N-vol] for it to have an effect. */
void  pd_slot_set_vol(int slot, float v);
float pd_slot_get_vol(int slot);

/* ---- Diagnose-Log (PD-Meldungen fuer den unteren Bildschirm) ----------- */
#define PD_LOG_LINES 20   /* gespeicherte Zeilen */
#define PD_LOG_COLS  63   /* chars per line (top screen PD Log page, 66 columns) */

/** Eigene Meldung ins Log schreiben (eine Zeile). */
void pd_bridge_log(const char *msg);
/** Zaehler, der sich bei jeder neuen Log-Zeile erhoeht (fuer Neuzeichnen). */
unsigned pd_bridge_log_serial(void);
/** Zeile i (0 = aelteste) oder "" wenn nicht vorhanden. */
const char *pd_bridge_log_line(int i);

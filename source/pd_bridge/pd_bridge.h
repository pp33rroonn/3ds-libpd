#pragma once
/* pd_bridge.h – libpd initialisation + print/message callbacks
 *
 * Abstracts libpd setup so main.c stays lean.
 * BPM messages from PD patches can drive the sync clock:
 *
 *   In your PD patch:   [send pd-bpm] connected to a number
 *   C side:             pd_bridge registers a receiver; on "pd-bpm"
 *                       it calls sync_clock_set_bpm() - except while a
 *                       patch is being opened ([loadbang] cannot override
 *                       the OPTIONS tempo).
 */

#include "sync_clock.h"
#include <stdbool.h>
#include "../fs/pd_parser.h"   /* PdCtrlList (CTRL feedback) */

/**
 * pd_bridge_init - Initialise libpd and switch DSP on. No patch is opened:
 * all slots start empty and are loaded via pd_slot_load (OPTIONS).
 * @param clk  SyncClock so PD can control tempo via [send pd-bpm] (may be NULL).
 */
bool pd_bridge_init(SyncClock *clk);

/** pd_bridge_deinit - Close all patches. */
void pd_bridge_deinit(void);

/** pd_bridge_set_dsp - DSP ein-/ausschalten ([; pd dsp 1/0]).
 *  Wird bei CPU-Ueberlast benutzt, damit die App bedienbar bleibt. */
void pd_bridge_set_dsp(bool on);

/* ---- Patch slots -------------------------------------------------------
 * Up to PD_SLOT_COUNT patches run simultaneously in the single libpd
 * instance (outputs are summed). All slots are equal, slot 0 = "Patch 1". */
#define PD_SLOT_COUNT 5

/** Load a patch into slot (0..4), replacing what was there. true on success. */
bool pd_slot_load(int slot, const char *dir, const char *file);
/** Unload slot (close its patch). Slot stays empty. */
void pd_slot_unload(int slot);
bool        pd_slot_is_loaded(int slot);
/** Short display name (<= 20 chars), "" if empty. */
const char *pd_slot_display_name(int slot);
/** Volume 0..1, sent as libpd_float("slot-N-vol", v). Works for every patch:
 *  on load each plain [dac~] is replaced by an inline [pd slotdac] subpatch (gain + meter). */
void  pd_slot_set_vol(int slot, float v);
float pd_slot_get_vol(int slot);
/** Output level of the slot 0..1 (from env~ in [pd slotdac]). */
float pd_slot_get_level(int slot);

/* ---- Input routing ----------------------------------------------------
 * Only ONE slot receives the inputs (buttons, pad, touch, accel, gyro,
 * cstick, CTRL panel): the patch of the active top-screen page. The slot
 * copy renames those receivers to "s<slot+1>-<name>" (see pd_rewrite.h),
 * so pd_input_float("touch-x", v) reaches [r touch-x] of that slot only. */

/** Set the slot that receives input (-1 = none: Waveform, Log, OPTIONS). */
void pd_bridge_set_input_slot(int slot);
int  pd_bridge_get_input_slot(void);
/** Send to the input slot (no-op if it is -1 or empty). name = plain name. */
void pd_input_float(const char *name, float v);
void pd_input_bang(const char *name);
/** Send to a specific slot (e.g. touch-off to the slot that lost input). */
void pd_slot_input_float(int slot, const char *name, float v);
void pd_slot_input_bang(int slot, const char *name);

/* ---- CTRL list per slot (PD <-> panel) ----------------------------------
 * pd_slot_load() reads the patch's CTRL entries (pd_parse) and binds
 * "s<slot+1>-<name>" for each BEFORE opening the patch, so values the patch
 * sends itself (loadbang, [s cutoff], sequencer, ...) update ctrls[].val.
 * The list stays valid (and keeps its values) until the slot is unloaded
 * or reloaded. */
/** CTRL list of a loaded slot, NULL if the slot is empty. */
PdCtrlList *pd_slot_ctrls(int slot);
/** true (once) if the patch in `slot` changed a value since the last call. */
bool pd_slot_ctrls_take_dirty(int slot);

/* ---- Diagnose-Log (PD-Meldungen fuer den unteren Bildschirm) ----------- */
#define PD_LOG_LINES 20   /* gespeicherte Zeilen */
#define PD_LOG_COLS  63   /* chars per line (top screen PD Log page, 66 columns) */

/** Eigene Meldung ins Log schreiben (eine Zeile). */
void pd_bridge_log(const char *msg);
/** Zaehler, der sich bei jeder neuen Log-Zeile erhoeht (fuer Neuzeichnen). */
unsigned pd_bridge_log_serial(void);
/** Zeile i (0 = aelteste) oder "" wenn nicht vorhanden. */
const char *pd_bridge_log_line(int i);

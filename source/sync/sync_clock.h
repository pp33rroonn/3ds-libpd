#pragma once
/* sync_clock.h – Audio-Rate Sync-Puls-Generator (1 / 2 / 24 ppqn)
 *
 * Erzeugt einen Rechteck-Puls-Zug auf dem RECHTEN Stereo-Kanal des
 * 3DS-Kopfhörerausgangs, damit externes Gerät (Volcas, Pocket Operators,
 * Modular, etc.) zum 3DS-Tempo synchronisiert werden kann.
 *
 * Unterstützte ppqn-Modi (Laufzeit umschaltbar via sync_clock_set_ppqn):
 *   1ppqn  – Ein Puls pro Viertelnote  (ältere Korg-Geräte, MS-Serie)
 *   2ppqn  – Zwei Pulse pro Viertel    (Korg Volca, Teenage Engineering PO)
 *   24ppqn – 24 Pulse pro Viertel      (MIDI-Clock / DIN-Sync)
 *
 * Puls-Protokoll:
 *   – Puls HIGH : INT16_MAX  (≈ +3.5 V Spitze via Kopfhörer-Amp)
 *   – Puls LOW  : 0
 *   – Puls-Breite: SYNC_PULSE_DUTY_SAMPLES Samples (Standard: 16 Smp)
 *
 * Verwendung:
 *   SyncClock *clk = sync_clock_create(44100, 120.0f);
 *   // pro Audio-Fill-Zyklus:
 *   sync_clock_fill(clk, right_channel_buf, NDSP_BUFSIZE);
 *   // BPM zur Laufzeit ändern:
 *   sync_clock_set_bpm(clk, new_bpm);
 *   // ppqn-Modus wechseln (thread-safe):
 *   sync_clock_set_ppqn(clk, 24);
 */

#include <stdint.h>
#include <stddef.h>

/* Pulse duty width in samples.  16 smp @ 44100 Hz ≈ 0.36 ms – enough for
 * most analogue sync inputs.  Increase if hardware misses pulses. */
#ifndef SYNC_PULSE_DUTY_SAMPLES
#define SYNC_PULSE_DUTY_SAMPLES 16
#endif

/* ppqn: defined via Makefile (-DSYNC_PPQN=2).  Kept here as fallback. */
#ifndef SYNC_PPQN
#define SYNC_PPQN 2
#endif

typedef struct SyncClock SyncClock;

/**
 * sync_clock_create – Allocate and initialise a sync clock.
 *
 * @param sample_rate  Audio sample rate in Hz (must match NDSP / libpd rate).
 * @param bpm          Initial tempo in beats per minute.
 * @return Heap-allocated SyncClock, or NULL on allocation failure.
 */
SyncClock *sync_clock_create(uint32_t sample_rate, float bpm);

/**
 * sync_clock_fill – Write one buffer of sync pulses into @out.
 *
 * Fills @n samples of @out with INT16_MAX (pulse HIGH) or 0 (pulse LOW)
 * according to current BPM and internal phase accumulator.
 *
 * @param clk  Clock instance.
 * @param out  Output buffer (int16_t, length @n).  Matches right_channel_buf
 *             passed to audio_init().
 * @param n    Number of samples to fill (= NDSP_BUFSIZE).
 */
void sync_clock_fill(SyncClock *clk, int16_t *out, size_t n);

/**
 * sync_clock_set_bpm – Thread-safe BPM update (uses atomic store).
 *
 * Safe to call from the main loop while audio runs on a separate thread /
 * NDSP callback.
 */
void sync_clock_set_bpm(SyncClock *clk, float bpm);

/** sync_clock_get_bpm – Return the current BPM value. */
float sync_clock_get_bpm(const SyncClock *clk);

/**
 * sync_clock_set_ppqn – Thread-safe ppqn-Modus wechseln.
 *
 * Erlaubte Werte: 1, 2, 24.  Andere Werte werden ignoriert.
 * Kann aus dem Main-Loop aufgerufen werden während Audio läuft.
 * Der Phasen-Akkumulator wird NICHT zurückgesetzt (kein Timing-Glitch).
 */
void sync_clock_set_ppqn(SyncClock *clk, uint32_t ppqn);

/** sync_clock_get_ppqn – Aktuellen ppqn-Wert lesen. */
uint32_t sync_clock_get_ppqn(const SyncClock *clk);

/** sync_clock_destroy – Alle Ressourcen freigeben. */
void sync_clock_destroy(SyncClock *clk);

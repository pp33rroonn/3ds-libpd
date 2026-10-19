#pragma once
/* sync_clock.h - global master clock (24 ppqn) + sync pulse output
 *
 * The master clock drives the clock-* receivers of ALL patches
 * (clock-step, clock-tick, clock-bpm, clock-ms; see audio.c) and the
 * sync pulse on the right channel. Pulse rate = ppqn x rate factor.
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
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Pulse duty width in samples.  16 smp @ 44100 Hz ≈ 0.36 ms – enough for
 * most analogue sync inputs.  Increase if hardware misses pulses. */
#ifndef SYNC_PULSE_DUTY_SAMPLES
#define SYNC_PULSE_DUTY_SAMPLES 16
#endif

/* ppqn: defined via Makefile (-DSYNC_PPQN=2).  Kept here as fallback. */
#ifndef SYNC_PPQN
#define SYNC_PPQN 2
#endif

/* Master clock resolution (MIDI standard). */
#define SYNC_CLOCK_PPQN   24
#define SYNC_BPM_MIN      20.0f
#define SYNC_BPM_MAX     300.0f
#define SYNC_SWING_MIN    50    /* % : 50 = straight                    */
#define SYNC_SWING_MAX    75
#define SYNC_RATE_Q4_MIN   1    /* sync pulse rate in quarters: 1 = x0.25 */
#define SYNC_RATE_Q4_MAX   8    /*                              8 = x2    */

typedef struct SyncClock SyncClock;

/* What happened during one sync_clock_render() call. At <= 300 BPM a tick
 * is >= 367 samples long, so a 64-sample block holds at most one tick and
 * one step; the counters are there for safety. */
typedef struct {
    int      ticks;     /* ppqn ticks crossed                          */
    uint64_t tick;      /* index of the last tick (since RESET)        */
    int      steps;     /* 16th steps crossed (swing applied)          */
    uint64_t step;      /* index of the last step (since RESET)        */
    bool     info;      /* BPM changed / info requested -> resend      */
    bool     reset;     /* counters were reset to 0                    */
    float    bpm;
} SyncClockEvents;

SyncClock *sync_clock_create(uint32_t sample_rate, float bpm);

/**
 * sync_clock_render - advance the master clock by @n samples.
 *
 * Always call it (sync on or off): it is the only time base of the app.
 * @out  Sync pulse output (INT16_MAX / 0), or NULL when the pulse is off.
 * @ev   Filled with the clock events of this block (for clock-* in PD).
 */
void sync_clock_render(SyncClock *clk, int16_t *out, size_t n,
                       SyncClockEvents *ev);

/* Thread-safe setters (main loop); applied at the next render call. */
void     sync_clock_set_bpm(SyncClock *clk, float bpm);   /* clamped 20..300 */
float    sync_clock_get_bpm(const SyncClock *clk);
void     sync_clock_set_ppqn(SyncClock *clk, uint32_t ppqn); /* 1, 2 or 24 */
uint32_t sync_clock_get_ppqn(const SyncClock *clk);
void     sync_clock_set_swing(SyncClock *clk, int pct);   /* 50..75 %       */
void     sync_clock_set_rate_q4(SyncClock *clk, int q4);  /* 1..8 = x0.25..x2 */

/* Restart all counters at 0 (every patch jumps to beat 1). */
void     sync_clock_reset(SyncClock *clk);

/* Resend clock-bpm / clock-ms at the next render (e.g. after a patch load). */
void     sync_clock_request_info(SyncClock *clk);

void     sync_clock_destroy(SyncClock *clk);

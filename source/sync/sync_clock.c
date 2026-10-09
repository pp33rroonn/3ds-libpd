/* sync_clock.c – Audio-Rate Sync-Puls-Generator (1 / 2 / 24 ppqn)
 *
 * Nur rechter Kanal.  PD-Audio liegt ausschließlich auf dem linken Kanal.
 *
 * Phasen-Akkumulator-Prinzip:
 *   samples_per_pulse = sample_rate * 60 / (bpm * ppqn)
 *
 *   Beispiele bei 44100 Hz / 120 BPM:
 *     1ppqn : 44100*60/(120*1 ) = 22050 Samples (~500 ms)
 *     2ppqn : 44100*60/(120*2 ) = 11025 Samples (~250 ms)
 *    24ppqn : 44100*60/(120*24) =   918 Samples (~21 ms)
 *
 * Der Akkumulator zählt pro Sample +1; bei Erreichen von samples_per_pulse
 * wird er zurückgesetzt und ein Puls HIGH-Fenster geöffnet.
 * Nach SYNC_PULSE_DUTY_SAMPLES fällt der Ausgang auf 0 zurück.
 *
 * BPM und ppqn sind _Atomic → thread-safe schreibbar aus dem Main-Loop.
 */

#include "sync_clock.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <math.h>

/* ---- Erlaubte ppqn-Werte ----------------------------------------------- */
/* Nur 1, 2 und 24 sind zulässig; ungültige Werte werden in set_ppqn() abgefangen. */
static const uint32_t VALID_PPQN[] = {1, 2, 24};
#define VALID_PPQN_COUNT  3

static int ppqn_is_valid(uint32_t p)
{
    for (int i = 0; i < VALID_PPQN_COUNT; i++)
        if (VALID_PPQN[i] == p) return 1;
    return 0;
}

/* ---- Struct --------------------------------------------------------------- */

struct SyncClock {
    uint32_t          sample_rate;
    _Atomic float     bpm;          /* Main-Loop schreibt, fill() liest  */
    _Atomic uint32_t  ppqn;         /* Main-Loop schreibt, fill() liest  */

    /* Interner Zustand – nur in sync_clock_fill() angefasst (single writer) */
    float    cached_bpm;            /* letzter verwendeter BPM-Wert      */
    uint32_t cached_ppqn;           /* letzter verwendeter ppqn-Wert     */
    float    samples_per_pulse;     /* wird bei BPM/ppqn-Änderung neu berechnet */
    float    phase;                 /* Sub-Sample-Akkumulator             */
    uint32_t duty_countdown;        /* verbleibende Samples des HIGH-Fensters   */
};

/* ---- Hilfsfunktionen --------------------------------------------------- */

static inline float compute_period(uint32_t sr, float bpm, uint32_t ppqn)
{
    /* BPM und ppqn gegen Null/Overflow absichern */
    if (bpm  < 1.0f)    bpm  = 1.0f;
    if (bpm  > 999.0f)  bpm  = 999.0f;
    if (ppqn < 1)       ppqn = 1;

    /* Samples zwischen zwei Puls-Onsets:
     *   period = sample_rate * 60 / (bpm * ppqn)
     *
     * WICHTIG: ppqn ist hier der Rate-Multiplikator (Pulse/Takt),
     * NICHT die Puffer-Größe oder Samplerate. */
    return (float)sr * 60.0f / (bpm * (float)ppqn);
}

/* ---- Öffentliche API --------------------------------------------------- */

SyncClock *sync_clock_create(uint32_t sample_rate, float bpm)
{
    SyncClock *clk = calloc(1, sizeof(SyncClock));
    if (!clk) return NULL;

    /* Compile-Time-Standard (Makefile -DSYNC_PPQN=2) als Startwert */
#ifndef SYNC_PPQN
#  define SYNC_PPQN 2
#endif
    uint32_t initial_ppqn = SYNC_PPQN;
    if (!ppqn_is_valid(initial_ppqn)) initial_ppqn = 2;

    clk->sample_rate        = sample_rate;
    atomic_store(&clk->bpm,  bpm);
    atomic_store(&clk->ppqn, initial_ppqn);
    clk->cached_bpm         = bpm;
    clk->cached_ppqn        = initial_ppqn;
    clk->samples_per_pulse  = compute_period(sample_rate, bpm, initial_ppqn);
    clk->phase              = 0.0f;
    clk->duty_countdown     = 0;

    return clk;
}

void sync_clock_fill(SyncClock *clk, int16_t *out, size_t n)
{
    /* BPM und ppqn einmal pro Puffer lesen (atomic load – günstig) */
    float    current_bpm  = atomic_load(&clk->bpm);
    uint32_t current_ppqn = atomic_load(&clk->ppqn);

    /* Neu berechnen wenn BPM oder ppqn geändert wurde */
    if (current_bpm  != clk->cached_bpm  ||
        current_ppqn != clk->cached_ppqn) {
        clk->cached_bpm         = current_bpm;
        clk->cached_ppqn        = current_ppqn;
        clk->samples_per_pulse  = compute_period(clk->sample_rate,
                                                  current_bpm, current_ppqn);
        /* Phase NICHT zurücksetzen – verhindert Timing-Glitch beim Wechsel */
    }

    const float  period = clk->samples_per_pulse;
    float        phase  = clk->phase;
    uint32_t     duty   = clk->duty_countdown;

    for (size_t i = 0; i < n; i++) {
        phase += 1.0f;

        if (phase >= period) {
            phase -= period;             /* Wrap mit Sub-Sample-Genauigkeit */
            duty   = SYNC_PULSE_DUTY_SAMPLES;
        }

        if (duty > 0) {
            out[i] = INT16_MAX;          /* Puls HIGH */
            duty--;
        } else {
            out[i] = 0;                  /* Puls LOW  */
        }
    }

    clk->phase          = phase;
    clk->duty_countdown = duty;
}

void sync_clock_set_bpm(SyncClock *clk, float bpm)
{
    atomic_store(&clk->bpm, bpm);
}

float sync_clock_get_bpm(const SyncClock *clk)
{
    return atomic_load(&clk->bpm);
}

void sync_clock_set_ppqn(SyncClock *clk, uint32_t ppqn)
{
    /* Nur erlaubte Werte (1, 2, 24) akzeptieren; ungültige ignorieren */
    if (!ppqn_is_valid(ppqn)) return;
    atomic_store(&clk->ppqn, ppqn);
}

uint32_t sync_clock_get_ppqn(const SyncClock *clk)
{
    return atomic_load(&clk->ppqn);
}

void sync_clock_destroy(SyncClock *clk)
{
    free(clk);
}

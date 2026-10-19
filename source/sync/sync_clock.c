/* sync_clock.c - global master clock (24 ppqn) + sync pulse output
 *
 * One master position `pos`, measured in ppqn ticks (24 per quarter note),
 * advances by  bpm * 24 / (60 * sample_rate)  per sample. Everything is
 * derived from this single position, so nothing can drift apart:
 *
 *   tick  k : pos == k
 *   step  n : 16th notes, a pair spans 12 ticks; the even step sits at
 *             12*(n/2), the odd step at 12*(n/2) + 12*swing/100
 *             (swing 50 % = straight = 6 ticks)
 *   pulse m : pos == m * 96 / (ppqn * rate_q4)   (rate = rate_q4 / 4)
 *
 * Positions are computed from the event index, never accumulated, so a
 * fractional rate (x0.75, x1.5 ...) or a BPM change cannot build up drift.
 * BPM changes keep `pos` continuous (no jump); RESET sets pos = 0.
 *
 * Writers (main loop): atomics. Reader / state owner: sync_clock_render().
 */

#include "sync_clock.h"

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <math.h>

static const uint32_t VALID_PPQN[] = {1, 2, 24};
#define VALID_PPQN_COUNT  3

static int ppqn_is_valid(uint32_t p)
{
    for (int i = 0; i < VALID_PPQN_COUNT; i++)
        if (VALID_PPQN[i] == p) return 1;
    return 0;
}

struct SyncClock {
    uint32_t          sample_rate;
    _Atomic float     bpm;
    _Atomic uint32_t  ppqn;
    _Atomic uint32_t  swing_pct;      /* SYNC_SWING_MIN..MAX              */
    _Atomic uint32_t  rate_q4;        /* rate in quarters: 1..8           */
    _Atomic bool      reset_req;
    _Atomic bool      info_req;

    /* render state - only touched by sync_clock_render() */
    double   pos;                     /* master position in ticks         */
    uint64_t next_tick;
    uint64_t next_step;
    uint64_t next_pulse;
    float    cached_bpm;
    uint32_t cached_ppqn;
    uint32_t cached_rate_q4;
    uint32_t duty_countdown;
};

static float clamp_bpm(float bpm)
{
    if (bpm < SYNC_BPM_MIN) bpm = SYNC_BPM_MIN;
    if (bpm > SYNC_BPM_MAX) bpm = SYNC_BPM_MAX;
    return bpm;
}

static double pulse_interval(uint32_t ppqn, uint32_t rate_q4)
{
    return (double)(SYNC_CLOCK_PPQN * 4) / (double)(ppqn * rate_q4);
}

static double step_pos(uint64_t step, uint32_t swing_pct)
{
    double base = (double)(step / 2) * 12.0;
    return (step & 1) ? base + 12.0 * (double)swing_pct / 100.0 : base;
}

SyncClock *sync_clock_create(uint32_t sample_rate, float bpm)
{
    SyncClock *clk = calloc(1, sizeof(SyncClock));
    if (!clk) return NULL;

    uint32_t initial_ppqn = SYNC_PPQN;
    if (!ppqn_is_valid(initial_ppqn)) initial_ppqn = 2;

    bpm = clamp_bpm(bpm);
    clk->sample_rate = sample_rate;
    atomic_store(&clk->bpm, bpm);
    atomic_store(&clk->ppqn, initial_ppqn);
    atomic_store(&clk->swing_pct, SYNC_SWING_MIN);
    atomic_store(&clk->rate_q4, 4u);
    atomic_store(&clk->reset_req, false);
    atomic_store(&clk->info_req, true);   /* first render announces BPM */
    clk->cached_bpm     = bpm;
    clk->cached_ppqn    = initial_ppqn;
    clk->cached_rate_q4 = 4;
    return clk;
}

void sync_clock_render(SyncClock *clk, int16_t *out, size_t n,
                       SyncClockEvents *ev)
{
    memset(ev, 0, sizeof(*ev));

    float    bpm   = atomic_load(&clk->bpm);
    uint32_t ppqn  = atomic_load(&clk->ppqn);
    uint32_t rate  = atomic_load(&clk->rate_q4);
    uint32_t swing = atomic_load(&clk->swing_pct);

    if (atomic_exchange(&clk->reset_req, false)) {
        clk->pos = 0.0;
        clk->next_tick = clk->next_step = clk->next_pulse = 0;
        clk->duty_countdown = 0;
        ev->reset = true;
        ev->info  = true;
    }
    if (atomic_exchange(&clk->info_req, false)) ev->info = true;
    if (bpm != clk->cached_bpm) { clk->cached_bpm = bpm; ev->info = true; }

    double interval = pulse_interval(ppqn, rate);
    if (ppqn != clk->cached_ppqn || rate != clk->cached_rate_q4) {
        /* re-phase onto the new grid (anchored at pos 0 -> bar-aligned) */
        clk->cached_ppqn    = ppqn;
        clk->cached_rate_q4 = rate;
        clk->next_pulse     = (uint64_t)ceil(clk->pos / interval);
    }

    double   inc   = (double)bpm * SYNC_CLOCK_PPQN / (60.0 * clk->sample_rate);
    double   pos   = clk->pos;
    double   tpos  = (double)clk->next_tick;
    double   spos  = step_pos(clk->next_step, swing);
    double   ppos  = (double)clk->next_pulse * interval;
    uint32_t duty  = clk->duty_countdown;

    for (size_t i = 0; i < n; i++) {
        if (pos >= tpos) {
            ev->ticks++;
            ev->tick = clk->next_tick++;
            tpos = (double)clk->next_tick;
        }
        if (pos >= spos) {
            ev->steps++;
            ev->step = clk->next_step++;
            spos = step_pos(clk->next_step, swing);
        }
        if (pos >= ppos) {
            clk->next_pulse++;
            ppos = (double)clk->next_pulse * interval;
            duty = SYNC_PULSE_DUTY_SAMPLES;
        }
        if (out) out[i] = duty ? INT16_MAX : 0;
        if (duty) duty--;
        pos += inc;
    }

    clk->pos            = pos;
    clk->duty_countdown = duty;
    ev->bpm             = bpm;
}

void sync_clock_set_bpm(SyncClock *clk, float bpm)
{
    atomic_store(&clk->bpm, clamp_bpm(bpm));
}

float sync_clock_get_bpm(const SyncClock *clk)
{
    return atomic_load(&((SyncClock *)clk)->bpm);
}

void sync_clock_set_ppqn(SyncClock *clk, uint32_t ppqn)
{
    if (!ppqn_is_valid(ppqn)) return;
    atomic_store(&clk->ppqn, ppqn);
}

uint32_t sync_clock_get_ppqn(const SyncClock *clk)
{
    return atomic_load(&((SyncClock *)clk)->ppqn);
}

void sync_clock_set_swing(SyncClock *clk, int pct)
{
    if (pct < SYNC_SWING_MIN) pct = SYNC_SWING_MIN;
    if (pct > SYNC_SWING_MAX) pct = SYNC_SWING_MAX;
    atomic_store(&clk->swing_pct, (uint32_t)pct);
}

void sync_clock_set_rate_q4(SyncClock *clk, int q4)
{
    if (q4 < SYNC_RATE_Q4_MIN) q4 = SYNC_RATE_Q4_MIN;
    if (q4 > SYNC_RATE_Q4_MAX) q4 = SYNC_RATE_Q4_MAX;
    atomic_store(&clk->rate_q4, (uint32_t)q4);
}

void sync_clock_reset(SyncClock *clk)
{
    atomic_store(&clk->reset_req, true);
}

void sync_clock_request_info(SyncClock *clk)
{
    if (clk) atomic_store(&clk->info_req, true);
}

void sync_clock_destroy(SyncClock *clk)
{
    free(clk);
}

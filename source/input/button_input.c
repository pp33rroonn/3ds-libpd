/* button_input.c - BPM-Steuerung via Tasten, Circle-Pad und Tap-Tempo
 *
 * Tap-Tempo-Algorithmus
 * ---------------------
 * Jeder A-Druck speichert den aktuellen Tick-Zeitstempel.
 * Ab dem zweiten Tap wird der Mittelwert aller Intervalle berechnet:
 *   bpm = 60.0 / (mittleres_intervall_in_sekunden)
 * Tick-Quelle: svcGetSystemTick() (268 MHz CPU-Takt = CPU_CLOCK Ticks/s)
 * Timeout: wird ein neuer Tap nach >2 Sekunden beendet, wird der
 * Tap-Puffer geleert und neu begonnen.
 *
 * Circle-Pad
 * ----------
 * Y-Achse: -156..+156 raw. Ausserhalb der Totzone wird ein
 * kontinuierlicher BPM-Delta pro Frame erzeugt:
 *   delta = (y / 156.0) * CPPAD_BPM_RATE
 * Default CPPAD_BPM_RATE = 2.0 BPM/Frame bei Vollausschlag.
 *
 * BPM-Clamping
 * ------------
 * Nach JEDER Aenderung (Taste, Tap, Circle-Pad) wird BPM
 * sofort auf [BPM_MIN, BPM_MAX] geclampt, bevor sync_clock_set_bpm()
 * aufgerufen wird.
 *
 * PD-Messages (neu)
 * -----------------
 * Zusaetzlich zu den Hardware-Funktionen werden folgende libpd-Messages
 * an den laufenden Patch geschickt:
 *   [receive btn-a]  -> bang bei A-Druck (gleichzeitig: Tap-Tempo)
 *   [receive btn-b]  -> bang bei B-Druck
 *   [receive btn-y]  -> bang bei Y-Druck
 *   [receive pad-x]  -> float -1..+1 (Circle-Pad X, jedes Frame ausserhalb
 *                       Totzone; 0.0 wenn Stick zum Zentrum zurueckkehrt)
 *   [receive pad-y]  -> float -1..+1 (Circle-Pad Y, ebenso)
 */

#include "button_input.h"

#include <3ds.h>
#include <z_libpd.h>   /* libpd_bang(), libpd_float() */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* 3DS CPU-Taktfrequenz fuer Zeitberechnung */
#define CPU_CLOCK  268111856ULL   /* Ticks pro Sekunde (ARM11 @ 268 MHz) */

/* Circle-Pad: max BPM-Aenderung pro Frame bei Vollausschlag */
#ifndef CPPAD_BPM_RATE
#  define CPPAD_BPM_RATE 2.0f
#endif

/* Tap-Tempo Timeout in Ticks (2 Sekunden) */
#define TAP_TIMEOUT_TICKS  (CPU_CLOCK * 2ULL)

/* Circle-Pad Normalisierungsfaktor */
#define CPPAD_MAX  156.0f

/* ---- Hilfsfunktionen --------------------------------------------------- */

static inline float clamp_bpm(float bpm)
{
    /* Sofortiges Clampen nach jeder Eingabe-Aenderung */
    if (bpm < BPM_MIN) bpm = BPM_MIN;
    if (bpm > BPM_MAX) bpm = BPM_MAX;
    return bpm;
}

static inline float cppad_delta(s16 raw_y)
{
    /* Totzone anwenden */
    if (raw_y > -CPPAD_DEADZONE && raw_y < CPPAD_DEADZONE) return 0.0f;

    /* Normalisieren auf [-1, +1] und mit Rate skalieren.
     * VORSICHT: raw_y positiv = Stick nach OBEN -> BPM erhoehen. */
    float norm = (float)raw_y / CPPAD_MAX;
    return norm * CPPAD_BPM_RATE;
}

/* Normalisiert raw Circle-Pad Wert auf -1..+1; 0 in Totzone. */
static inline float cppad_normalized(s16 raw)
{
    if (raw > -CPPAD_DEADZONE && raw < CPPAD_DEADZONE) return 0.0f;
    float v = (float)raw / CPPAD_MAX;
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

/* ---- Tap-Tempo --------------------------------------------------------- */

typedef struct {
    u64  stamps[TAP_TEMPO_MAX_TAPS];  /* Zeitstempel-History  */
    int  count;                        /* Anzahl gueltige Taps */
} TapTempo;

static void tap_tempo_reset(TapTempo *tt)
{
    tt->count = 0;
}

/* Neuen Tap hinzufuegen; gibt berechnetes BPM zurueck (oder 0 wenn noch kein Intervall). */
static float tap_tempo_tap(TapTempo *tt, u64 now)
{
    if (tt->count > 0) {
        /* Timeout: letzter Tap zu lange her -> neu starten */
        u64 last = tt->stamps[tt->count - 1];
        if ((now - last) > TAP_TIMEOUT_TICKS) {
            tap_tempo_reset(tt);
        }
    }

    /* Zeitstempel schieben wenn Buffer voll */
    if (tt->count >= TAP_TEMPO_MAX_TAPS) {
        for (int i = 0; i < TAP_TEMPO_MAX_TAPS - 1; i++)
            tt->stamps[i] = tt->stamps[i + 1];
        tt->count = TAP_TEMPO_MAX_TAPS - 1;
    }

    tt->stamps[tt->count++] = now;

    /* Mindestens 2 Taps fuer ein Intervall noetig */
    if (tt->count < 2) return 0.0f;

    /* Mittelwert ueber alle Intervalle */
    u64 total = tt->stamps[tt->count - 1] - tt->stamps[0];
    int intervals = tt->count - 1;
    double avg_ticks = (double)total / (double)intervals;
    double bpm = 60.0 * (double)CPU_CLOCK / avg_ticks;

    return (float)bpm;
}

/* ---- ButtonInputCtx ---------------------------------------------------- */

/* Erlaubte ppqn-Werte (muss mit VALID_PPQN in sync_clock.c uebereinstimmen) */
static const uint32_t PPQN_MODES[]     = {1, 2, 24};
#define PPQN_MODE_COUNT  3
/* Standard-Startindex -> SYNC_PPQN aus Makefile (Fallback: Index 1 = 2ppqn) */
#ifndef SYNC_PPQN
#  define SYNC_PPQN 2
#endif
static int ppqn_default_idx(void) {
    for (int i = 0; i < PPQN_MODE_COUNT; i++)
        if (PPQN_MODES[i] == (uint32_t)SYNC_PPQN) return i;
    return 1; /* Fallback: 2ppqn */
}

struct ButtonInputCtx {
    SyncClock *clk;
    float      bpm;
    bool       sync_on;
    TapTempo   tap;

    /* ppqn-Modus-Index in PPQN_MODES[] (X-Taste cyclet) */
    int        ppqn_idx;

    /* D-Pad Auto-Repeat: Taste gehalten -> wiederholte Aenderung nach Delay */
    u32        held_keys;
    int        repeat_frames;   /* Frames seit Taste gedrueckt */

    /* Letzter gesendeter Circle-Pad-Wert fuer PD (um Null-Message zu senden
     * wenn Stick zum Zentrum zurueckkehrt) */
    float      last_pad_x;
    float      last_pad_y;
};

/* Auto-Repeat Schwellenwerte (in Frames bei 60fps) */
#define REPEAT_DELAY   20   /* Erstes Repeat nach 20 Frames */
#define REPEAT_RATE    4    /* Danach alle 4 Frames */

/* ---- Oeffentliche API -------------------------------------------------- */

ButtonInputCtx *button_input_create(SyncClock *clk)
{
    ButtonInputCtx *ctx = calloc(1, sizeof(ButtonInputCtx));
    if (!ctx) return NULL;

    ctx->clk       = clk;
    ctx->bpm       = clk ? sync_clock_get_bpm(clk) : 120.0f;
    ctx->sync_on   = true;
    ctx->ppqn_idx  = ppqn_default_idx();   /* Startwert aus Makefile -DSYNC_PPQN */
    ctx->last_pad_x = 0.0f;
    ctx->last_pad_y = 0.0f;

    tap_tempo_reset(&ctx->tap);
    return ctx;
}

void button_input_poll(ButtonInputCtx *ctx)
{
    if (!ctx) return;

    /* BPM kann auch vom Patch kommen ([send pd-bpm]) -> vor jeder Tasten-
     * Aenderung den aktuellen Clock-Wert uebernehmen, damit Anzeige und
     * D-Pad-Schritte vom echten Tempo ausgehen. */
    if (ctx->clk) ctx->bpm = sync_clock_get_bpm(ctx->clk);

    /* hidScanInput() wird EINMAL pro Frame in main.c aufgerufen.
     * Hier nur die bereits aktualisierten Zustaende lesen. */
    u32 down  = hidKeysDown();
    u32 held  = hidKeysHeld();
    circlePosition cp;
    hidCircleRead(&cp);

    float delta = 0.0f;

    /* --- D-Pad Feineinstellung (+/-1 BPM) -------------------------------- */
    if (down & KEY_DRIGHT) delta += 1.0f;
    if (down & KEY_DLEFT)  delta -= 1.0f;

    /* --- D-Pad Grobeinstellung (+/-10 BPM) ------------------------------- */
    if (down & KEY_DUP)    delta += 10.0f;
    if (down & KEY_DDOWN)  delta -= 10.0f;

    /* --- Schultertasten (+/-10 BPM) -------------------------------------- */
    if (down & KEY_L)      delta -= 10.0f;
    if (down & KEY_R)      delta += 10.0f;

    /* --- Auto-Repeat fuer D-Pad / Schulter (Taste gehalten) -------------- */
    u32 repeat_keys = KEY_DRIGHT | KEY_DLEFT | KEY_DUP | KEY_DDOWN |
                      KEY_L | KEY_R;
    if (held & repeat_keys) {
        /* Nur wenn KEINE neuen Tasten (down == 0 fuer diese Gruppe) */
        if (!(down & repeat_keys)) {
            ctx->repeat_frames++;
            if (ctx->repeat_frames >= REPEAT_DELAY &&
                (ctx->repeat_frames - REPEAT_DELAY) % REPEAT_RATE == 0) {
                /* Welche Taste ist gehalten? */
                if (held & KEY_DRIGHT) delta += 1.0f;
                if (held & KEY_DLEFT)  delta -= 1.0f;
                if (held & KEY_DUP)    delta += 10.0f;
                if (held & KEY_DDOWN)  delta -= 10.0f;
                if (held & KEY_L)      delta -= 10.0f;
                if (held & KEY_R)      delta += 10.0f;
            }
        } else {
            ctx->repeat_frames = 0;
        }
    } else {
        ctx->repeat_frames = 0;
    }

    /* --- Circle-Pad (kontinuierlich) ------------------------------------- */
    /* Y positiv = Stick oben = BPM hoeher                                  */
    delta += cppad_delta(cp.dy);

    /* --- BPM anwenden (Clampen sofort nach Summation) -------------------- */
    if (delta != 0.0f) {
        ctx->bpm = clamp_bpm(ctx->bpm + delta);
        if (ctx->clk) sync_clock_set_bpm(ctx->clk, ctx->bpm);
    }

    /* --- Tap-Tempo (A-Taste) --------------------------------------------- */
    if (down & KEY_A) {
        u64 now = svcGetSystemTick();
        float tapped_bpm = tap_tempo_tap(&ctx->tap, now);
        if (tapped_bpm > 0.0f) {
            ctx->bpm = clamp_bpm(tapped_bpm);
            if (ctx->clk) sync_clock_set_bpm(ctx->clk, ctx->bpm);
        }
        /* PD-Message: btn-a bang */
        libpd_bang("btn-a");
    }

    /* --- B-Taste -> PD bang ---------------------------------------------- */
    if (down & KEY_B) {
        libpd_bang("btn-b");
    }

    /* --- Y-Taste -> PD bang ---------------------------------------------- */
    if (down & KEY_Y) {
        libpd_bang("btn-y");
    }

    /* --- ppqn-Modus wechseln (X-Taste) ----------------------------------- */
    if (down & KEY_X) {
        ctx->ppqn_idx = (ctx->ppqn_idx + 1) % PPQN_MODE_COUNT;
        uint32_t new_ppqn = PPQN_MODES[ctx->ppqn_idx];
        if (ctx->clk) sync_clock_set_ppqn(ctx->clk, new_ppqn);
    }

    /* --- Sync ein/aus (SELECT) ------------------------------------------- */
    if (down & KEY_SELECT) {
        ctx->sync_on = !ctx->sync_on;
    }

    /* --- Circle-Pad -> PD floats (pad-x, pad-y) -------------------------- */
    /* Normalisiert auf -1..+1; 0 in der Totzone.
     * Nur senden wenn sich der Wert gegenueber dem letzten Frame geaendert
     * hat (minimale Aenderung 0.01), um den PD-Scheduler nicht zu fluten. */
    {
        float nx = cppad_normalized(cp.dx);
        float ny = cppad_normalized(cp.dy);

        float dx = nx - ctx->last_pad_x;
        float dy = ny - ctx->last_pad_y;
        if (dx < 0.0f) dx = -dx;
        if (dy < 0.0f) dy = -dy;

        if (dx >= 0.01f) {
            libpd_float("pad-x", nx);
            ctx->last_pad_x = nx;
        }
        if (dy >= 0.01f) {
            libpd_float("pad-y", ny);
            ctx->last_pad_y = ny;
        }
    }
}

float button_input_get_bpm(const ButtonInputCtx *ctx)
{
    return ctx ? ctx->bpm : 120.0f;
}

bool button_input_sync_enabled(const ButtonInputCtx *ctx)
{
    return ctx ? ctx->sync_on : true;
}

uint32_t button_input_get_ppqn(const ButtonInputCtx *ctx)
{
    if (!ctx) return 2;  /* sicherer Fallback */
    return PPQN_MODES[ctx->ppqn_idx];
}

void button_input_destroy(ButtonInputCtx *ctx)
{
    free(ctx);
}

#pragma once
/* button_input.h - BPM-Steuerung via Tasten, Circle-Pad und Tap-Tempo
 *
 * Tastenbelegung (abhaengig vom App-Modus, siehe app_state.h)
 * ------------------------------------------------------------
 *   L+R gleichzeitig      : OPTIONS ein/aus (zurueck in den vorherigen Modus)
 *   R allein              : PATCH <-> CTRL (in OPTIONS ohne Wirkung)
 *
 *   MODE_PATCH (Standard) : alle Tasten/Circle-Pad nur an PD
 *
 *   MODE_CTRL:
 *   D-Pad Hoch / Runter   : im Slider-Panel scrollen (kein bang an PD)
 *   A/B/X/Y/SELECT        : weiterhin bang an PD (btn-*)
 *   Circle-Pad            : weiterhin pad-x / pad-y an PD
 *
 *   MODE_OPTIONS:
 *   D-Pad Links / Rechts  : BPM -1 / +1   (Feineinstellung)
 *   D-Pad Hoch  / Runter  : BPM +10 / -10 (Grobeinstellung)
 *   A-Taste               : Tap-Tempo (3-8 Taps -> BPM-Mittelwert)
 *   Circle-Pad Y-Achse    : kontinuierliche BPM-Steuerung
 *   X-Taste               : ppqn-Modus wechseln (1 -> 2 -> 24 -> 1 ...)
 *   SELECT                : Sync ein/aus (aus = PD-Stereo auf beiden Kanaelen)
 *
 * PD Receiver-Namen fuer eigene Patches (Tasten/Pad/Touch nur im MODE_PATCH)
 * --------------------------------------
 *   [receive btn-a]  -> bang wenn A gedrueckt
 *   [receive btn-b]  -> bang wenn B gedrueckt
 *   [receive btn-x]  -> bang wenn X gedrueckt
 *   [receive btn-y]  -> bang wenn Y gedrueckt
 *   [receive btn-up|btn-down|btn-left|btn-right] -> bang (D-Pad)
 *   [receive btn-select] -> bang wenn SELECT gedrueckt
 *   [receive pad-x]  -> float -1..+1  (Circle-Pad X-Achse)
 *   [receive pad-y]  -> float -1..+1  (Circle-Pad Y-Achse)
 *   [receive touch-x]   -> float 0..319  (Touch X-Position)
 *   [receive touch-y]   -> float 0..239  (Touch Y-Position)
 *   [receive touch-on]  -> bang beim Aufsetzen des Fingers
 *   [receive touch-off] -> bang beim Abheben des Fingers
 *   [send pd-bpm <float>] -> setzt BPM vom Patch aus
 *
 * BPM-Bereich: BPM_MIN .. BPM_MAX (sofort geclampt nach jeder Aenderung)
 *
 * Verwendung in main.c:
 *   ButtonInputCtx *btn = button_input_create(clk);
 *   // im Main-Loop:
 *   button_input_poll(btn);
 *   // fuer BPM-Anzeige:
 *   float bpm = button_input_get_bpm(btn);
 *   bool  sync_on = button_input_sync_enabled(btn);
 *   uint32_t ppqn = button_input_get_ppqn(btn);
 */

#include "../sync/sync_clock.h"
#include <stdbool.h>

/* BPM-Grenzen */
#define BPM_MIN  20.0f
#define BPM_MAX 300.0f

/* Circle-Pad Totzone (0..156 Skala) */
#ifndef CPPAD_DEADZONE
#  define CPPAD_DEADZONE 24
#endif

/* Maximale Tap-Tempo-History */
#define TAP_TEMPO_MAX_TAPS 8

typedef struct ButtonInputCtx ButtonInputCtx;

/**
 * button_input_create - Kontext allokieren.
 *
 * @param clk  SyncClock-Instanz; BPM-Aenderungen werden direkt
 *             per sync_clock_set_bpm() weitergegeben.
 * @return Heap-allokierter Kontext, oder NULL bei Fehler.
 */
ButtonInputCtx *button_input_create(SyncClock *clk);

/**
 * button_input_poll - Tastenstatus einlesen und BPM anpassen.
 * Einmal pro Frame aus dem Main-Loop aufrufen.
 */
void button_input_poll(ButtonInputCtx *ctx);

/**
 * button_input_toggle_options - OPTIONS ein/aus (intern bei L+R).
 * Beim Oeffnen wird der aktuelle Modus in g_prev_mode gemerkt und
 * pad-x/pad-y im Patch auf 0 gesetzt; beim Schliessen geht es dorthin zurueck.
 */
void button_input_toggle_options(ButtonInputCtx *ctx);

/**
 * button_input_toggle_ctrl - PATCH <-> CTRL (intern bei R allein).
 * Im Modus OPTIONS ohne Wirkung.
 */
void button_input_toggle_ctrl(ButtonInputCtx *ctx);

/**
 * button_input_ctrl_scroll_delta - Scroll-Schritte im CTRL-Modus abholen.
 * D-Pad Hoch = -1, Runter = +1 (mit Auto-Repeat beim Halten).
 * Der Wert wird beim Lesen auf 0 zurueckgesetzt (einmal pro Frame abholen).
 */
int button_input_ctrl_scroll_delta(void);

/** Aktuellen BPM-Wert lesen (fuer Anzeige). */
float button_input_get_bpm(const ButtonInputCtx *ctx);

/** Gibt true zurueck wenn Sync (rechter Kanal) aktiv ist. */
bool button_input_sync_enabled(const ButtonInputCtx *ctx);

/**
 * button_input_get_ppqn - Aktuellen ppqn-Modus lesen (1, 2 oder 24).
 * X-Taste cyclet den Modus; der Wert wird direkt an sync_clock weitergegeben.
 */
uint32_t button_input_get_ppqn(const ButtonInputCtx *ctx);

/** Ressourcen freigeben. */
void button_input_destroy(ButtonInputCtx *ctx);

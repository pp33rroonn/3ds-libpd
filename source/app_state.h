#pragma once
/* app_state.h - globaler App-Modus
 *
 *   MODE_PATCH   (Standard): Tasten, Circle-Pad und Touch gehen nur an PD.
 *   MODE_CTRL              : Slider-Panel fuer die Receiver des Patches
 *                            (Touch = Slider, D-Pad U/D = scrollen,
 *                            A/B/X/Y/SELECT + Circle-Pad weiter an PD).
 *   MODE_OPTIONS           : Tasten steuern BPM/Sync/ppqn, nichts geht an PD.
 *
 * Umschalten:
 *   L+R gleichzeitig : OPTIONS ein/aus (zurueck in den vorherigen Modus,
 *                      gemerkt in g_prev_mode)
 *   R allein         : PATCH <-> CTRL (in OPTIONS ohne Wirkung)
 * START wechselt in allen Modi den Patch.
 * Definition von g_app_mode in main.c, g_prev_mode in button_input.c.
 */

typedef enum { MODE_PATCH = 0, MODE_CTRL = 1, MODE_OPTIONS = 2 } AppMode;

#define MODE_COUNT 3

extern AppMode g_app_mode;
extern AppMode g_prev_mode;   /* Modus vor dem Oeffnen von OPTIONS */

/* Ehemaliger OPT-Button (unten rechts, 320x240 Pixel-Koordinaten).
 * Der Button wird nicht mehr gezeichnet; die Konstanten bleiben fuer
 * moegliche Touch-Bereiche erhalten. */
#define OPT_BTN_X0  256
#define OPT_BTN_Y0  200
#define OPT_BTN_X1  319
#define OPT_BTN_Y1  239

/* Im CTRL-Modus belegt das Slider-Panel die Zeilen 1..29 (letzter Slider
 * endet bei y 223); Touches unten rechts ab y 226 ignoriert das Panel
 * (Zeile 30 = Hinweiszeile). */
#define OPT_BTN_CTRL_Y0  226

#pragma once
/* app_state.h - globaler App-Modus
 *
 *   MODE_PATCH   (Standard): Tasten, Circle-Pad und Touch gehen nur an PD.
 *   MODE_OPTIONS           : Tasten steuern BPM/Sync/ppqn, nichts geht an PD.
 *
 * Umschalten: L+R gleichzeitig oder Touch auf den OPT-Button (unten rechts).
 * START wechselt in beiden Modi den Patch.
 * Definition von g_app_mode in main.c.
 */

typedef enum { MODE_PATCH = 0, MODE_OPTIONS = 1 } AppMode;

extern AppMode g_app_mode;

/* OPT-Button auf dem unteren Bildschirm (320x240), Pixel-Koordinaten.
 * Gezeichnet in Konsolen-Zellen Spalte 34..39 / Zeile 27..29 (1-basiert,
 * 8x8 px pro Zelle = x 264..311, y 208..231); die Touch-Flaeche ist etwas
 * groesser, damit der Button leicht zu treffen ist. */
#define OPT_BTN_X0  256
#define OPT_BTN_Y0  200
#define OPT_BTN_X1  319
#define OPT_BTN_Y1  239

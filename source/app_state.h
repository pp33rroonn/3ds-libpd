#pragma once
/* app_state.h - global app state
 *
 * Bottom-screen mode (g_app_mode):
 *   MODE_PATCH (default): buttons, Circle-Pad and touch go to PD only.
 *   MODE_CTRL           : slider panel for the patch receivers
 *                         (touch = slider, D-pad U/D = scroll,
 *                         A/B/X/Y/SELECT + Circle-Pad still go to PD).
 *   R toggles PATCH <-> CTRL (not while OPTIONS is open).
 *
 * Top screen (pixel UI, see ui/top_screen.h and ui/options_menu.h):
 *   g_app_page     : Mixer / Patch 1..5 / PD Log, ZL / ZR step through pages.
 *   g_options_open : OPTIONS overlay, START opens/closes it. While open,
 *                    nothing is sent to PD.
 *
 * g_app_mode is defined in main.c, g_app_page in ui/top_screen.c,
 * g_options_open and g_menu in ui/options_menu.c.
 */

#include <stdbool.h>
#include "ui/options_menu.h"
#include "ui/top_screen.h"

typedef enum { MODE_PATCH = 0, MODE_CTRL = 1 } AppMode;

#define MODE_COUNT 2

extern AppMode g_app_mode;
extern bool    g_options_open;

/* Former OPT button area (bottom right, 320x240 pixel coordinates).
 * The button is no longer drawn; the constants remain for touch areas. */
#define OPT_BTN_X0  256
#define OPT_BTN_Y0  200
#define OPT_BTN_X1  319
#define OPT_BTN_Y1  239

/* In CTRL mode the slider panel uses console rows 1..29 (the last slider
 * ends at y 223); touches bottom right from y 226 are ignored by the panel
 * (row 30 = hint line). */
#define OPT_BTN_CTRL_Y0  226

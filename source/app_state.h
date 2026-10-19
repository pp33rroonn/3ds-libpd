#pragma once
/* app_state.h - global app state
 *
 * Bottom-screen mode (g_app_mode):
 *   BOT_MIXER (start): pixel mixer (slot volume faders). Touch goes ONLY to
 *                      the mixer faders, never to PD.
 *   BOT_CTRL         : CTRL slider panel of the active patch slot
 *                      (touch = slider + touch-x/y to PD, D-pad U/D = scroll).
 *                      Shows "kein Patch geladen" if no loaded slot is active.
 *   ZL / ZR navigation switches to BOT_CTRL on a patch page and to
 *   BOT_MIXER on Waveform (Waveform always shows the mixer); SELECT toggles
 *   BOT_MIXER <-> BOT_CTRL while a patch page is shown (no-op on Waveform).
 *   Buttons and Circle-Pad go to PD in both modes (not while OPTIONS is open).
 *
 * Top screen (pixel UI, see ui/top_screen.h and ui/options_menu.h):
 *   g_app_page     : Waveform / Patch 1..5 (ZL / ZR cycle), PD Log (ZL+ZR).
 *   g_options_open : OPTIONS overlay, START opens/closes it. While open,
 *                    nothing is sent to PD.
 *
 * g_app_mode is defined in main.c, g_app_page in ui/top_screen.c,
 * g_options_open and g_menu in ui/options_menu.c.
 */

#include <stdbool.h>
#include "ui/options_menu.h"
#include "ui/top_screen.h"

typedef enum { BOT_MIXER = 0, BOT_CTRL = 1 } AppMode;

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

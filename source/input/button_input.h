#pragma once
/* button_input.h - button / Circle-Pad routing
 *
 * Key map (see app_state.h)
 * -------------------------
 *   START                 : OPTIONS overlay open / close (top screen)
 *   ZL / ZR               : previous / next top-screen page
 *   R                     : PATCH <-> CTRL (no effect while OPTIONS is open)
 *
 *   OPTIONS open          : D-pad, L/R, Y -> options menu, nothing to PD
 *                           (BPM, tap tempo, sync mode: see ui/options_menu.h)
 *
 *   MODE_PATCH (default)  : all buttons / Circle-Pad to PD
 *
 *   MODE_CTRL:
 *   D-pad Up / Down       : scroll the slider panel (no bang to PD)
 *   A/B/X/Y/SELECT, L     : still bang to PD (btn-*)
 *   Circle-Pad            : still pad-x / pad-y to PD
 *
 * PD receiver names
 * -----------------
 *   [receive btn-a|btn-b|btn-x|btn-y]            -> bang
 *   [receive btn-up|btn-down|btn-left|btn-right] -> bang (D-pad, PATCH only)
 *   [receive btn-select]                         -> bang
 *   [receive btn-l]                              -> bang
 *   [receive pad-x] / [receive pad-y]            -> float -1..+1
 *   [receive touch-x] / [receive touch-y]        -> float (touch position)
 *   [receive touch-on] / [receive touch-off]     -> bang
 *   [send pd-bpm <float>]                        -> sets BPM from the patch
 *
 * Usage in main.c:
 *   ButtonInputCtx *btn = button_input_create(clk);
 *   button_input_poll(btn);              // once per frame
 *   float bpm = button_input_get_bpm(btn);
 */

#include "../sync/sync_clock.h"
#include <stdbool.h>

/* BPM range */
#define BPM_MIN  20.0f
#define BPM_MAX 300.0f

/* Circle-Pad dead zone (0..156 scale) */
#ifndef CPPAD_DEADZONE
#  define CPPAD_DEADZONE 24
#endif

typedef struct ButtonInputCtx ButtonInputCtx;

/** Allocate the context; clk is used to read the current BPM. */
ButtonInputCtx *button_input_create(SyncClock *clk);

/** Read the buttons and route them. Call once per frame. */
void button_input_poll(ButtonInputCtx *ctx);

/** PATCH <-> CTRL (internally on R). No effect while OPTIONS is open. */
void button_input_toggle_ctrl(ButtonInputCtx *ctx);

/**
 * Fetch scroll steps for CTRL mode (D-pad Up = -1, Down = +1, with
 * auto-repeat). Reset to 0 on read; fetch once per frame.
 */
int button_input_ctrl_scroll_delta(void);

/** Current BPM (follows the sync clock). */
float button_input_get_bpm(const ButtonInputCtx *ctx);

/** Free resources. */
void button_input_destroy(ButtonInputCtx *ctx);

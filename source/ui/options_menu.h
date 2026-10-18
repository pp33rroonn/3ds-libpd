#pragma once
/* options_menu.h - OPTIONS overlay on the top screen
 *
 * Opened/closed with START (button_input.c). While open, D-pad, L/R and Y
 * go to options_menu_input() and nothing is sent to PD.
 *
 * Every row is a scroll list: <  VALUE  >  (no toggles).
 *   D-pad Up/Down     : select row (wraps)
 *   D-pad Left/Right  : change value (auto-repeat when held)
 *   L / R  (Tempo row): -10 / +10 BPM
 *   Y      (Tempo row): tap tempo
 *
 * The menu only records changes; main.c applies them each frame
 * (bpm_changed / sync_changed flags, options_menu_take_patch_request()).
 */

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include "../fs/patch_list.h"

#define MENU_PATCH_SLOTS  5
#define MENU_TAP_MAX      8

typedef enum {
    MENU_PATCH_0 = 0,
    MENU_PATCH_1,
    MENU_PATCH_2,
    MENU_PATCH_3,
    MENU_PATCH_4,
    MENU_TEMPO,
    MENU_SYNC,
    MENU_SPECAN,
    MENU_EYE,
    MENU_ROW_COUNT
} MenuRow;

#define SYNC_MODE_COUNT    4   /* 0 = Off (Stereo), 1 = 1ppqn, 2 = 2ppqn, 3 = 24ppqn */
#define SPECAN_MODE_COUNT  4   /* 0 = Waveform, 1 = Envelope, 2 = Stereo, 3 = Level */

typedef struct {
    int   selected_row;               /* MenuRow                          */
    int   patch_idx[MENU_PATCH_SLOTS];/* index into the PatchList, -1 = Empty */
    float bpm;
    int   sync_mode;                  /* 0..SYNC_MODE_COUNT-1             */
    int   specan_mode;                /* 0..SPECAN_MODE_COUNT-1           */
    int   eye_idx;                    /* theme index 0..17                */
    int   tap_count;
    u64   tap_stamps[MENU_TAP_MAX];

    /* change notifications for main.c (cleared by main.c) */
    bool  bpm_changed;
    bool  sync_changed;

    /* internal: auto-repeat and deferred patch loading */
    int   repeat_frames;
    int   patch_pending;              /* patch index to load, -1 = none   */
    int   patch_settle;               /* frames until patch_pending loads */
} OptionsMenu;

extern bool        g_options_open;
extern OptionsMenu g_menu;

/* Patch list used for slot names and ranges (call before options_menu_init). */
void options_menu_set_patch_list(const PatchList *pl);

void options_menu_init(OptionsMenu *m);
void options_menu_draw(u16 *fb, const OptionsMenu *m, float bpm,
                       int dsp_pct, unsigned glitches);
void options_menu_input(OptionsMenu *m, u32 held, u32 down);

/* Display name of the patch in slot (NULL if the slot is empty). */
const char *options_menu_patch_name(const OptionsMenu *m, int slot);

/* Number of patches in the list (0 if none). */
int options_menu_patch_count(void);

/* Sync pulse enabled (sync_mode != Off) and the selected ppqn (1/2/24). */
bool     options_menu_sync_on(const OptionsMenu *m);
uint32_t options_menu_ppqn(const OptionsMenu *m);

/* Label of the current sync mode, e.g. "2ppqn Volca/PO". */
const char *options_menu_sync_label(const OptionsMenu *m);

/* Call once per frame: returns the patch index slot 0 should load once the
 * selection has settled (no reload storm while scrolling), else -1. */
int options_menu_take_patch_request(OptionsMenu *m);

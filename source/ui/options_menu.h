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
 *   Left/Right (Reset row): restart the global clock at 0 (beat 1)
 *
 * The menu only records changes; main.c applies them each frame
 * (bpm_changed / sync_changed / clock_changed / clock_reset flags, options_menu_take_slot_request()).
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
    MENU_RATE,
    MENU_SWING,
    MENU_RESET,
    MENU_SPECAN,
    MENU_EYE,
    MENU_FADERS,
    MENU_MIC,
    MENU_CAM,
    MENU_CAM_SOURCE,
    MENU_CAM_PIXEL,
    MENU_CAM_GLITCH,
    MENU_ROW_COUNT
} MenuRow;

#define SYNC_MODE_COUNT    4   /* 0 = Off (Stereo), 1 = 1ppqn, 2 = 2ppqn, 3 = 24ppqn */
#define FADER_LAYOUT_COUNT 3   /* 0 Horizontal, 1 Vertical, 2 Auto (= CTRL_LAYOUT_*) */
#define CAM_MODE_COUNT     4   /* 0 Aus, 1 Fader, 2 Bild, 3 Bild+Matrix (MENU_CAM) */
#define CAM_SOURCE_COUNT   2   /* 0 Back (SELECT_OUT1), 1 Front (SELECT_IN1) */
#define CAM_PIXEL_COUNT    5   /* X1..X5 pixelation grid (fine -> coarse)        */
#define CAM_GLITCH_COUNT   6   /* 0 Off, 1..5 glitch intensity (stacks on pixel) */
#define SPECAN_MODE_COUNT 12   /* 0 Waveform, 1 Envelope, 2 Stereo, 3 Level,
                                  4..11 ASCII views (must match ScopeMode) */

typedef struct {
    int   selected_row;               /* MenuRow                          */
    int   patch_idx[MENU_PATCH_SLOTS];/* index into the PatchList, -1 = Empty */
    float bpm;
    int   sync_mode;                  /* 0..SYNC_MODE_COUNT-1             */
    int   swing_pct;                  /* 50..75 (clock-step swing)        */
    int   rate_q4;                    /* sync pulse rate x0.25..x2 = 1..8 */
    int   reset_flash;                /* frames the RESET row shows "done" */
    int   specan_mode;                /* 0..SPECAN_MODE_COUNT-1           */
    int   fader_layout;               /* 0..FADER_LAYOUT_COUNT-1 (CTRL panel) */
    int   eye_idx;                    /* theme index 0..17                */
    int   mic_on;                     /* 0 = Aus, 1 = An (Mikrofon)       */
    int   cam_mode;                   /* 0 Aus,1 Fader,2 Bild,3 Bild+Matrix (CAM_MODE_COUNT) */
    int   cam_source;                 /* 0 Back (SELECT_OUT1), 1 Front (SELECT_IN1) */
    int   cam_pixel;                  /* 1..CAM_PIXEL_COUNT pixelation (X1 fine .. X5 coarse) */
    int   cam_glitch;                 /* 0 Off, 1..5 glitch intensity (stacks on pixel)       */
    int   tap_count;
    u64   tap_stamps[MENU_TAP_MAX];

    /* change notifications for main.c (cleared by main.c) */
    bool  bpm_changed;
    bool  sync_changed;
    bool  clock_changed;              /* swing_pct / rate_q4              */
    bool  clock_reset;                /* RESET requested                  */
    bool  fader_layout_changed;
    bool  mic_changed;                /* mic_on toggled                   */
    bool  cam_changed;                /* cam_on toggled                   */
    bool  cam_source_changed;         /* camera source toggled front/back */

    /* internal: auto-repeat and deferred patch loading (all 5 slots) */
    int   repeat_frames;
    bool  slot_patch_changed[MENU_PATCH_SLOTS];
    int   slot_settle[MENU_PATCH_SLOTS];   /* frames until the change is reported */
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

/* Label of the current camera mode (Aus / Fader / Bild / Bild+Matrix). */
const char *options_menu_cam_label(const OptionsMenu *m);

/* Call once per frame: advances the settle timers of all slots (a slot is
 * only reported once its selection stopped changing -> no reload storm). */
void options_menu_tick(OptionsMenu *m);

/* Call after options_menu_tick(), repeatedly until it returns -1: returns
 * the next slot (0..4) whose selection changed and settled, and stores its
 * PatchList index in *out_patch_idx (-1 = Empty / unload). */
int options_menu_take_slot_request(OptionsMenu *m, int *out_patch_idx);

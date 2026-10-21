#pragma once
/* top_screen.h - page dispatcher for the pixel top screen
 *
 * Pages: Waveform (start), Patch 1..5 (only slots that hold a patch),
 * PD Log. ZL / ZR step backwards / forwards through Waveform and the
 * patch pages (wrapping, empty slots skipped). PD Log is NOT part of that
 * cycle: it is opened / closed with the ZL+ZR combo (see button_input.c).
 * The OPTIONS overlay replaces the page while g_options_open is set
 * (see options_menu.h).
 */

#include <3ds.h>
#include <stdint.h>
#include "options_menu.h"

typedef enum {
    PAGE_WAVEFORM = 0,  /* start screen: audio view (OPTIONS -> SpecAn) */
    PAGE_PATCH_0,
    PAGE_PATCH_1,
    PAGE_PATCH_2,
    PAGE_PATCH_3,
    PAGE_PATCH_4,
    PAGE_PD_LOG,        /* only via the ZL+ZR combo */
    PAGE_COUNT
} AppPage;

extern AppPage g_app_page;

/* Draw the active page (whole screen). */
void top_screen_draw(u16 *fb, const OptionsMenu *m, float bpm,
                     int dsp_pct, unsigned glitches);

/* Next / previous page of the ZL / ZR cycle (Waveform + filled patch
 * slots; never PAGE_PD_LOG). */
AppPage top_screen_next_page(const OptionsMenu *m);
AppPage top_screen_prev_page(const OptionsMenu *m);

/* Optional status/warning line on the Waveform page ("" or NULL = none). */
void top_screen_set_status(const char *msg);

/* Mikrofon-Indikator im Header ein/aus (kleines "MIC" in Akzentfarbe). */
void top_screen_set_mic(bool on);

/* Kamera-Indikator im Header: Modus 0 = aus (ausgeblendet), 1 = Fader,
 * 2 = Bild, 3 = Bild+Matrix. Zeigt "CAM" mit kurzem Moduszusatz. */
void top_screen_set_cam_mode(int mode);

/* Raw camera frame (CAM_FRAME_W x CAM_FRAME_H RGB565) for the PatchView image,
 * or NULL when no image should be drawn. main.c passes camera_frame() only when
 * the camera is active and the CAM mode shows an image; top_screen draws it only
 * on the camera patch's page. The pointer must stay valid until the next draw. */
void top_screen_set_cam_frame(const u16 *frame);

/* TEMPORARY camera capture diagnostics for the patch view (remove later):
 * whether the camera is active, the current frame serial and the last
 * brightness (0..1). Shown as a small line at the bottom of the frame. */
void top_screen_set_cam_debug(bool active, unsigned serial, float bright,
                              unsigned err, unsigned transfer, unsigned resync);

/* Patch slot shown on the top screen: 0..4 for PAGE_PATCH_0..4, -1 for
 * PAGE_WAVEFORM / PAGE_PD_LOG. */
int top_screen_get_active_slot(void);

/* PD output peak levels (0..32767) for the Waveform page. */
void top_screen_set_peak(int l, int r);

/* Newest PD output frames (oldest first) for the audio view on the Waveform
 * page. The arrays must stay valid until the next top_screen_draw(). */
void top_screen_set_scope(const int16_t *l, const int16_t *r, int n);

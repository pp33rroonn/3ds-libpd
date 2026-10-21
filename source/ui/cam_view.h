#pragma once
/* cam_view.h - Game-Boy-Camera style rendering of the built-in camera frame
 * onto the BOTTOM screen, plus an optional 16x16 brightness export to PD.
 *
 * The raw QQVGA RGB565 frame from camera.c is reduced to 4 grey levels (2-bit)
 * with an ordered Bayer 4x4 dither, giving the classic Game Boy Camera look.
 * Two palettes are selected automatically from the current theme:
 *   - themes 0 ("B/W") and 1 ("W/B") : pure greyscale (4 greys)
 *   - every other theme              : 4 shades of the theme accent colour
 *
 * All drawing is done with the same column-major, y-inverted bottom-screen
 * framebuffer layout used by ctrl_panel.c / draw.h.
 */

#include <3ds.h>   /* u16 */

/* Pixel size of the rendered camera rectangle on the bottom screen.
 * The source frame (CAM_FRAME_W x CAM_FRAME_H = 160x120) is drawn 1:1. */
#define CAM_VIEW_W 160
#define CAM_VIEW_H 120

/* Border thickness (px) drawn around the camera rectangle. */
#define CAM_VIEW_BORDER 2

/* Total footprint including the border, used by the control panel to reserve
 * vertical space for the faders below it. */
#define CAM_VIEW_TOTAL_W (CAM_VIEW_W + 2 * CAM_VIEW_BORDER)
#define CAM_VIEW_TOTAL_H (CAM_VIEW_H + 2 * CAM_VIEW_BORDER)

/* Render the given raw RGB565 frame (CAM_FRAME_W x CAM_FRAME_H) into the
 * bottom-screen framebuffer with the inner image top-left at (ox, oy); a
 * CAM_VIEW_BORDER-thick accent border is drawn just outside that. Safe to call
 * with frame == NULL (draws nothing). Clips to the bottom screen. */
void cam_view_draw(u16 *fb, const u16 *frame, int ox, int oy);

/* Scaled render of the camera image onto the TOP screen (400x240, column-major
 * y-inverted, see draw.h). The source frame is nearest-neighbour scaled to
 * dw x dh and drawn with the inner image top-left at (ox, oy), using the same
 * Game Boy dither + theme palette as cam_view_draw. A CAM_VIEW_BORDER-thick
 * border is drawn just outside the image; its colour follows the theme (light
 * grey for the greyscale themes 0/1, the accent colour otherwise). Safe with
 * frame == NULL (draws nothing). Used by the top-screen PatchView. */
void cam_view_draw_top_scaled(u16 *fb, const u16 *frame, int ox, int oy,
                              int dw, int dh);

/* Default size of the scaled top-screen camera image used in the PatchView
 * (160x120 reduced by 0.75). */
#define CAM_TOP_W 120
#define CAM_TOP_H 90

/* Reduce the frame to a 16x16 grid of brightness values (0..1) and write them
 * into the PD array "cam-matrix" via libpd_write_array(). If the array does not
 * exist (libpd_arraysize <= 0) this is a no-op, no error. Row-major, 256 floats
 * (or fewer if the array is smaller). */
void cam_view_matrix(const u16 *frame);

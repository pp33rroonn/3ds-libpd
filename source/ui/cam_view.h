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

/* Camera value modes (set each frame from the OPTIONS menu, see options_menu.*).
 * Shared Off-rule: for every value-mode index 0 = Off, 1..5 = X1 (fine) .. X5
 * (coarse).
 *   g_cam_pixel  : 0 Off, 1..5 pixelation grid. Each grid cell becomes one value
 *                  in "cam-matrix" and one visible block.
 *   g_cam_polygon: 0 Off, 1..5 low-poly triangle render. Each grid cell is split
 *                  into two triangles; each triangle is flat-filled with the
 *                  average luminance of the source pixels it covers and outputs
 *                  one value into "cam-matrix".
 *   g_cam_glitch : 0 Off, 1..5 intensity. Stacks ADDITIVELY on top of whatever
 *                  base render is active (polygon / pixel / raw), scrambling both
 *                  the exported values and the on-screen image.
 *
 * Precedence (base render + who writes "cam-matrix"):
 *   Polygon on            -> polygon is the base render AND writes cam-matrix.
 *   Polygon off, Pixel on -> pixel is the base render AND writes cam-matrix.
 *   both off              -> raw feed on screen; cam-matrix behaves as before
 *                            (default 16x16 brightness grid).
 *   Glitch, when on, is layered on top of whichever of the three is chosen. */
#define CAM_PIXEL_LEVELS 5   /* number of pixel grids   (X1..X5) */
#define CAM_POLY_LEVELS  5   /* number of polygon grids (X1..X5) */
extern int g_cam_pixel;
extern int g_cam_polygon;
extern int g_cam_glitch;

/* g_cam_smooth: 0 Off, 1..5 temporal smoothing strength of the BASE camera
 * image (X1 light .. X5 heavy). A per-pixel exponential moving average over
 * successive camera frames, applied to the source frame BEFORE the Game Boy
 * dither / pixel / polygon render. It only calms the frame-to-frame luma drift
 * that the 4-level dither amplifies into visible flicker - the crop, palette,
 * dither and overall look are unchanged. Off (0) returns the untouched live
 * frame, i.e. exactly today's base image (including its flicker), so nothing of
 * the current look is lost; the option is purely opt-in. Glitch is applied on
 * top of the (smoothed) base, so smoothing never removes Glitch. */
#define CAM_SMOOTH_LEVELS 5
extern int g_cam_smooth;

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

/* Reduce the frame to a g_cam_pixel grid of brightness values (0..1) and write
 * them into the PD array "cam-matrix" via libpd_write_array(), row-major. The
 * grid size follows the Cam Pixel mode (X1 = 32x24 = 768 cells .. X5 = 4x3 = 12)
 * and g_cam_glitch scrambles the values. If the array does not exist
 * (libpd_arraysize <= 0) this is a no-op. Size the array to at least gw*gh
 * (e.g. 768 for a full X1 grid); a shorter array just receives the first cells. */
void cam_view_matrix(const u16 *frame);

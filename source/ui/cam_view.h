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

/* g_cam_smooth: "Buffer FX" - a TECHNIQUE LADDER over the BASE camera image,
 * 9 positions (0..8). Unlike the old single EMA<->hysteresis axis (which only
 * traded ghosting for flicker), each stage uses a DIFFERENT mix of techniques,
 * so the cleanest stage can be sharp AND stable by attacking the noise source
 * directly instead of smearing time.
 *
 * Techniques, all applied to the source frame BEFORE the Game Boy dither /
 * pixel / polygon render, serial-gated once per NEW camera frame:
 *   - spatial 3x3 median noisegate (removes sensor speckle without softening);
 *   - unsharp-mask sharpen (restores edge crispness after the median);
 *   - exposure damping (global luma offset toward a slow baseline mean, +-24,
 *     cancels the AE "breathing"; does NOT touch AWB / white balance);
 *   - per-pixel temporal EMA over successive frames (CAM_EMA_WN/16 new-frame
 *     weight; higher weight = less smoothing);
 *   - dither hysteresis (sticky 4-level thresholds, CAM_DITH_MARGIN luma) in
 *     the render / matrix paths so tiny luma drift no longer flips a level.
 *
 * Ladder (index : techniques):
 *   0 Off = cleanest NEW default: median + unsharp + exposure-damp + strong
 *           hysteresis with only light EMA. Sharp and stable, no ghosting.
 *   1 X1  = median + unsharp (no exposure damping).
 *   2 X2  = median + unsharp, weaker sharpen.
 *   3 X3  = the PREVIOUS Off look (hysteresis-led, EMA 8/16, margin 40).
 *   4 X4  = older Off look (EMA 2/16, margin 28).
 *   5 X5  = EMA 4/16, margin 22.
 *   6 X6  = EMA 6/16, margin 16.
 *   7 X7  = EMA 12/16, margin 5 (almost raw).
 *   8 X8  = ZERO stabilization: the raw live frame passed straight through,
 *           i.e. EXACTLY today's base image (no median/unsharp/exposure/EMA/
 *           hysteresis) - byte-identical to the old X6.
 *
 * The crop, palette and dither look are otherwise unchanged. Glitch is applied
 * on top of the (stabilized) base, so Buffer FX stays combinable with Glitch.
 * CAM_SMOOTH_LEVELS is the highest index (8); there are CAM_SMOOTH_LEVELS+1 = 9
 * positions, matching CAM_SMOOTH_COUNT in options_menu.h. */
#define CAM_SMOOTH_LEVELS 8
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

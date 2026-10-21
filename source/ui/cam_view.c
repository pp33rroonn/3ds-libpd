/* cam_view.c - Game-Boy-Camera rendering of the camera frame (see cam_view.h) */

#include "cam_view.h"

#include "theme.h"
#include "draw.h"      /* RGB565, BOT_W */
#include "../camera/camera.h"

#include <z_libpd.h>   /* libpd_write_array / libpd_arraysize */

/* --- Dead-strip crop -----------------------------------------------------
 * The camera DMA on this modded New-3DS QQVGA pipeline regularly ends a few
 * rows short, leaving the bottom of the frame as unwritten memory (the frozen /
 * garbage strip). The "frame done" interrupt fires even on a short transfer, so
 * those rows can NEVER be filled from the capture side. Instead we simply never
 * sample them: only the top CAM_VISIBLE_H rows are drawn, stretched to fill the
 * whole display box. The box on screen stays exactly the same size.
 * Tune CAM_CROP_BOTTOM if a strip still shows (increase) or too much image is
 * cut off (decrease). */
#define CAM_CROP_BOTTOM 26
#define CAM_VISIBLE_H   (CAM_FRAME_H - CAM_CROP_BOTTOM)   /* = 94 of 120 rows */

/* --- Camera value modes --------------------------------------------------
 * g_cam_pixel 1..5 selects the pixelation grid (fine -> coarse); g_cam_glitch
 * 0..5 layers a scrambling effect on top. Defaults match options_menu_init(). */
int g_cam_pixel  = 3;
int g_cam_glitch = 0;

/* Grid dimensions per pixel level (4:3, matches the mockup). X1 is the finest.
 * Cell counts: 768, 300, 108, 48, 12. */
static const int CAM_GRID_W[CAM_PIXEL_LEVELS] = { 32, 20, 12, 8, 4 };
static const int CAM_GRID_H[CAM_PIXEL_LEVELS] = { 24, 15,  9, 6, 3 };

/* Resolve the active grid dimensions from g_cam_pixel (clamped 1..5). */
static void cam_grid_dims(int *gw, int *gh)
{
    int lvl = g_cam_pixel;
    if (lvl < 1) lvl = 1;
    if (lvl > CAM_PIXEL_LEVELS) lvl = CAM_PIXEL_LEVELS;
    *gw = CAM_GRID_W[lvl - 1];
    *gh = CAM_GRID_H[lvl - 1];
}

/* Small LCG PRNG for the glitch effect; reseeded from the system tick each
 * frame so the scramble changes continuously. */
static unsigned s_glitch_rng = 0x1234567u;
static inline unsigned glitch_rand(void)
{
    s_glitch_rng = s_glitch_rng * 1664525u + 1013904223u;
    return s_glitch_rng;
}

/* Bottom screen is 320x240, column-major, y-inverted (same as ctrl_panel.c):
 *   fb[sx * 240 + (239 - sy)] = colour
 * Clip against the full bottom screen to avoid framebuffer overruns. */
#define BOT_H 240

static inline void set_px(u16 *fb, int sx, int sy, u16 col)
{
    if ((unsigned)sx < (unsigned)BOT_W && (unsigned)sy < (unsigned)BOT_H)
        fb[sx * BOT_H + (BOT_H - 1 - sy)] = col;
}

/* Ordered Bayer 4x4 threshold matrix (values 0..15). */
static const int BAYER4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

/* Decode an RGB565 pixel to 8-bit components. */
static inline void rgb565_to_rgb8(u16 c, int *r, int *g, int *b)
{
    *r = (((c >> 11) & 0x1F) * 255) / 31;
    *g = (((c >>  5) & 0x3F) * 255) / 63;
    *b = ( (c        & 0x1F) * 255) / 31;
}

/* Build the 4-level palette for the current theme into pal[0..3]
 * (darkest -> brightest). Themes 0/1 use pure greys; all others use 4 shades
 * of the theme accent colour. */
static void build_palette(u16 pal[4])
{
    if (g_theme_idx == 0 || g_theme_idx == 1) {
        pal[0] = RGB565( 15,  15,  15);
        pal[1] = RGB565( 90,  90,  90);
        pal[2] = RGB565(170, 170, 170);
        pal[3] = RGB565(245, 245, 245);
        return;
    }

    int ar, ag, ab;
    rgb565_to_rgb8(TH.accent, &ar, &ag, &ab);

    /* Four brightness steps of the accent colour. Use a non-zero floor so the
     * darkest level is not a pure black hole. */
    static const int num[4] = { 22, 48, 74, 100 };   /* percent */
    for (int i = 0; i < 4; i++) {
        int r = (ar * num[i]) / 100;
        int g = (ag * num[i]) / 100;
        int b = (ab * num[i]) / 100;
        pal[i] = RGB565(r, g, b);
    }
}

void cam_view_draw(u16 *fb, const u16 *frame, int ox, int oy)
{
    if (!fb || !frame) return;

    u16 pal[4];
    build_palette(pal);

    /* Border (accent) drawn just outside the image rectangle. */
    u16 border = TH.accent;
    for (int t = 1; t <= CAM_VIEW_BORDER; t++) {
        int x0 = ox - t, y0 = oy - t;
        int x1 = ox + CAM_VIEW_W - 1 + t;
        int y1 = oy + CAM_VIEW_H - 1 + t;
        for (int x = x0; x <= x1; x++) { set_px(fb, x, y0, border); set_px(fb, x, y1, border); }
        for (int y = y0; y <= y1; y++) { set_px(fb, x0, y, border); set_px(fb, x1, y, border); }
    }

    /* Image: 160x120, 1:1. Ordered dither to 4 levels. */
    for (int y = 0; y < CAM_VIEW_H; y++) {
        const u16 *srow = frame + (size_t)y * CAM_FRAME_W;
        int sy = oy + y;
        for (int x = 0; x < CAM_VIEW_W; x++) {
            int r, g, b;
            rgb565_to_rgb8(srow[x], &r, &g, &b);
            int luma = (299 * r + 587 * g + 114 * b) / 1000;   /* 0..255 */

            /* Map 0..255 into 0..3 with a Bayer threshold between levels. */
            int v   = luma * 3;                 /* 0..765            */
            int base = v / 255;                 /* 0..3              */
            int frac = v - base * 255;          /* 0..254            */
            int thr  = (BAYER4[y & 3][x & 3] * 255) / 16 + 8;
            int lvl  = base + (frac > thr ? 1 : 0);
            if (lvl < 0) lvl = 0;
            if (lvl > 3) lvl = 3;

            set_px(fb, ox + x, sy, pal[lvl]);
        }
    }
}

/* Top screen is 400x240, column-major, y-inverted (see draw.h):
 *   fb[sx * TOP_H + (TOP_H - 1 - sy)] = colour */
static inline void set_px_top(u16 *fb, int sx, int sy, u16 col)
{
    if ((unsigned)sx < (unsigned)TOP_W && (unsigned)sy < (unsigned)TOP_H)
        fb[sx * TOP_H + (TOP_H - 1 - sy)] = col;
}

void cam_view_draw_top_scaled(u16 *fb, const u16 *frame, int ox, int oy,
                              int dw, int dh)
{
    if (!fb || !frame || dw <= 0 || dh <= 0) return;

    u16 pal[4];
    build_palette(pal);

    /* Border follows the theme: light grey for the greyscale themes, else the
     * accent colour. */
    u16 border = (g_theme_idx == 0 || g_theme_idx == 1)
               ? RGB565(220, 220, 220) : TH.accent;
    for (int t = 1; t <= CAM_VIEW_BORDER; t++) {
        int x0 = ox - t, y0 = oy - t;
        int x1 = ox + dw - 1 + t;
        int y1 = oy + dh - 1 + t;
        for (int x = x0; x <= x1; x++) { set_px_top(fb, x, y0, border); set_px_top(fb, x, y1, border); }
        for (int y = y0; y <= y1; y++) { set_px_top(fb, x0, y, border); set_px_top(fb, x1, y, border); }
    }

    /* Pixelation grid (g_cam_pixel) + optional glitch (g_cam_glitch), scaled to
     * dw x dh with the Bayer dither. Only the top CAM_VISIBLE_H source rows are
     * sampled (the dead bottom strip is cropped), so the visible rows fill the
     * whole box and the unwritten DMA rows can never appear.
     *
     * Each display pixel is mapped to a grid cell and the source is sampled at
     * that cell's centre -> blocky pixelation. The glitch layer shears whole
     * grid rows sideways and occasionally replaces a cell with a random level,
     * block-coherent via per-cell hashes so it reads as data corruption. */
    int gw, gh;
    cam_grid_dims(&gw, &gh);

    int gl = g_cam_glitch;
    if (gl < 0) gl = 0;
    if (gl > 5) gl = 5;
    if (gl > 0) s_glitch_rng ^= (unsigned)svcGetSystemTick();   /* reseed/frame */
    int      shear_max = gl * 2;                 /* cells of horizontal shear */
    unsigned repl_prob = (unsigned)gl * 10;      /* /256 random-replace chance */

    const int cell_w = CAM_FRAME_W  / gw;        /* source px per cell */
    const int cell_h = CAM_VISIBLE_H / gh;

    for (int y = 0; y < dh; y++) {
        int cellY = (y * gh) / dh;               /* 0..gh-1 */
        int sy = oy + y;

        int shear = 0;
        if (shear_max > 0) {
            unsigned h = (unsigned)cellY * 2654435761u ^ s_glitch_rng;
            shear = (int)(h % (unsigned)(2 * shear_max + 1)) - shear_max;
        }

        int srcy = cellY * cell_h + cell_h / 2;
        if (srcy >= CAM_VISIBLE_H) srcy = CAM_VISIBLE_H - 1;
        const u16 *srow = frame + (size_t)srcy * CAM_FRAME_W;

        for (int x = 0; x < dw; x++) {
            int cellX = (x * gw) / dw;            /* 0..gw-1 */
            int scellX = cellX + shear;
            if (scellX < 0) scellX = 0;
            if (scellX >= gw) scellX = gw - 1;

            int srcx = scellX * cell_w + cell_w / 2;
            if (srcx >= CAM_FRAME_W) srcx = CAM_FRAME_W - 1;

            int r, g, b;
            rgb565_to_rgb8(srow[srcx], &r, &g, &b);
            int luma = (299 * r + 587 * g + 114 * b) / 1000;   /* 0..255 */

            int v    = luma * 3;
            int base = v / 255;
            int frac = v - base * 255;
            int thr  = (BAYER4[y & 3][x & 3] * 255) / 16 + 8;
            int lvl  = base + (frac > thr ? 1 : 0);
            if (lvl < 0) lvl = 0;
            if (lvl > 3) lvl = 3;

            if (repl_prob > 0) {
                unsigned hc = ((unsigned)cellX * 73856093u)
                            ^ ((unsigned)cellY * 19349663u) ^ s_glitch_rng;
                if ((hc & 0xFF) < repl_prob) lvl = (int)((hc >> 8) & 3);
            }

            set_px_top(fb, ox + x, sy, pal[lvl]);
        }
    }
}

void cam_view_matrix(const u16 *frame)
{
    if (!frame) return;

    int sz = libpd_arraysize("cam-matrix");
    if (sz <= 0) return;                 /* array absent -> no-op, no error */

    /* Grid follows g_cam_pixel: gw x gh average-brightness cells (0..1), row-
     * major. X1 = 32x24 = 768 cells, so the static buffer holds up to 1024.
     * Only the cropped visible rows are averaged, matching the on-screen image.
     * NOTE: size the PD "cam-matrix" array to at least gw*gh (e.g. 768 for a
     * full X1 grid); a shorter array just receives the first cells. */
    int gw, gh;
    cam_grid_dims(&gw, &gh);

    static float out[1024];
    const int cells = gw * gh;           /* <= 768 */
    const int bw = CAM_FRAME_W  / gw;
    const int bh = CAM_VISIBLE_H / gh;

    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            unsigned long sum = 0, n = 0;
            int y0 = gy * bh, x0 = gx * bw;
            for (int yy = 0; yy < bh; yy++) {
                const u16 *srow = frame + (size_t)(y0 + yy) * CAM_FRAME_W;
                for (int xx = 0; xx < bw; xx++) {
                    int r, g, b;
                    rgb565_to_rgb8(srow[x0 + xx], &r, &g, &b);
                    sum += (unsigned)((299 * r + 587 * g + 114 * b) / 1000);
                    n++;
                }
            }
            out[gy * gw + gx] = n ? (float)sum / ((float)n * 255.0f) : 0.0f;
        }
    }

    /* Glitch layer: scramble the exported values so combining pixel + glitch
     * yields many unpredictable / generative values. */
    int gl = g_cam_glitch;
    if (gl < 0) gl = 0;
    if (gl > 5) gl = 5;
    if (gl > 0) {
        s_glitch_rng ^= (unsigned)svcGetSystemTick();
        float    amt       = (float)gl / 5.0f;    /* 0..1 noise amount        */
        unsigned repl_prob = (unsigned)gl * 12;   /* /256 full-random replace */
        for (int i = 0; i < cells; i++) {
            unsigned h = glitch_rand();
            float v = out[i];
            if ((h & 0xFF) < repl_prob) {
                v = (float)((h >> 8) & 0xFFFF) / 65535.0f;             /* random */
            } else {
                float noise = ((float)((h >> 8) & 0x3FF) / 1023.0f - 0.5f) * amt;
                v += noise;
            }
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            out[i] = v;
        }
    }

    int n = cells < sz ? cells : sz;
    libpd_write_array("cam-matrix", 0, out, n);
}

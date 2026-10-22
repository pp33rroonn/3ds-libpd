/* cam_view.c - Game-Boy-Camera rendering of the camera frame (see cam_view.h) */

#include "cam_view.h"

#include "theme.h"
#include "draw.h"      /* RGB565, BOT_W */
#include "../camera/camera.h"

#include <z_libpd.h>   /* libpd_write_array / libpd_arraysize */
#include <string.h>    /* memcpy (temporal smoothing buffer) */

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
 * Shared Off-rule (see cam_view.h): index 0 = Off, 1..5 = X1 fine .. X5 coarse.
 *   g_cam_pixel   0 Off / 1..5 pixelation grid (base render + cam-matrix).
 *   g_cam_polygon 0 Off / 1..5 low-poly triangle render (base render + matrix).
 *   g_cam_glitch  0 Off / 1..5 scrambling layer stacked on the base render.
 *
 * Precedence: Polygon (when on) is the base render and writes cam-matrix; else
 * Pixel (when on) is the base render and writes cam-matrix; else the raw feed is
 * shown and cam-matrix falls back to a default 16x16 brightness grid. Glitch,
 * when on, stacks additively on whichever base is active.
 * Defaults match options_menu_init() (all Off). */
int g_cam_pixel   = 0;
int g_cam_polygon = 0;
int g_cam_glitch  = 0;
int g_cam_smooth  = 0;   /* Buffer FX technique ladder: 0 Off = cleanest/strongest
                          * (Luma-Lock: luma-normalize + static-scene freeze +
                          * light unsharp), X1 = 3-frame temporal median, X2..X9
                          * trade spatial cleanup for temporal/hysteresis,
                          * X10 = raw today's look */

/* Grid dimensions per level (4:3, matches the mockup). X1 is the finest.
 * Cell counts: 768, 300, 108, 48, 12. Polygon reuses the same grids and splits
 * each cell into two triangles, so its value count is twice the cell count. */
static const int CAM_GRID_W[CAM_PIXEL_LEVELS] = { 32, 20, 12, 8, 4 };
static const int CAM_GRID_H[CAM_PIXEL_LEVELS] = { 24, 15,  9, 6, 3 };

/* Largest polygon value count: finest grid (32x24) x 2 triangles = 1536. */
#define CAM_POLY_MAX_TRI (32 * 24 * 2)

/* Default grid used for cam-matrix when no value-mode is active (both Off). */
#define CAM_DEFAULT_GRID 16

static inline int clampi_local(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Resolve grid dimensions for a 1..5 level (clamped). Shared by pixel + polygon.
 * (grid_avg / poly_tri_luma are defined below build_palette, after the
 * rgb565_to_rgb8 helper they rely on.) */
static void grid_dims(int lvl, int *gw, int *gh)
{
    lvl = clampi_local(lvl, 1, CAM_PIXEL_LEVELS);
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

/* --- Buffer FX: base-image stabilization (g_cam_smooth technique ladder) ----
 * The ladder (see cam_view.h) mixes a luma-normalize + static-scene freeze
 * ("Luma-Lock", Off only), a 3-frame per-pixel temporal MEDIAN (X1), spatial
 * pre-processing (median noisegate, unsharp sharpen, exposure damping - all in
 * process_spatial below) and a per-pixel temporal exponential moving average
 * over successive CAMERA frames (X2..X9). Everything is applied to the source
 * frame before any dither / pixel / polygon render, so the on-screen image and
 * the exported matrix stay consistent.
 *
 * Off (lvl 0) routes through luma_lock_frame(): the strongest/cleanest stage, it
 * normalizes overall luma (neutralizing the auto-exposure "br" stepping that
 * flickers a still scene) and freezes a held copy of the visible frame while the
 * scene is static, updating live only on real motion - zero flicker when still.
 * X1 (lvl 1) routes through temporal_median_frame(): rejects single-frame
 * flips/speckle with no ghosting. X2..X9 use the EMA path; the EMA new-frame
 * weight is CAM_EMA_WN[lvl]/16 (higher = less smoothing); at X10 (weight
 * 16/16 = pure new frame) the raw live frame is returned untouched = exactly
 * today's image, with NO spatial work either. The
 * work runs once per NEW camera frame (serial change), so repeated UI redraws of
 * the same frame never over-process and the exported matrix matches the
 * on-screen image. Only the visible (cropped) rows are processed; the dead
 * bottom strip is copied through so the buffer stays a valid full frame. */
static u16      s_sm_frame[CAM_FRAME_W * CAM_FRAME_H];
static int      s_sm_init   = 0;
static unsigned s_sm_serial = 0;

/* --- Buffer FX technique-ladder tables (index g_cam_smooth 0..10) ----------
 * Parallel tables, one column per ladder stage (see cam_view.h for the full
 * description of each stage). Index 10 (X10) = raw today's image (EMA 16/16, no
 * margin, no spatial work) and is reproduced byte-for-byte. Indices 1..10 are
 * the old 0..9 shifted up by one; index 0 is the NEW Off (Luma-Lock), whose
 * stabilization is luma-normalize + static-scene freeze (luma_lock_frame), not
 * EMA. For Off, CAM_UNSHARP_ST[0] drives the light unsharp applied after the
 * normalize/freeze; CAM_EXPDAMP_EN[0] is 0 (Luma-Lock does its own luma
 * normalization), CAM_MEDIAN_EN[0] is 0, and CAM_EMA_WN[0] is unused by the
 * luma-lock path (kept < 16 so it can never hit the raw early-return).
 * Index 1 (X1) is the 3-frame temporal median stage: it drives the per-frame
 * pre-process that feeds the median (exposure-damp ON + light unsharp, spatial
 * median OFF - the temporal median handles speckle).
 *   CAM_EMA_WN     : temporal EMA new-frame weight /16 (16 = pure new = no EMA).
 *   CAM_DITH_MARGIN: dither hysteresis luma margin (0 = no hysteresis); defined
 *                    further down next to the hysteresis code.
 *   CAM_MEDIAN_EN  : 1 = run the spatial 3x3 median noisegate.
 *   CAM_UNSHARP_ST : unsharp-mask strength (0 = skip, true no-op).
 *   CAM_EXPDAMP_EN : 1 = run exposure damping (global luma offset). */
static const int CAM_EMA_WN[CAM_SMOOTH_LEVELS + 1]     = {  8,  8, 12, 10, 10, 8, 2, 4, 6, 12, 16 };
static const int CAM_MEDIAN_EN[CAM_SMOOTH_LEVELS + 1]  = {  0,  0,  1,  1,  1, 0, 0, 0, 0,  0,  0 };
static const int CAM_UNSHARP_ST[CAM_SMOOTH_LEVELS + 1] = {  2,  2,  3,  3,  2, 0, 0, 0, 0,  0,  0 };
static const int CAM_EXPDAMP_EN[CAM_SMOOTH_LEVELS + 1] = {  0,  1,  1,  0,  0, 0, 0, 0, 0,  0,  0 };

/* Spatial pre-processing scratch (luma planes over the visible region) and the
 * processed full-frame buffer handed to the EMA blend. Also the slow exposure
 * baseline tracker (persistent EMA of the visible mean luma). */
static unsigned char s_lum_a[CAM_FRAME_W * CAM_VISIBLE_H];
static unsigned char s_lum_b[CAM_FRAME_W * CAM_VISIBLE_H];
static u16           s_proc[CAM_FRAME_W * CAM_FRAME_H];
static float         s_exp_base = 0.0f;
static int           s_exp_init = 0;

/* 3x3 median noisegate over the visible region (replicate-border). Removes
 * isolated sensor speckle without the softening a box blur would cause. */
static void median3x3(const unsigned char *src, unsigned char *dst)
{
    const int W = CAM_FRAME_W, H = CAM_VISIBLE_H;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            unsigned char win[9];
            int k = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy; if (yy < 0) yy = 0; if (yy >= H) yy = H - 1;
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx; if (xx < 0) xx = 0; if (xx >= W) xx = W - 1;
                    win[k++] = src[yy * W + xx];
                }
            }
            for (int i = 1; i < 9; i++) {        /* insertion sort */
                unsigned char v = win[i]; int j = i - 1;
                while (j >= 0 && win[j] > v) { win[j + 1] = win[j]; j--; }
                win[j + 1] = v;
            }
            dst[y * W + x] = win[4];             /* median */
        }
    }
}

/* Unsharp-mask sharpen over the visible region (replicate-border). blurred is a
 * 3x3 box average; sharp = orig + strength*(orig-blurred)/4 (integer scale 4, so
 * strength 3 = 0.75 gain - clear but not harsh). strength 0 is a true no-op and
 * is never called (guarded by the caller). */
static void unsharp(const unsigned char *src, unsigned char *dst, int strength)
{
    const int W = CAM_FRAME_W, H = CAM_VISIBLE_H;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int sum = 0;
            for (int dy = -1; dy <= 1; dy++) {
                int yy = y + dy; if (yy < 0) yy = 0; if (yy >= H) yy = H - 1;
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx; if (xx < 0) xx = 0; if (xx >= W) xx = W - 1;
                    sum += src[yy * W + xx];
                }
            }
            int blurred = sum / 9;
            int orig    = src[y * W + x];
            int sharp   = orig + (strength * (orig - blurred)) / 4;
            if (sharp < 0)   sharp = 0;
            if (sharp > 255) sharp = 255;
            dst[y * W + x] = (unsigned char)sharp;
        }
    }
}

/* Spatial pipeline for a stage with any of exposure-damp / median / unsharp
 * enabled: exposure-damp -> median -> unsharp, on a single luma plane, then
 * written back as a greyscale RGB565 frame (chroma dropped). The downstream
 * dither / matrix paths are luma-only, so dropping chroma on these cleaner
 * stages is visually identical while letting the spatial filters work on one
 * intensity plane. Returns s_proc with the visible rows filled. */
static const u16 *process_spatial(const u16 *frame, int lvl)
{
    const int    W = CAM_FRAME_W, H = CAM_VISIBLE_H;
    const size_t vis = (size_t)W * H;

    /* Exposure damping: shift the whole plane toward a slow baseline mean so the
     * AE "breathing" cancels, clamped +-24 so real light changes still pass. */
    int off = 0;
    if (CAM_EXPDAMP_EN[lvl]) {
        unsigned long sum = 0;
        for (size_t i = 0; i < vis; i++) {
            int r, g, b;
            rgb565_to_rgb8(frame[i], &r, &g, &b);
            sum += (unsigned)((299 * r + 587 * g + 114 * b) / 1000);
        }
        float mean = (float)sum / (float)vis;
        if (!s_exp_init) { s_exp_base = mean; s_exp_init = 1; }
        else             { s_exp_base += (mean - s_exp_base) * (1.0f / 64.0f); }
        off = (int)(s_exp_base - mean);
        if (off < -24) off = -24;
        if (off >  24) off =  24;
    }

    /* Build the luma plane (+ exposure offset). */
    for (size_t i = 0; i < vis; i++) {
        int r, g, b;
        rgb565_to_rgb8(frame[i], &r, &g, &b);
        int l = (299 * r + 587 * g + 114 * b) / 1000 + off;
        if (l < 0)   l = 0;
        if (l > 255) l = 255;
        s_lum_a[i] = (unsigned char)l;
    }

    unsigned char *cur = s_lum_a, *nxt = s_lum_b;
    if (CAM_MEDIAN_EN[lvl]) {
        median3x3(cur, nxt);
        unsigned char *t = cur; cur = nxt; nxt = t;
    }
    if (CAM_UNSHARP_ST[lvl] > 0) {
        unsharp(cur, nxt, CAM_UNSHARP_ST[lvl]);
        unsigned char *t = cur; cur = nxt; nxt = t;
    }

    /* Luma -> grey RGB565. */
    for (size_t i = 0; i < vis; i++) {
        int l  = cur[i];
        int r5 = l * 31 / 255, g6 = l * 63 / 255, b5 = l * 31 / 255;
        s_proc[i] = (u16)((r5 << 11) | (g6 << 5) | b5);
    }
    return s_proc;
}

/* --- Buffer FX X1: 3-frame temporal median ---------------------------------
 * Rather than time-averaging (EMA, which ghosts) the X1 stage keeps the last 3
 * PROCESSED camera frames of the visible region and outputs the per-pixel
 * MEDIAN. On a still camera a single-
 * frame dither-zone flip or sensor speckle is never the median of three, so it
 * is rejected outright - stable AND sharp, with no ghosting. Each incoming frame
 * is first run through process_spatial(frame, 1): exposure lock + light unsharp
 * (AWB untouched; spatial median off, the temporal median does that job). The
 * ring advances only when the camera serial changes; until it holds 3 frames the
 * newest processed frame is passed straight through. Only the visible rows are
 * touched; the dead bottom strip is copied through from the live frame.
 *
 * Medians are taken on the grey RGB565 values produced by process_spatial. That
 * mapping is monotonic in luma, so the median of three grey u16 values is the
 * grey of the median luma - correct and cheaper than a separate luma plane. */
static u16      s_tm_buf[3][CAM_FRAME_W * CAM_VISIBLE_H];
static int      s_tm_count  = 0;   /* processed frames held (0..3)       */
static int      s_tm_head   = 0;   /* next ring slot to overwrite        */
static int      s_tm_init   = 0;
static unsigned s_tm_serial = 0;

static inline u16 med3_u16(u16 a, u16 b, u16 c)
{
    if (a > b) { u16 t = a; a = b; b = t; }
    if (b > c) { u16 t = b; b = c; c = t; }
    if (a > b) { u16 t = a; a = b; b = t; }
    return b;
}

static const u16 *temporal_median_frame(const u16 *frame)
{
    const size_t vis = (size_t)CAM_VISIBLE_H * CAM_FRAME_W;
    unsigned serial  = camera_frame_serial();

    /* Dead bottom strip always passes through unchanged from the live frame. */
    memcpy(s_sm_frame, frame, sizeof(s_sm_frame));

    if (!s_tm_init) {
        const u16 *nf = process_spatial(frame, 1);
        memcpy(s_tm_buf[0], nf, vis * sizeof(u16));
        s_tm_count  = 1;
        s_tm_head   = 1;
        s_tm_init   = 1;
        s_tm_serial = serial;
        memcpy(s_sm_frame, nf, vis * sizeof(u16));
        return s_sm_frame;
    }

    /* Push a freshly processed frame only when the camera produced a new one. */
    if (serial != s_tm_serial) {
        s_tm_serial = serial;
        const u16 *nf = process_spatial(frame, 1);
        memcpy(s_tm_buf[s_tm_head], nf, vis * sizeof(u16));
        s_tm_head = (s_tm_head + 1) % 3;
        if (s_tm_count < 3) s_tm_count++;
    }

    if (s_tm_count < 3) {                      /* ring not full: pass newest */
        int newest = (s_tm_head + 3 - 1) % 3;
        memcpy(s_sm_frame, s_tm_buf[newest], vis * sizeof(u16));
        return s_sm_frame;
    }

    for (size_t i = 0; i < vis; i++)
        s_sm_frame[i] = med3_u16(s_tm_buf[0][i], s_tm_buf[1][i], s_tm_buf[2][i]);
    return s_sm_frame;
}

/* --- Buffer FX Off: Luma-Lock (luma-normalize + static-scene freeze) --------
 * The strongest/cleanest Off technique, built to kill the exact flicker seen on
 * a STILL camera: the sensor's auto-EXPOSURE steps the whole-frame brightness a
 * few percent between frames (debug overlay shows br 35%->38%), which pushes
 * large regions across the 4-level dither thresholds together into visible
 * rectangular "zone-flip" blocks. A per-pixel median/EMA cannot reject a
 * SUSTAINED brightness step, so Luma-Lock attacks the cause in two stages,
 * applied only to the visible rows (dead bottom strip copied through):
 *
 *  1. LUMA NORMALIZE: track a slow reference mean luma of the visible region and
 *     rescale each frame's luma by gain = ref/mean (clamped 0.75..1.33 so real
 *     light changes still pass and gain can't run away). This flattens the
 *     auto-exposure "br" stepping before any dither. AWB/white-balance untouched.
 *  2. STATIC-SCENE FREEZE: hold a locked copy of the normalized visible frame =
 *     what gets rendered. Each new frame's global mean-abs luma diff vs the lock
 *     drives a hysteresis: while frozen, stay frozen until diff reaches
 *     LUMA_LOCK_RELEASE (real motion) then live-follow; while moving, keep
 *     following until diff drops below LUMA_LOCK_FREEZE then re-freeze. The
 *     freeze threshold sits above the measured still-scene noise (~12-15) and
 *     the release below real motion (~40+), so a static scene shows a truly
 *     still image (zero flicker) and motion updates live.
 *  3. LIGHT UNSHARP (CAM_UNSHARP_ST[0]) keeps edges crisp after normalize.
 *
 * Serial-gated like the other stages: the heavy work runs once per NEW camera
 * frame; repeated UI redraws of the same frame just re-emit the locked image, so
 * the on-screen image and the exported matrix stay identical. Output is the grey
 * of the (possibly frozen) normalized luma, matching the other spatial stages. */
#define LUMA_GAIN_MIN     0.75f   /* clamp on per-frame luma rescale gain  */
#define LUMA_GAIN_MAX     1.33f
#define LUMA_LOCK_FREEZE  20      /* mean-abs luma diff below this -> re-freeze */
#define LUMA_LOCK_RELEASE 28      /* mean-abs luma diff at/above this -> live follow */

static unsigned char s_ll_lock[CAM_FRAME_W * CAM_VISIBLE_H]; /* locked normalized luma */
static float    s_ll_ref    = 0.0f;   /* slow reference mean luma   */
static int      s_ll_init   = 0;
static int      s_ll_moving = 0;      /* hysteresis state: 1 = following motion */
static unsigned s_ll_serial = 0;

/* Emit a luma plane as grey RGB565 into the visible region of s_sm_frame. */
static void ll_emit(const unsigned char *lum, size_t vis)
{
    for (size_t i = 0; i < vis; i++) {
        int l  = lum[i];
        int r5 = l * 31 / 255, g6 = l * 63 / 255, b5 = l * 31 / 255;
        s_sm_frame[i] = (u16)((r5 << 11) | (g6 << 5) | b5);
    }
}

static const u16 *luma_lock_frame(const u16 *frame)
{
    const int    W = CAM_FRAME_W, H = CAM_VISIBLE_H;
    const size_t vis = (size_t)W * H;
    unsigned serial = camera_frame_serial();

    /* Dead bottom strip always passes through unchanged from the live frame. */
    memcpy(s_sm_frame, frame, sizeof(s_sm_frame));

    /* Same frame as last time: just re-emit the held image (no re-processing). */
    if (s_ll_init && serial == s_ll_serial) {
        ll_emit(s_ll_lock, vis);
        return s_sm_frame;
    }
    s_ll_serial = serial;

    /* Mean luma of the visible region. */
    unsigned long sum = 0;
    for (size_t i = 0; i < vis; i++) {
        int r, g, b;
        rgb565_to_rgb8(frame[i], &r, &g, &b);
        sum += (unsigned)((299 * r + 587 * g + 114 * b) / 1000);
    }
    float mean = (float)sum / (float)vis;
    if (mean < 1.0f) mean = 1.0f;
    if (!s_ll_init) s_ll_ref = mean;

    /* Per-frame luma-normalize gain toward the slow reference. */
    float gain = s_ll_ref / mean;
    if (gain < LUMA_GAIN_MIN) gain = LUMA_GAIN_MIN;
    if (gain > LUMA_GAIN_MAX) gain = LUMA_GAIN_MAX;

    for (size_t i = 0; i < vis; i++) {
        int r, g, b;
        rgb565_to_rgb8(frame[i], &r, &g, &b);
        int l  = (299 * r + 587 * g + 114 * b) / 1000;
        int nl = (int)(l * gain + 0.5f);
        if (nl < 0)   nl = 0;
        if (nl > 255) nl = 255;
        s_lum_a[i] = (unsigned char)nl;
    }

    /* Light unsharp after normalize to keep edges crisp. */
    unsigned char *cur = s_lum_a;
    if (CAM_UNSHARP_ST[0] > 0) {
        unsharp(s_lum_a, s_lum_b, CAM_UNSHARP_ST[0]);
        cur = s_lum_b;
    }

    /* First frame: initialize the lock + reference and pass it through. */
    if (!s_ll_init) {
        memcpy(s_ll_lock, cur, vis);
        s_ll_init   = 1;
        s_ll_moving = 0;
        ll_emit(s_ll_lock, vis);
        return s_sm_frame;
    }

    /* Global mean-abs luma diff of the new normalized frame vs the lock. */
    unsigned long dsum = 0;
    for (size_t i = 0; i < vis; i++) {
        int d = (int)cur[i] - (int)s_ll_lock[i];
        if (d < 0) d = -d;
        dsum += (unsigned)d;
    }
    float diff = (float)dsum / (float)vis;

    /* Hysteresis: freeze while still, live-follow while moving. */
    if (s_ll_moving) {
        memcpy(s_ll_lock, cur, vis);               /* live follow */
        if (diff < LUMA_LOCK_FREEZE) s_ll_moving = 0;
    } else {
        if (diff >= LUMA_LOCK_RELEASE) {           /* real motion begins */
            s_ll_moving = 1;
            memcpy(s_ll_lock, cur, vis);
        }
        /* else hold the locked frame unchanged -> zero flicker */
    }

    /* Slow-adapt the reference mean so sustained light changes are tracked. */
    s_ll_ref += (mean - s_ll_ref) * (1.0f / 16.0f);

    ll_emit(s_ll_lock, vis);
    return s_sm_frame;
}

static const u16 *smoothed_frame(const u16 *frame)
{
    int lvl = g_cam_smooth;
    if (lvl < 0) lvl = 0;
    if (lvl > CAM_SMOOTH_LEVELS) lvl = CAM_SMOOTH_LEVELS;   /* 0..10 */

    if (lvl == 0) {                           /* Off: Luma-Lock */
        s_tm_init = 0;                        /* other stabilizers' state is stale */
        s_sm_init = 0;
        return luma_lock_frame(frame);
    }

    /* Any non-Off level invalidates the Luma-Lock state so a later return to Off
     * rebuilds it (reference + lock) from fresh frames rather than stale ones. */
    s_ll_init = 0;

    if (lvl == 1) {                           /* X1: 3-frame temporal median */
        s_sm_init = 0;
        return temporal_median_frame(frame);
    }

    /* Any level past X1 invalidates the temporal-median ring likewise. */
    s_tm_init = 0;

    int wn = CAM_EMA_WN[lvl];                 /* new-frame weight 0..16 */
    if (wn >= 16) {                           /* X10: raw today's look */
        s_sm_init  = 0;
        s_exp_init = 0;
        return frame;
    }

    int spatial = CAM_MEDIAN_EN[lvl] || CAM_UNSHARP_ST[lvl] || CAM_EXPDAMP_EN[lvl];
    if (!CAM_EXPDAMP_EN[lvl]) s_exp_init = 0; /* baseline resets when damping off */

    unsigned serial = camera_frame_serial();
    size_t   vis    = (size_t)CAM_VISIBLE_H * CAM_FRAME_W;

    if (!s_sm_init) {
        memcpy(s_sm_frame, frame, sizeof(s_sm_frame));
        if (spatial)
            memcpy(s_sm_frame, process_spatial(frame, lvl), vis * sizeof(u16));
        s_sm_init   = 1;
        s_sm_serial = serial;
        return s_sm_frame;
    }

    if (serial != s_sm_serial) {
        s_sm_serial = serial;
        const u16 *nf = spatial ? process_spatial(frame, lvl) : frame;
        int wo = 16 - wn;                     /* old-frame weight */
        for (size_t i = 0; i < vis; i++) {
            u16 oc = s_sm_frame[i], nc = nf[i];
            int orr = (oc >> 11) & 0x1F, ogg = (oc >> 5) & 0x3F, obb = oc & 0x1F;
            int nrr = (nc >> 11) & 0x1F, ngg = (nc >> 5) & 0x3F, nbb = nc & 0x1F;
            int rr = (orr * wo + nrr * wn) >> 4;
            int gg = (ogg * wo + ngg * wn) >> 4;
            int bb = (obb * wo + nbb * wn) >> 4;
            s_sm_frame[i] = (u16)((rr << 11) | (gg << 5) | bb);
        }
    }
    return s_sm_frame;
}

/* --- Buffer FX: dither hysteresis (sticky 4-level quantisation) ------------
 * The 4-level Bayer dither turns tiny frame-to-frame luma drift (the AE/AWB
 * "breathing") into large, visible level flips - the MAIN source of the base
 * image "kippeln". To stop that WITHOUT blurring edges, every output pixel
 * remembers the luma that produced its currently shown dither level; the level
 * only changes once the luma moves by more than a margin. The margin is scaled
 * by Buffer FX: strong at Off (clean), 0 at X5 (behaves exactly like today).
 * A single buffer keyed by output (x,y) is shared by the poly/pixel/raw render
 * branches and reset whenever the geometry, render mode or level changes. */
#define CAM_DITH_MAX (200 * 160)   /* max stabilised output pixels (>= 120x90) */
static unsigned char s_dith_lvl[CAM_DITH_MAX];   /* 0..3, or 0xFF = unseen */
static unsigned char s_dith_lum[CAM_DITH_MAX];   /* luma 0..255 that set it */
static int s_dith_init = 0;
static int s_dith_w = 0, s_dith_h = 0, s_dith_sig = 0;

/* Dither hysteresis luma margin (0..255) per g_cam_smooth stage 0..10 (technique
 * ladder; see cam_view.h). index10 (X10) = raw passthrough: margin 0 (no
 * hysteresis). index0 (new Off, Luma-Lock) uses a moderate margin (24) since the
 * normalize/freeze is the primary stabilizer; index5 (X5) restores the previous
 * Off margin (40). Indices 1..10 are the old 0..9 shifted up by one. */
static const int CAM_DITH_MARGIN[CAM_SMOOTH_LEVELS + 1] = { 24, 24, 40, 32, 20, 40, 28, 22, 16, 5, 0 };

/* Reset hysteresis state when geometry / render mode / level changed. */
static void dith_reset_if_needed(int dw, int dh, int sig)
{
    if (!s_dith_init || dw != s_dith_w || dh != s_dith_h || sig != s_dith_sig) {
        s_dith_init = 1;
        s_dith_w = dw; s_dith_h = dh; s_dith_sig = sig;
        memset(s_dith_lvl, 0xFF, sizeof(s_dith_lvl));   /* mark all unseen */
    }
}

/* Sticky dither level for output pixel idx given the freshly computed candidate
 * level (0..3) and source luma (0..255). margin <= 0 -> no hysteresis (returns
 * the candidate unchanged, today's behaviour at X5). */
static int dith_sticky(int idx, int lvl, int luma, int margin)
{
    if (margin <= 0 || idx < 0 || idx >= CAM_DITH_MAX) return lvl;
    if (s_dith_lvl[idx] == 0xFF) {                 /* first touch: adopt */
        s_dith_lvl[idx] = (unsigned char)lvl;
        s_dith_lum[idx] = (unsigned char)luma;
        return lvl;
    }
    int dl = luma - (int)s_dith_lum[idx];
    if (dl < 0) dl = -dl;
    if (dl <= margin) return (int)s_dith_lvl[idx]; /* within deadband: keep */
    s_dith_lvl[idx] = (unsigned char)lvl;          /* moved enough: update */
    s_dith_lum[idx] = (unsigned char)luma;
    return lvl;
}

/* Deadband for the exported cam-matrix values - the continuous analog of the
 * on-screen dither hysteresis, so the PD matrix is stabilised consistently with
 * the image: a cell's value only updates once it moves by more than the Buffer
 * FX margin. Off exports a stable matrix; X5 (margin 0) exports today's raw
 * values. Reset when the cell count or level changes. Runs BEFORE the glitch
 * layer so Glitch still stacks on top. */
static float s_mx_val[2048];
static int   s_mx_init = 0, s_mx_cells = 0, s_mx_lvl = -1;

static void matrix_deadband(float *out, int cells)
{
    int lvl = g_cam_smooth;
    if (lvl < 0) lvl = 0;
    if (lvl > CAM_SMOOTH_LEVELS) lvl = CAM_SMOOTH_LEVELS;
    float margin = (float)CAM_DITH_MARGIN[lvl] / 255.0f;
    if (margin <= 0.0f) { s_mx_init = 0; return; }     /* X10: no hysteresis */

    if (!s_mx_init || cells != s_mx_cells || lvl != s_mx_lvl) {
        s_mx_init = 1; s_mx_cells = cells; s_mx_lvl = lvl;
        for (int i = 0; i < cells && i < 2048; i++) s_mx_val[i] = out[i];
        return;
    }
    for (int i = 0; i < cells && i < 2048; i++) {
        float d = out[i] - s_mx_val[i];
        if (d < 0.0f) d = -d;
        if (d <= margin) out[i] = s_mx_val[i];   /* within deadband: keep */
        else             s_mx_val[i] = out[i];   /* moved enough: update */
    }
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

/* Average luminance (0..1) of each grid cell, row-major out[gy*gw+gx]. */
static void grid_avg(const u16 *frame, int gw, int gh, float *out)
{
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
}

/* Average luminance (0..255) of the two triangles in every gw x gh cell, written
 * as tri[(gy*gw+gx)*2 + t]: t=0 upper-left triangle (cell-local fu <= fv), t=1
 * lower-right. The split is the main diagonal of the cell. */
static void poly_tri_luma(const u16 *frame, int gw, int gh, int *tri)
{
    const int bw = CAM_FRAME_W  / gw;
    const int bh = CAM_VISIBLE_H / gh;
    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            unsigned long s0 = 0, n0 = 0, s1 = 0, n1 = 0;
            int y0 = gy * bh, x0 = gx * bw;
            for (int yy = 0; yy < bh; yy++) {
                const u16 *srow = frame + (size_t)(y0 + yy) * CAM_FRAME_W;
                for (int xx = 0; xx < bw; xx++) {
                    int r, g, b;
                    rgb565_to_rgb8(srow[x0 + xx], &r, &g, &b);
                    unsigned lu = (unsigned)((299 * r + 587 * g + 114 * b) / 1000);
                    if ((long)xx * bh <= (long)yy * bw) { s0 += lu; n0++; }
                    else                                { s1 += lu; n1++; }
                }
            }
            int base = (gy * gw + gx) * 2;
            tri[base + 0] = n0 ? (int)(s0 / n0) : 0;
            tri[base + 1] = n1 ? (int)(s1 / n1) : 0;
        }
    }
}

/* Glitch layer for the exported cam-matrix values: random replace + noise,
 * stacked on whichever base (polygon / pixel / default) produced them. No-op
 * when g_cam_glitch is Off, so Glitch stays combinable with every base. */
static void apply_glitch_values(float *out, int cells)
{
    int gl = clampi_local(g_cam_glitch, 0, 5);
    if (gl <= 0) return;
    s_glitch_rng ^= (unsigned)svcGetSystemTick();
    float    amt       = (float)gl / 5.0f;      /* 0..1 noise amount        */
    unsigned repl_prob = (unsigned)gl * 12;     /* /256 full-random replace */
    for (int i = 0; i < cells; i++) {
        unsigned h = glitch_rand();
        float v = out[i];
        if ((h & 0xFF) < repl_prob) {
            v = (float)((h >> 8) & 0xFFFF) / 65535.0f;
        } else {
            float noise = ((float)((h >> 8) & 0x3FF) / 1023.0f - 0.5f) * amt;
            v += noise;
        }
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = v;
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

    /* Temporal smoothing of the base image (Off -> unchanged live frame). */
    frame = smoothed_frame(frame);

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

    /* Base render selection (precedence Polygon > Pixel > raw). Glitch, when on,
     * is layered on top of whichever base is drawn. Only the top CAM_VISIBLE_H
     * source rows are sampled (the dead bottom strip is cropped), so the visible
     * rows fill the whole box and the unwritten DMA rows can never appear. */
    int pix  = clampi_local(g_cam_pixel,   0, CAM_PIXEL_LEVELS);
    int poly = clampi_local(g_cam_polygon, 0, CAM_POLY_LEVELS);

    /* Buffer FX dither hysteresis: margin scaled by level (strong at Off, 0 at
     * X10). The signature folds geometry + render mode + level so the sticky
     * buffer resets cleanly whenever any of them changes. */
    int dith_lvl = clampi_local(g_cam_smooth, 0, CAM_SMOOTH_LEVELS);
    int dith_margin = CAM_DITH_MARGIN[dith_lvl];
    int dith_sig = poly * 37 + pix * 7 + dith_lvl;
    dith_reset_if_needed(dw, dh, dith_sig);

    int gl = clampi_local(g_cam_glitch, 0, 5);
    if (gl > 0) s_glitch_rng ^= (unsigned)svcGetSystemTick();   /* reseed/frame */
    int      shear_max = gl * 2;                 /* cells/px of horizontal shear */
    unsigned repl_prob = (unsigned)gl * 10;      /* /256 random-replace chance   */

    if (poly > 0) {
        /* Low-poly triangle render: each grid cell is split along its main
         * diagonal into two flat-filled triangles (average luminance). */
        int gw, gh;
        grid_dims(poly, &gw, &gh);
        static int tri[CAM_POLY_MAX_TRI];
        poly_tri_luma(frame, gw, gh, tri);

        for (int y = 0; y < dh; y++) {
            int cellY = (y * gh) / dh;
            int fv    = y * gh - cellY * dh;     /* cell-local 0..dh-1 */
            int sy    = oy + y;
            int shear = 0;
            if (shear_max > 0) {
                unsigned h = (unsigned)cellY * 2654435761u ^ s_glitch_rng;
                shear = (int)(h % (unsigned)(2 * shear_max + 1)) - shear_max;
            }
            for (int x = 0; x < dw; x++) {
                int cellX = (x * gw) / dw;
                int fu    = x * gw - cellX * dw;  /* cell-local 0..dw-1 */
                int tt    = ((long)fu * dh <= (long)fv * dw) ? 0 : 1;
                int scellX = cellX + shear;
                if (scellX < 0) scellX = 0;
                if (scellX >= gw) scellX = gw - 1;
                int plum = tri[(cellY * gw + scellX) * 2 + tt];
                int lvl  = plum * 4 / 256;
                if (lvl > 3) lvl = 3;
                lvl = dith_sticky(y * dw + x, lvl, plum, dith_margin);
                if (repl_prob > 0) {
                    unsigned hc = ((unsigned)scellX * 73856093u)
                                ^ ((unsigned)cellY  * 19349663u)
                                ^ ((unsigned)tt     * 83492791u) ^ s_glitch_rng;
                    if ((hc & 0xFF) < repl_prob) lvl = (int)((hc >> 8) & 3);
                }
                set_px_top(fb, ox + x, sy, pal[lvl]);
            }
        }
    } else if (pix > 0) {
        /* Pixelation grid: display pixel -> grid cell, sampled at the cell centre
         * (unchanged tested look). Glitch shears rows and replaces random cells. */
        int gw, gh;
        grid_dims(pix, &gw, &gh);
        const int cell_w = CAM_FRAME_W  / gw;
        const int cell_h = CAM_VISIBLE_H / gh;

        for (int y = 0; y < dh; y++) {
            int cellY = (y * gh) / dh;
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
                int cellX = (x * gw) / dw;
                int scellX = cellX + shear;
                if (scellX < 0) scellX = 0;
                if (scellX >= gw) scellX = gw - 1;
                int srcx = scellX * cell_w + cell_w / 2;
                if (srcx >= CAM_FRAME_W) srcx = CAM_FRAME_W - 1;

                int r, g, b;
                rgb565_to_rgb8(srow[srcx], &r, &g, &b);
                int luma = (299 * r + 587 * g + 114 * b) / 1000;
                int v    = luma * 3;
                int base = v / 255;
                int frac = v - base * 255;
                int thr  = (BAYER4[y & 3][x & 3] * 255) / 16 + 8;
                int lvl  = base + (frac > thr ? 1 : 0);
                if (lvl < 0) lvl = 0;
                if (lvl > 3) lvl = 3;
                lvl = dith_sticky(y * dw + x, lvl, luma, dith_margin);
                if (repl_prob > 0) {
                    unsigned hc = ((unsigned)cellX * 73856093u)
                                ^ ((unsigned)cellY * 19349663u) ^ s_glitch_rng;
                    if ((hc & 0xFF) < repl_prob) lvl = (int)((hc >> 8) & 3);
                }
                set_px_top(fb, ox + x, sy, pal[lvl]);
            }
        }
    } else {
        /* Raw feed (both Off): nearest-neighbour scale + Bayer dither, with the
         * glitch layer (per-row shear + random replace) still stacked on top. */
        for (int y = 0; y < dh; y++) {
            int sy = oy + y;
            int srcy = (y * CAM_VISIBLE_H) / dh;
            if (srcy >= CAM_VISIBLE_H) srcy = CAM_VISIBLE_H - 1;
            int shear = 0;
            if (shear_max > 0) {
                unsigned h = (unsigned)srcy * 2654435761u ^ s_glitch_rng;
                shear = (int)(h % (unsigned)(2 * shear_max + 1)) - shear_max;
            }
            const u16 *srow = frame + (size_t)srcy * CAM_FRAME_W;

            for (int x = 0; x < dw; x++) {
                int srcx = (x * CAM_FRAME_W) / dw + shear;
                if (srcx < 0) srcx = 0;
                if (srcx >= CAM_FRAME_W) srcx = CAM_FRAME_W - 1;

                int r, g, b;
                rgb565_to_rgb8(srow[srcx], &r, &g, &b);
                int luma = (299 * r + 587 * g + 114 * b) / 1000;
                int v    = luma * 3;
                int base = v / 255;
                int frac = v - base * 255;
                int thr  = (BAYER4[y & 3][x & 3] * 255) / 16 + 8;
                int lvl  = base + (frac > thr ? 1 : 0);
                if (lvl < 0) lvl = 0;
                if (lvl > 3) lvl = 3;
                lvl = dith_sticky(y * dw + x, lvl, luma, dith_margin);
                if (repl_prob > 0) {
                    unsigned hc = ((unsigned)x    * 73856093u)
                                ^ ((unsigned)srcy * 19349663u) ^ s_glitch_rng;
                    if ((hc & 0xFF) < repl_prob) lvl = (int)((hc >> 8) & 3);
                }
                set_px_top(fb, ox + x, sy, pal[lvl]);
            }
        }
    }
}

void cam_view_matrix(const u16 *frame)
{
    if (!frame) return;

    /* Smooth the source first so the exported values match the on-screen image
     * (same serial -> the blend done by whichever of draw/matrix runs first is
     * reused here). Off -> unchanged live frame. */
    frame = smoothed_frame(frame);

    int sz = libpd_arraysize("cam-matrix");
    if (sz <= 0) return;                 /* array absent -> no-op, no error */

    /* Who writes cam-matrix follows the precedence Polygon > Pixel > default:
     *   Polygon on            -> one value per triangle (2 * gw * gh, <= 1536).
     *   Polygon off, Pixel on -> one value per grid cell  (gw * gh, <= 768).
     *   both off              -> default 16x16 brightness grid (256 cells).
     * All values are 0..1 and row-major; only the cropped visible rows are
     * averaged, matching the on-screen image. Glitch (when on) is then stacked
     * on top via apply_glitch_values(), keeping it combinable with every base.
     * NOTE: size the PD "cam-matrix" array to at least the active value count
     * (up to 1536 for a full X1 polygon grid); a shorter array just receives
     * the first values. */
    static float out[2048];
    int cells;

    int pix  = clampi_local(g_cam_pixel,   0, CAM_PIXEL_LEVELS);
    int poly = clampi_local(g_cam_polygon, 0, CAM_POLY_LEVELS);

    if (poly > 0) {
        int gw, gh;
        grid_dims(poly, &gw, &gh);
        static int tri[CAM_POLY_MAX_TRI];
        poly_tri_luma(frame, gw, gh, tri);
        cells = gw * gh * 2;
        for (int i = 0; i < cells; i++) out[i] = (float)tri[i] / 255.0f;
    } else if (pix > 0) {
        int gw, gh;
        grid_dims(pix, &gw, &gh);
        cells = gw * gh;
        grid_avg(frame, gw, gh, out);
    } else {
        /* both Off: default 16x16 brightness grid (behaves as before). */
        cells = CAM_DEFAULT_GRID * CAM_DEFAULT_GRID;
        grid_avg(frame, CAM_DEFAULT_GRID, CAM_DEFAULT_GRID, out);
    }

    /* Buffer FX stabilisation of the exported values (deadband), kept consistent
     * with the on-screen dither hysteresis. Runs before the glitch layer. */
    matrix_deadband(out, cells);

    apply_glitch_values(out, cells);

    int n = cells < sz ? cells : sz;
    libpd_write_array("cam-matrix", 0, out, n);
}

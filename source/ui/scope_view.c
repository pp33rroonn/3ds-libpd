/* scope_view.c - audio visualisation for the top screen (see scope_view.h) */

#include "scope_view.h"
#include "scope_ascii.h"
#include "draw.h"
#include "font5x8.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WAVE_DEC        2      /* samples per pixel column (~17.6 ms on 388 px) */
#define WAVE_SEARCH     1024   /* max samples searched back for the trigger */
#define WAVE_SILENCE    64     /* below this |amplitude| no trigger is searched */
#define GONIO_FRAMES    1024   /* points plotted in the stereo view */
#define LEVEL_DB_MIN    (-60.0f)
#define HOLD_FRAMES     45     /* peak-hold time before it starts falling */
#define HOLD_FALL_DB    0.5f   /* dB per video frame after the hold time */
#define ENV_MAX_W       TOP_W

static const char *const MODE_NAMES[SCOPE_MODE_COUNT] = {
    "Waveform", "Envelope", "Stereo", "Level",
    "ASCII Wave", "ASCII Rain", "ASCII Scope", "Braille Scope", "Phosphor", "Stereo Dots", "Stereo Glow", "Stereo Field"
};

/* Envelope history (ring, newest at s_env_pos - 1) */
static uint8_t s_env_l[ENV_MAX_W], s_env_r[ENV_MAX_W];
static int     s_env_pos = 0, s_env_len = 0;

/* Level peak hold per channel */
static float s_hold_db[2]  = {LEVEL_DB_MIN, LEVEL_DB_MIN};
static int   s_hold_cnt[2] = {0, 0};

void scope_view_reset(void)
{
    s_env_pos = s_env_len = 0;
    s_hold_db[0] = s_hold_db[1] = LEVEL_DB_MIN;
    s_hold_cnt[0] = s_hold_cnt[1] = 0;
    scope_ascii_reset();
}

/* ---- helpers ------------------------------------------------------------- */

typedef struct { int x, y, w, h; } Rect;   /* inner drawing area */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* vertical span y0..y1 (any order) in the theme fg (rainbow-aware) */
static void vspan_fg(u16 *fb, const Rect *a, int x, int y0, int y1)
{
    if (x < a->x || x >= a->x + a->w) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    y0 = clampi(y0, a->y, a->y + a->h - 1);
    y1 = clampi(y1, a->y, a->y + a->h - 1);
    for (int y = y0; y <= y1; y++) draw_pixel(fb, x, y, theme_fg_at(y));
}

static void dotted_hline(u16 *fb, int x, int y, int w, u16 c)
{
    for (int i = 0; i < w; i += 4) draw_pixel(fb, x + i, y, c);
}

static void dotted_vline(u16 *fb, int x, int y, int h, u16 c)
{
    for (int i = 0; i < h; i += 4) draw_pixel(fb, x, y + i, c);
}

static int peak_of(const int16_t *s, int n)
{
    int pk = 0;
    for (int i = 0; i < n; i++) {
        int v = s[i] < 0 ? -(int)s[i] : s[i];
        if (v > pk) pk = v;
    }
    return pk;
}

static float to_db(float amp)   /* amp 0..32768 -> dBFS, floored */
{
    if (amp < 1.0f) return LEVEL_DB_MIN;
    float db = 20.0f * log10f(amp / 32768.0f);
    return db < LEVEL_DB_MIN ? LEVEL_DB_MIN : db;
}

/* ---- 0: Waveform --------------------------------------------------------- */

static int mono_at(const int16_t *l, const int16_t *r, int i)
{
    return ((int)l[i] + (int)r[i]) / 2;
}

static void draw_wave(u16 *fb, const Rect *a, const int16_t *l,
                      const int16_t *r, int n)
{
    int cy  = a->y + a->h / 2;
    int amp = a->h / 2 - 2;
    dotted_hline(fb, a->x, cy, a->w, TH.dim);
    if (n < 2 || amp < 1) return;

    int dec  = WAVE_DEC;
    int need = a->w * dec;
    if (need > n) { dec = 1; need = a->w < n ? a->w : n; }

    /* trigger: newest rising zero crossing that still leaves `need` samples */
    int start = n - need;
    if (peak_of(l, n) >= WAVE_SILENCE || peak_of(r, n) >= WAVE_SILENCE) {
        int lo = start - WAVE_SEARCH;
        if (lo < 1) lo = 1;
        for (int i = start; i >= lo; i--) {
            if (mono_at(l, r, i - 1) < 0 && mono_at(l, r, i) >= 0) { start = i; break; }
        }
    }

    int cols = need / dec, prev_y = cy;
    for (int c = 0; c < cols; c++) {
        int sum = 0;
        for (int k = 0; k < dec; k++) sum += mono_at(l, r, start + c * dec + k);
        int v  = sum / dec;
        int yy = cy - v * amp / 32768;
        vspan_fg(fb, a, a->x + c, c ? prev_y : yy, yy);
        prev_y = yy;
    }
}

/* ---- 1: Envelope --------------------------------------------------------- */

static void draw_envelope(u16 *fb, const Rect *a, const int16_t *l,
                          const int16_t *r, int n)
{
    int m = n < SCOPE_FRAME_SAMPLES ? n : SCOPE_FRAME_SAMPLES;
    const int16_t *nl = l + (n - m), *nr = r + (n - m);
    s_env_l[s_env_pos] = (uint8_t)(peak_of(nl, m) * 255 / 32767);
    s_env_r[s_env_pos] = (uint8_t)(peak_of(nr, m) * 255 / 32767);
    s_env_pos = (s_env_pos + 1) % ENV_MAX_W;
    if (s_env_len < ENV_MAX_W) s_env_len++;

    int cy   = a->y + a->h / 2;
    int half = a->h / 2 - 1;
    dotted_hline(fb, a->x, cy, a->w, TH.dim);
    draw_string(fb, a->x + a->w - 2 * FONT_ADV_X, a->y + 2, "L", TH.dim, TH.bg);
    draw_string(fb, a->x + a->w - 2 * FONT_ADV_X, a->y + a->h - FONT_ADV_Y - 1,
                "R", TH.dim, TH.bg);

    int cols = a->w < s_env_len ? a->w : s_env_len;
    for (int c = 0; c < cols; c++) {           /* newest at the right edge */
        int idx = (s_env_pos - 1 - c + ENV_MAX_W) % ENV_MAX_W;
        int x   = a->x + a->w - 1 - c;
        int hl  = s_env_l[idx] * half / 255;
        int hr  = s_env_r[idx] * half / 255;
        if (hl) vspan_fg(fb, a, x, cy - hl, cy - 1);
        if (hr) vspan_fg(fb, a, x, cy + 1, cy + hr);
    }
}

/* ---- 2: Stereo (goniometer) ---------------------------------------------- */

static void draw_stereo(u16 *fb, const Rect *a, const int16_t *l,
                        const int16_t *r, int n)
{
    int side = a->w < a->h ? a->w : a->h;
    int half = side / 2 - 1;
    int cx = a->x + a->w / 2, cy = a->y + a->h / 2;

    /* guides: M (vertical), S (horizontal), L / R diagonals */
    dotted_vline(fb, cx, cy - half, 2 * half + 1, TH.dim);
    dotted_hline(fb, cx - half, cy, 2 * half + 1, TH.dim);
    for (int i = -half; i <= half; i += 3) {
        draw_pixel(fb, cx + i, cy + i, TH.dim);
        draw_pixel(fb, cx + i, cy - i, TH.dim);
    }
    int d = half * 7 / 10;
    draw_string(fb, cx - d - FONT_ADV_X - 2, cy - d - FONT_ADV_Y, "L", TH.dim, TH.bg);
    draw_string(fb, cx + d + 3,             cy - d - FONT_ADV_Y, "R", TH.dim, TH.bg);
    draw_string(fb, cx - FONT_ADV_X / 2 + 3, a->y + 1,           "M", TH.dim, TH.bg);

    int m = n < GONIO_FRAMES ? n : GONIO_FRAMES;
    double slr = 0, sll = 0, srr = 0;
    for (int i = n - m; i < n; i++) {
        int L = l[i], R = r[i];
        int mid = (L + R) / 2, sd = (R - L) / 2;   /* L-only -> upper left */
        int px = cx + sd * half / 32768;
        int py = cy - mid * half / 32768;
        if (py >= a->y && py < a->y + a->h)
            draw_pixel(fb, px, py, theme_fg_at(py));
        slr += (double)L * R; sll += (double)L * L; srr += (double)R * R;
    }

    /* phase correlation +1 (mono) .. 0 (wide) .. -1 (out of phase) */
    int tx = a->x + 4, ty = a->y + a->h - FONT_ADV_Y - 2;
    if (sll > 1e3 * m && srr > 1e3 * m) {
        float corr = (float)(slr / sqrt(sll * srr));
        draw_stringf(fb, tx, ty, theme_fg_at(ty), TH.bg, "Corr %+.2f", (double)corr);
    } else {
        draw_string(fb, tx, ty, "Corr  --", TH.dim, TH.bg);
    }
}

/* ---- 3: Level ------------------------------------------------------------ */

static void draw_level(u16 *fb, const Rect *a, const int16_t *l,
                       const int16_t *r, int n)
{
    static const int TICKS[] = {-60, -48, -36, -24, -18, -12, -6, -3, 0};
    int m = n < SCOPE_FRAME_SAMPLES ? n : SCOPE_FRAME_SAMPLES;
    const int16_t *ch[2] = {l + (n - m), r + (n - m)};
    float rms_db[2], pk_db[2];

    for (int c = 0; c < 2; c++) {
        double acc = 0;
        for (int i = 0; i < m; i++) acc += (double)ch[c][i] * ch[c][i];
        rms_db[c] = m ? to_db((float)sqrt(acc / m)) : LEVEL_DB_MIN;
        pk_db[c]  = to_db((float)peak_of(ch[c], m));
        if (pk_db[c] >= s_hold_db[c]) {
            s_hold_db[c] = pk_db[c];
            s_hold_cnt[c] = HOLD_FRAMES;
        } else if (s_hold_cnt[c] > 0) {
            s_hold_cnt[c]--;
        } else {
            s_hold_db[c] -= HOLD_FALL_DB;
            if (s_hold_db[c] < LEVEL_DB_MIN) s_hold_db[c] = LEVEL_DB_MIN;
        }
    }

    int bx = a->x + 3 * FONT_ADV_X, bw = a->w - 6 * FONT_ADV_X;
    int bh = 18, gap = 10;
    int total = 2 * bh + gap + 4 + FONT_ADV_Y + 6 + FONT_ADV_Y;
    int by = a->y + (a->h - total) / 2;
    if (by < a->y + 2) by = a->y + 2;
    int warn_x = bx + (int)((-6.0f - LEVEL_DB_MIN) / -LEVEL_DB_MIN * bw);

    static const char *const LBL[2] = {"L", "R"};
    for (int c = 0; c < 2; c++) {
        int y = by + c * (bh + gap);
        draw_string(fb, a->x + FONT_ADV_X, y + (bh - FONT_ADV_Y) / 2 + 1, LBL[c],
                    theme_fg_at(y), TH.bg);
        draw_rect(fb, bx - 1, y - 1, bw + 2, bh + 2, TH.dim);
        int fw = (int)((rms_db[c] - LEVEL_DB_MIN) / -LEVEL_DB_MIN * bw);
        for (int x = bx; x < bx + fw; x++) {
            if (x >= warn_x) { draw_vline(fb, x, y, bh, TH.accent); continue; }
            for (int yy = y; yy < y + bh; yy++) draw_pixel(fb, x, yy, theme_fg_at(yy));
        }
        if (s_hold_db[c] > LEVEL_DB_MIN) {
            int hx = bx + (int)((s_hold_db[c] - LEVEL_DB_MIN) / -LEVEL_DB_MIN * bw);
            if (hx > bx + bw - 2) hx = bx + bw - 2;
            draw_vline(fb, hx, y, bh, TH.accent);
            draw_vline(fb, hx + 1, y, bh, TH.accent);
        }
    }

    /* dB scale */
    int sy = by + 2 * bh + gap + 4;
    for (size_t i = 0; i < sizeof(TICKS) / sizeof(TICKS[0]); i++) {
        int tx = bx + (int)(((float)TICKS[i] - LEVEL_DB_MIN) / -LEVEL_DB_MIN * bw);
        draw_vline(fb, tx, sy - 3, 3, TH.dim);
        char t[8];
        snprintf(t, sizeof(t), "%d", TICKS[i]);
        int len = (int)strlen(t);
        if (TICKS[i] == -3) continue;          /* too close to -6 / 0 for a label */
        draw_string(fb, tx - len * FONT_ADV_X / 2, sy, t, TH.dim, TH.bg);
    }

    int ry = sy + FONT_ADV_Y + 6;
    draw_stringf(fb, bx, ry, theme_fg_at(ry), TH.bg,
                 "Peak L %5.1f dB   R %5.1f dB", (double)pk_db[0], (double)pk_db[1]);
}

/* ---- public -------------------------------------------------------------- */

void scope_view_draw(u16 *fb, int x, int y, int w, int h, int mode,
                     const int16_t *l, const int16_t *r, int n)
{
    if (!fb || w < 8 || h < 8) return;
    if (mode < 0 || mode >= SCOPE_MODE_COUNT) mode = SCOPE_WAVEFORM;
    if (!l || !r) n = 0;

    draw_rect(fb, x, y, w, h, TH.dim);
    Rect a = { x + 1, y + 1, w - 2, h - 2 };

    switch (mode) {
        case SCOPE_ENVELOPE: draw_envelope(fb, &a, l, r, n); break;
        case SCOPE_STEREO:   draw_stereo(fb, &a, l, r, n);   break;
        case SCOPE_LEVEL:    draw_level(fb, &a, l, r, n);    break;
        case SCOPE_ASCII_WAVE: case SCOPE_ASCII_RAIN: case SCOPE_ASCII_SCOPE:
        case SCOPE_ASCII_BRAILLE: case SCOPE_ASCII_PHOSPHOR:
        case SCOPE_ASCII_STEREO_DOTS: case SCOPE_ASCII_STEREO_GLOW:
        case SCOPE_ASCII_STEREO_FIELD:
            scope_ascii_draw(fb, a.x, a.y, a.w, a.h, mode - SCOPE_ASCII_WAVE, l, r, n);
            break;
        default:             draw_wave(fb, &a, l, r, n);     break;
    }

    /* mode name in the top-left corner (drawn last so it stays readable) */
    draw_string(fb, a.x + 3, a.y + 2, MODE_NAMES[mode], TH.dim, TH.bg);
}

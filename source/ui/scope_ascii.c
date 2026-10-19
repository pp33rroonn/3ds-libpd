/* scope_ascii.c - text-mode audio views (see scope_ascii.h) */

#include "scope_ascii.h"
#include "draw.h"
#include "font5x8.h"
#include "theme.h"

#include <math.h>
#include <string.h>

#define CW          FONT_ADV_X          /* cell width  (6) */
#define CH          FONT_ADV_Y          /* cell height (9) */
#define MAX_COLS    (TOP_W / 6 + 1)
#define MAX_ROWS    (TOP_H / 9 + 1)
#define SCOPE_SPC   12                  /* samples per text column (scopes) */
#define TRIG_SEARCH 1024
#define TRIG_SILENCE 64

/* ---- extended glyph codes ---------------------------------------------- */
/* < 0x80 : ASCII via the 5x7 font                                          */
#define G_SHADE(k)  (0x100 + (k))   /* k 1..4 : light, medium, dark, full   */
#define G_LOW(k)    (0x110 + (k))   /* k 1..8 : lower k/8 of the cell       */
#define G_UP(k)     (0x120 + (k))   /* k 1..8 : upper k/8 of the cell       */
#define G_BOX(k)    (0x140 + (k))   /* see box_px()                         */
#define G_BRL(b)    (0x200 + (b))   /* b 0..255 : braille dots, bit=row*2+col */
#define G_RUNE(s)   (0x400 + (s))   /* s 0..255 : random symmetric glyph    */

enum { BX_H, BX_V, BX_SL, BX_BS, BX_CROSS, BX_X, BX_DIAMOND, BX_DOT,
       BX_DH, BX_DV, BX_COUNT };

typedef enum { C_FG, C_DIM, C_ACC } Col;

typedef struct { int x0, y0, cols, rows; } Grid;

/* ---- state -------------------------------------------------------------- */

static int8_t  s_hmax[MAX_COLS], s_hmin[MAX_COLS];   /* wave history ring */
static int     s_hpos = 0, s_hlen = 0;
static int16_t s_rain_g[MAX_ROWS][MAX_COLS];         /* rain glyph memory */
static int8_t  s_drop[MAX_COLS] = { [0 ... MAX_COLS - 1] = -1 };   /* rain drop row, -1 none */
static uint8_t s_glow[MAX_ROWS][MAX_COLS];           /* phosphor 0..255 */
static uint32_t s_rng = 0x2545F491u;
static uint8_t s_sglow[MAX_ROWS][MAX_COLS];          /* stereo glow 0..255 */
static int     s_gx = 0, s_gy = 0;                   /* stereo auto-gain peaks */
static float   s_ray[MAX_COLS];                      /* stereo field ray length 0..1 */
static uint8_t s_rayhit[MAX_COLS];                   /* stereo field ray brightness */

void scope_ascii_reset(void)
{
    s_hpos = s_hlen = 0;
    memset(s_rain_g, 0, sizeof(s_rain_g));
    memset(s_drop, -1, sizeof(s_drop));
    memset(s_glow, 0, sizeof(s_glow));
    memset(s_sglow, 0, sizeof(s_sglow));
    memset(s_ray, 0, sizeof(s_ray));
    memset(s_rayhit, 0, sizeof(s_rayhit));
    s_gx = s_gy = 0;
}

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* ---- glyph rendering ---------------------------------------------------- */

static int box_px(int k, int px, int py)
{
    int dx = px - 2, dy = py - 4;
    switch (k) {
        case BX_H:       return py == 4;
        case BX_V:       return px == 2;
        case BX_SL:      return px == (8 - py) * 5 / 8;
        case BX_BS:      return px == py * 5 / 8;
        case BX_CROSS:   return py == 4 || px == 2;
        case BX_X:       return px == (8 - py) * 5 / 8 || px == py * 5 / 8;
        case BX_DIAMOND: return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) <= 2;
        case BX_DOT:     return dx * dx + dy * dy <= 3;
        case BX_DH:      return py == 3 || py == 5;
        case BX_DV:      return px == 1 || px == 3;
        default:         return 0;
    }
}

static int rune_px(int seed, int px, int py)
{
    if (px > 4 || py < 1 || py > 7) return 0;
    int mx = px < 4 - px ? px : 4 - px;            /* mirror -> 0..2 */
    uint32_t h = (uint32_t)seed * 2654435761u ^ (uint32_t)(mx * 7 + py) * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 7) < 3;
}

static int glyph_px(int g, int px, int py)
{
    if (g >= 0x400) return rune_px(g - 0x400, px, py);
    if (g >= 0x200) {
        int b = g - 0x200, col = px < 3 ? 0 : 1;
        if (px % 3 == 2 || !(py & 1) || py > 7) return 0;
        int row = py >> 1;
        return (b >> (row * 2 + col)) & 1;
    }
    if (g >= 0x140) return box_px(g - 0x140, px, py);
    if (g >= 0x120) return py < ((g - 0x120) * CH + 4) / 8;
    if (g >= 0x110) return py >= CH - ((g - 0x110) * CH + 4) / 8;
    switch (g - 0x100) {
        case 1:  return ((px + 2 * py) & 3) == 0;
        case 2:  return ((px + py) & 1) == 0;
        case 3:  return ((px + 2 * py) & 3) != 0;
        default: return 1;
    }
}

static u16 col_at(Col c, int y)
{
    return c == C_DIM ? TH.dim : c == C_ACC ? TH.accent : theme_fg_at(y);
}

static void cell(u16 *fb, const Grid *G, int cx, int cy, int g, Col c)
{
    if (cx < 0 || cy < 0 || cx >= G->cols || cy >= G->rows || g <= ' ') return;
    int x = G->x0 + cx * CW, y = G->y0 + cy * CH;
    if (g < 0x80) {
        draw_char(fb, x, y, (char)g, col_at(c, y + CH / 2), TH.bg);
        return;
    }
    for (int py = 0; py < CH; py++) {
        u16 col = col_at(c, y + py);
        for (int px = 0; px < CW; px++)
            if (glyph_px(g, px, py)) draw_pixel(fb, x + px, y + py, col);
    }
}

/* random glyph from "everything": ASCII, runes, braille, boxes, shades */
static int wild_glyph(void)
{
    static const char ASC[] = "#$%&@*+=?!/\\|<>{}[]()~^:;0123456789ABCDEFXYZ";
    uint32_t r = rnd();
    switch (r % 6) {
        case 0: case 1: return ASC[(r >> 8) % (sizeof(ASC) - 1)];
        case 2:         return G_RUNE((r >> 8) & 255);
        case 3:         return G_BRL(((r >> 8) & 254) | 1);
        case 4:         return G_BOX((r >> 8) % BX_COUNT);
        default:        return G_SHADE(1 + (r >> 8) % 4);
    }
}

/* ---- shared helpers ----------------------------------------------------- */

static int mono_at(const int16_t *l, const int16_t *r, int i)
{
    return ((int)l[i] + (int)r[i]) / 2;
}

static int peak_mono(const int16_t *l, const int16_t *r, int from, int n)
{
    int pk = 0;
    for (int i = from; i < n; i++) {
        int v = mono_at(l, r, i);
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    return pk;
}

/* start index of a window of `need` samples, aligned to a rising zero crossing */
static int trigger(const int16_t *l, const int16_t *r, int n, int need)
{
    int start = n - need;
    if (start < 1) return start < 0 ? 0 : start;
    if (peak_mono(l, r, 0, n) < TRIG_SILENCE) return start;
    int lo = start - TRIG_SEARCH;
    if (lo < 1) lo = 1;
    for (int i = start; i >= lo; i--)
        if (mono_at(l, r, i - 1) < 0 && mono_at(l, r, i) >= 0) return i;
    return start;
}

/* push min/max of the newest video frame into the wave history */
static void push_history(const int16_t *l, const int16_t *r, int n)
{
    int m = n < 735 ? n : 735, mx = 0, mn = 0;
    for (int i = n - m; i < n; i++) {
        int v = mono_at(l, r, i);
        if (v > mx) mx = v;
        if (v < mn) mn = v;
    }
    s_hmax[s_hpos] = (int8_t)(mx * 127 / 32767);
    s_hmin[s_hpos] = (int8_t)(mn * 127 / 32768);
    s_hpos = (s_hpos + 1) % MAX_COLS;
    if (s_hlen < MAX_COLS) s_hlen++;
}

/* history column c (0 = newest) */
static void hist(int c, int *mx, int *mn)
{
    int idx = (s_hpos - 1 - c + 2 * MAX_COLS) % MAX_COLS;
    *mx = s_hmax[idx]; *mn = -s_hmin[idx];
}

/* ---- 0: ASCII wave ------------------------------------------------------ */

static void view_wave(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    static const int RAMP[] = { '@', '#', '%', '&', '$', '*', '+', '=', '~', ':',
                                '.' };
    const int nramp = (int)(sizeof(RAMP) / sizeof(RAMP[0]));
    int H = G->rows / 2;
    if (H < 1) return;
    if (n > 0) push_history(l, r, n);

    for (int cx = 0; cx < G->cols; cx++) cell(fb, G, cx, H, (cx & 3) ? '-' : '+', C_DIM);

    int cols = G->cols < s_hlen ? G->cols : s_hlen;
    for (int c = 0; c < cols; c++) {
        int cx = G->cols - 1 - c, up, dn;
        hist(c, &up, &dn);
        up = up * H * 8 / 127;           /* height in eighths of a cell */
        dn = dn * H * 8 / 127;
        for (int half = 0; half < 2; half++) {
            int e = half ? dn : up;
            for (int k = 0; k < H && e > 0; k++, e -= 8) {
                int cy = half ? H + 1 + k : H - 1 - k;
                if (half && cy >= G->rows) break;
                if (e >= 8) cell(fb, G, cx, cy, RAMP[k * nramp / H], C_FG);
                else        cell(fb, G, cx, cy, half ? G_UP(e) : G_LOW(e), C_ACC);
            }
        }
        if (up || dn) cell(fb, G, cx, H, up + dn > 8 * H ? G_BOX(BX_CROSS) : '=', C_ACC);
    }
}

/* ---- 1: ASCII rain (glitch waveform) ----------------------------------- */

static void view_rain(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    int H = G->rows / 2;
    if (H < 1) return;
    if (n > 0) push_history(l, r, n);

    int cols = G->cols < s_hlen ? G->cols : s_hlen;
    int lvl = 0;
    for (int cx = 0; cx < G->cols; cx++) {
        int c = G->cols - 1 - cx, up = 0, dn = 0;
        if (c < cols) hist(c, &up, &dn);
        if (c < 4 && up > lvl) lvl = up;
        int hu = (up * H + 63) / 127, hd = (dn * H + 63) / 127;
        for (int cy = 0; cy < G->rows; cy++) {
            int d = cy < H ? H - 1 - cy : cy - H;          /* distance from axis */
            int on = cy < H ? d < hu : (cy == H ? (hu || hd) : d < hd);
            int16_t *g = &s_rain_g[cy][cx];
            if (!on) { *g = 0; continue; }
            if (!*g || (rnd() & 3) == 0) *g = (int16_t)wild_glyph();
            int edge = cy < H ? d == hu - 1 : d == hd - 1;
            cell(fb, G, cx, cy, *g, edge ? C_ACC : C_FG);
        }
    }

    /* falling drops: spawn rate follows the level */
    for (int cx = 0; cx < G->cols; cx++) {
        if (s_drop[cx] < 0) {
            if ((int)(rnd() % 400) < 2 + lvl) s_drop[cx] = 0;
            continue;
        }
        if (s_rain_g[s_drop[cx]][cx] == 0) cell(fb, G, cx, s_drop[cx], wild_glyph(), C_DIM);
        if ((rnd() & 1) && ++s_drop[cx] >= G->rows) s_drop[cx] = -1;
    }
}

/* ---- 2: ASCII scope (line characters) ---------------------------------- */

static void view_scope(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    int rows = G->rows, sub = rows * 3;             /* 3 sub-rows per text row */
    int mid = rows / 2;
    for (int cy = 0; cy < rows; cy++)
        for (int cx = 0; cx < G->cols; cx++) {
            if (cy == mid)             cell(fb, G, cx, cy, (cx % 8) ? '-' : '+', C_DIM);
            else if (cx % 8 == 0)      cell(fb, G, cx, cy, ':', C_DIM);
            else if ((cx + cy) % 4 == 0) cell(fb, G, cx, cy, '.', C_DIM);
        }
    int need = G->cols * SCOPE_SPC;
    if (n < need || rows < 2) return;
    int st = trigger(l, r, n, need);

    int prev = -1;
    for (int cx = 0; cx < G->cols; cx++) {
        int a = st + cx * SCOPE_SPC, mn = 32767, mx = -32768;
        for (int k = 0; k < SCOPE_SPC; k++) {
            int v = mono_at(l, r, a + k);
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        int first = mono_at(l, r, a), last = mono_at(l, r, a + SCOPE_SPC - 1);
        /* sub-row 0 = top */
        int s_hi = clampi((32767 - mx) * (sub - 1) / 65535, 0, sub - 1);
        int s_lo = clampi((32767 - mn) * (sub - 1) / 65535, 0, sub - 1);
        if (prev >= 0) {                              /* connect to previous column */
            if (prev < s_hi) s_hi = prev;
            if (prev > s_lo) s_lo = prev;
        }
        int r_hi = s_hi / 3, r_lo = s_lo / 3;
        int rising = last > first;
        prev = clampi((32767 - last) * (sub - 1) / 65535, 0, sub - 1);
        if (r_hi == r_lo) {
            static const char SUB[3] = { '`', '-', '_' };
            int s = (s_hi + s_lo) / 2;
            cell(fb, G, cx, r_hi, s_lo - s_hi >= 2 ? (rising ? '/' : '\\') : SUB[s % 3], C_FG);
            continue;
        }
        for (int cy = r_hi; cy <= r_lo; cy++) {
            int g = rising ? '/' : '\\';
            if (r_lo - r_hi >= 2 && cy > r_hi && cy < r_lo) g = '|';
            if (cy == r_hi && cy != (rising ? prev / 3 : -1) && (s_hi % 3) == 2) g = '.';
            cell(fb, G, cx, cy, g, cy == r_hi || cy == r_lo ? C_ACC : C_FG);
        }
    }
}

/* ---- 3: braille scope --------------------------------------------------- */

static void view_braille(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    static uint8_t dots[MAX_ROWS][MAX_COLS];
    int sw = G->cols * 2, sh = G->rows * 4, spc = SCOPE_SPC / 2;
    int need = sw * spc;
    memset(dots, 0, sizeof(dots));

    /* faint braille grid: centre line + every 8th column */
    for (int cx = 0; cx < G->cols; cx++)
        cell(fb, G, cx, G->rows / 2, G_BRL(cx & 1 ? 0x01 : 0x03), C_DIM);
    for (int cx = 0; cx < G->cols; cx += 8)
        for (int cy = 0; cy < G->rows; cy++)
            if (cy != G->rows / 2) cell(fb, G, cx, cy, G_BRL(0x41), C_DIM);

    if (n < need || sh < 4) return;
    int st = trigger(l, r, n, need), prev = -1;
    for (int sx = 0; sx < sw; sx++) {
        int sum = 0;
        for (int k = 0; k < spc; k++) sum += mono_at(l, r, st + sx * spc + k);
        int sy = clampi((32767 - sum / spc) * (sh - 1) / 65535, 0, sh - 1);
        int y0 = prev < 0 ? sy : prev, y1 = sy;
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
        for (int yy = y0; yy <= y1; yy++)
            dots[yy >> 2][sx >> 1] |= (uint8_t)(1u << ((yy & 3) * 2 + (sx & 1)));
        prev = sy;
    }
    for (int cy = 0; cy < G->rows; cy++)
        for (int cx = 0; cx < G->cols; cx++)
            if (dots[cy][cx]) {
                /* clear the grid glyph underneath before drawing */
                draw_rect_filled(fb, G->x0 + cx * CW, G->y0 + cy * CH, CW, CH, TH.bg);
                cell(fb, G, cx, cy, G_BRL(dots[cy][cx]), C_FG);
            }
}

/* ---- 4: phosphor scope -------------------------------------------------- */

static void view_phosphor(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    static const int RAMP[] = { '.', '`', ':', '-', '~', '=', '+', '*', 'x', 'o',
                                '#', '%', '@', G_SHADE(1), G_SHADE(2), G_SHADE(3),
                                G_SHADE(4) };
    const int nramp = (int)(sizeof(RAMP) / sizeof(RAMP[0]));
    int rows = G->rows, cols = G->cols;

    for (int cy = 0; cy < rows; cy++)            /* afterglow decay */
        for (int cx = 0; cx < cols; cx++)
            s_glow[cy][cx] = (uint8_t)(s_glow[cy][cx] * 13 / 16);

    int need = cols * SCOPE_SPC, clip = 0;
    if (n >= need && rows >= 2) {
        int st = trigger(l, r, n, need), prev = -1;
        for (int i = 0; i < need; i++) {
            int v  = mono_at(l, r, st + i);
            int cx = i / SCOPE_SPC;
            int cy = clampi((32767 - v) * (rows * 2 - 1) / 65535, 0, rows * 2 - 1);
            if (v > 31000 || v < -31000) clip++;
            int y0 = prev < 0 ? cy : prev, y1 = cy;
            if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
            for (int yy = y0; yy <= y1; yy++) {          /* half-row resolution */
                int g = s_glow[yy >> 1][cx] + 7;
                s_glow[yy >> 1][cx] = (uint8_t)(g > 255 ? 255 : g);
            }
            prev = cy;
        }
    }

    for (int cy = 0; cy < rows; cy++)
        for (int cx = 0; cx < cols; cx++) {
            int g = s_glow[cy][cx];
            if (g < 6) {
                if (cy == rows / 2 && (cx & 1)) cell(fb, G, cx, cy, '-', C_DIM);
                continue;
            }
            int gl = RAMP[clampi(g * nramp / 256, 0, nramp - 1)];
            if (clip && (int)(rnd() % 100) < 6) gl = wild_glyph();   /* glitch */
            cell(fb, G, cx, cy, gl, g < 50 ? C_DIM : g > 200 ? C_ACC : C_FG);
        }

    if (clip) {   /* clipping: scatter runes over the whole screen */
        int k = clip > 40 ? 40 : clip;
        for (int i = 0; i < k; i++)
            cell(fb, G, (int)(rnd() % (unsigned)cols), (int)(rnd() % (unsigned)rows),
                 G_RUNE(rnd() & 255), C_ACC);
    }
}

/* ---- stereo helpers (5..7) ---------------------------------------------- */

#define ST_WIN      2048        /* samples per frame for the stereo views    */
#define ST_FLOOR    2048        /* min. auto-gain peak: silence is not zoomed */
#define ST_SIDE_MAX 8           /* side axis is zoomed at most 8x the mid axis */

/* follow a peak value: fast attack, slow release, never below `floor` */
static int follow(int cur, int pk, int floor)
{
    if (pk > cur) cur = pk;
    else          cur -= (cur - pk) / 16 + 1;
    return cur < floor ? floor : cur;
}

/* Update the auto-gain from the newest window. Returns the window start, or
 * -1 if the input is silent. mid = (L+R)/2 is vertical, side = (R-L)/2 is
 * horizontal. Each axis has its own gain so a narrow stereo image still fills
 * the screen; a pure mono signal (side = 0) stays a vertical line. */
static int stereo_window(const int16_t *l, const int16_t *r, int n)
{
    int st = n > ST_WIN ? n - ST_WIN : 0, pm = 0, ps = 0;
    for (int i = st; i < n; i++) {
        int m = ((int)l[i] + (int)r[i]) / 2, s = ((int)r[i] - (int)l[i]) / 2;
        if (m < 0) m = -m;
        if (s < 0) s = -s;
        if (m > pm) pm = m;
        if (s > ps) ps = s;
    }
    if (n <= 0 || (pm < TRIG_SILENCE && ps < TRIG_SILENCE)) return -1;
    s_gy = follow(s_gy, pm, ST_FLOOR);
    int fx = s_gy / ST_SIDE_MAX;
    s_gx = follow(s_gx, ps, fx < ST_FLOOR / 4 ? ST_FLOOR / 4 : fx);
    return st;
}

/* map sample i to a point in [-1,1]^2 (x = side, y = mid, up = +) scaled to 90% */
static void stereo_xy(const int16_t *l, const int16_t *r, int i, float *x, float *y)
{
    int m = ((int)l[i] + (int)r[i]) / 2, s = ((int)r[i] - (int)l[i]) / 2;
    *x = 0.9f * (float)s / (float)s_gx;
    *y = 0.9f * (float)m / (float)s_gy;
}

/* ---- 5: stereo dots (braille goniometer) ------------------------------- */

static void view_stereo_dots(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    static uint8_t dots[MAX_ROWS][MAX_COLS];
    static uint8_t hits[MAX_ROWS][MAX_COLS];
    int sw = G->cols * 2, sh = G->rows * 4;
    int st = stereo_window(l, r, n);
    if (st < 0) return;
    memset(dots, 0, sizeof(dots));
    memset(hits, 0, sizeof(hits));
    for (int i = st; i < n; i++) {
        float fx, fy;
        stereo_xy(l, r, i, &fx, &fy);
        int sx = (int)((fx + 1.0f) * 0.5f * (float)(sw - 1) + 0.5f);
        int sy = (int)((1.0f - fy) * 0.5f * (float)(sh - 1) + 0.5f);
        if (sx < 0 || sy < 0 || sx >= sw || sy >= sh) continue;
        dots[sy >> 2][sx >> 1] |= (uint8_t)(1u << ((sy & 3) * 2 + (sx & 1)));
        if (hits[sy >> 2][sx >> 1] < 255) hits[sy >> 2][sx >> 1]++;
    }
    for (int cy = 0; cy < G->rows; cy++)
        for (int cx = 0; cx < G->cols; cx++)
            if (dots[cy][cx])
                cell(fb, G, cx, cy, G_BRL(dots[cy][cx]),
                     hits[cy][cx] > 24 ? C_ACC : hits[cy][cx] > 3 ? C_FG : C_DIM);
}

/* ---- 6: stereo glow (goniometer density + afterglow) ------------------- */

static void view_stereo_glow(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    static const int RAMP[] = { '.', '\'', ':', '-', '+', '*', 'x', 'o', '#', '%',
                                '@', G_SHADE(1), G_SHADE(2), G_SHADE(3), G_SHADE(4) };
    const int nramp = (int)(sizeof(RAMP) / sizeof(RAMP[0]));
    int rows = G->rows, cols = G->cols;

    for (int cy = 0; cy < rows; cy++)
        for (int cx = 0; cx < cols; cx++)
            s_sglow[cy][cx] = (uint8_t)(s_sglow[cy][cx] * 12 / 16);

    int st = stereo_window(l, r, n);
    if (st >= 0)
        for (int i = st; i < n; i++) {
            float fx, fy;
            stereo_xy(l, r, i, &fx, &fy);
            int cx = (int)((fx + 1.0f) * 0.5f * (float)(cols - 1) + 0.5f);
            int cy = (int)((1.0f - fy) * 0.5f * (float)(rows - 1) + 0.5f);
            if (cx < 0 || cy < 0 || cx >= cols || cy >= rows) continue;
            int g = s_sglow[cy][cx] + 6;
            s_sglow[cy][cx] = (uint8_t)(g > 255 ? 255 : g);
        }

    for (int cy = 0; cy < rows; cy++)
        for (int cx = 0; cx < cols; cx++) {
            int g = s_sglow[cy][cx];
            if (g < 4) continue;
            int gl = RAMP[clampi(g * nramp / 256, 0, nramp - 1)];
            if (g > 230 && (rnd() & 7) == 0) gl = wild_glyph();   /* hot spots sparkle */
            cell(fb, G, cx, cy, gl, g < 40 ? C_DIM : g > 180 ? C_ACC : C_FG);
        }
}

/* ---- 7: stereo field (polar fan of rays) ------------------------------- */
/* Angle = stereo position (left .. centre .. right), length = level. Rays
 * start at the bottom centre and fan out over a half ellipse. */

static void view_stereo_field(u16 *fb, const Grid *G, const int16_t *l, const int16_t *r, int n)
{
    static uint8_t used[MAX_ROWS][MAX_COLS];
    const float PI = 3.14159265f;
    int rays = G->cols / 2 + 1;                 /* one ray per ~2 columns */
    if (rays > MAX_COLS) rays = MAX_COLS;
    float hit_n[MAX_COLS] = { 0 }, len_n[MAX_COLS] = { 0 };

    int st = stereo_window(l, r, n);
    if (st >= 0) {
        float gr = (float)(s_gx > s_gy ? s_gx : s_gy);
        for (int i = st; i < n; i += 2) {
            float m = (float)(((int)l[i] + (int)r[i]) / 2);
            float s = (float)(((int)r[i] - (int)l[i]) / 2);
            float rad = sqrtf(m * m + s * s) / gr;
            if (rad < 0.02f) continue;
            float a = atan2f(s, m < 0 ? -m : m);           /* -pi/2 (L) .. +pi/2 (R) */
            int b = (int)((a / PI + 0.5f) * (float)(rays - 1) + 0.5f);
            b = clampi(b, 0, rays - 1);
            hit_n[b] += 1.0f;
            if (rad > len_n[b]) len_n[b] = rad;
        }
    }
    for (int b = 0; b < rays; b++) {
        float len = len_n[b] > 1.0f ? 1.0f : len_n[b];
        s_ray[b] = len > s_ray[b] * 0.88f ? len : s_ray[b] * 0.88f;   /* hold + fall */
        int h = (int)(hit_n[b] * 2.0f);
        h = h > s_rayhit[b] ? h : s_rayhit[b] * 7 / 8;
        s_rayhit[b] = (uint8_t)(h > 255 ? 255 : h);
    }

    memset(used, 0, sizeof(used));
    float ox = (float)(G->cols * CW) * 0.5f;        /* origin in grid pixels */
    float oy = (float)(G->rows * CH) - 1.0f;
    float rx = ox * 0.95f, ry = oy * 0.95f;
    for (int b = 0; b < rays; b++) {
        if (s_ray[b] < 0.03f) continue;
        float a  = ((float)b / (float)(rays - 1) - 0.5f) * PI;
        float dx = sinf(a) * rx, dy = -cosf(a) * ry;  /* full-length ray vector */
        /* line glyph from the on-screen direction */
        float sa = atan2f(-dy, dx) * 180.0f / PI;     /* 0 = right, 90 = up */
        int g = sa > 67.5f && sa < 112.5f ? '|' : sa >= 112.5f && sa < 157.5f ? '\\'
              : sa > 22.5f && sa <= 67.5f ? '/' : '-';
        Col c = s_rayhit[b] > 120 ? C_ACC : s_rayhit[b] > 12 ? C_FG : C_DIM;
        int steps = (int)(s_ray[b] * (fabsf(dx) / CW > fabsf(dy) / CH
                                      ? fabsf(dx) / CW : fabsf(dy) / CH) * 2.0f) + 1;
        int tx = -1, ty = -1;
        for (int k = 1; k <= steps; k++) {
            float t = s_ray[b] * (float)k / (float)steps;
            int cx = (int)((ox + dx * t) / CW), cy = (int)((oy + dy * t) / CH);
            if (cx < 0 || cy < 0 || cx >= G->cols || cy >= G->rows) break;
            tx = cx; ty = cy;
            if (used[cy][cx]) continue;
            used[cy][cx] = 1;
            cell(fb, G, cx, cy, g, c);
        }
        if (tx >= 0) {                                /* sparkling tip */
            draw_rect_filled(fb, G->x0 + tx * CW, G->y0 + ty * CH, CW, CH, TH.bg);
            cell(fb, G, tx, ty, wild_glyph(), C_ACC);
        }
    }
    if (st >= 0) cell(fb, G, G->cols / 2, G->rows - 1, G_BOX(BX_DIAMOND), C_ACC);
}

/* ---- public ------------------------------------------------------------- */

void scope_ascii_draw(u16 *fb, int x, int y, int w, int h, int v,
                      const int16_t *l, const int16_t *r, int n)
{
    Grid G;
    G.cols = clampi(w / CW, 0, MAX_COLS);
    G.rows = clampi(h / CH, 0, MAX_ROWS);
    if (G.cols < 2 || G.rows < 2) return;
    G.x0 = x + (w - G.cols * CW) / 2;
    G.y0 = y + (h - G.rows * CH) / 2;
    if (!l || !r) n = 0;

    switch (v) {
        case ASCII_RAIN:     view_rain(fb, &G, l, r, n);     break;
        case ASCII_SCOPE:    view_scope(fb, &G, l, r, n);    break;
        case ASCII_BRAILLE:  view_braille(fb, &G, l, r, n);  break;
        case ASCII_PHOSPHOR: view_phosphor(fb, &G, l, r, n); break;
        case ASCII_STEREO_DOTS:  view_stereo_dots(fb, &G, l, r, n);  break;
        case ASCII_STEREO_GLOW:  view_stereo_glow(fb, &G, l, r, n);  break;
        case ASCII_STEREO_FIELD: view_stereo_field(fb, &G, l, r, n); break;
        default:             view_wave(fb, &G, l, r, n);     break;
    }
}

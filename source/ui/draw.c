/* draw.c - pixel drawing primitives for the top screen (see draw.h) */

#include "draw.h"
#include "font5x8.h"

#include <stdarg.h>
#include <stdio.h>

static inline void put(u16 *fb, int x, int y, u16 color)
{
    fb[x * TOP_H + (TOP_H - 1 - y)] = color;
}

void draw_clear(u16 *fb, u16 color)
{
    if (!fb) return;
    for (int i = 0; i < TOP_W * TOP_H; i++) fb[i] = color;
}

void draw_pixel(u16 *fb, int x, int y, u16 color)
{
    if (!fb || x < 0 || y < 0 || x >= TOP_W || y >= TOP_H) return;
    put(fb, x, y, color);
}

void draw_hline(u16 *fb, int x, int y, int w, u16 color)
{
    if (!fb || y < 0 || y >= TOP_H) return;
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w > TOP_W ? TOP_W : x + w;
    for (int i = x0; i < x1; i++) put(fb, i, y, color);
}

void draw_vline(u16 *fb, int x, int y, int h, u16 color)
{
    if (!fb || x < 0 || x >= TOP_W) return;
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h > TOP_H ? TOP_H : y + h;
    /* one column is contiguous in memory (y inverted) */
    u16 *col = fb + x * TOP_H;
    for (int j = y0; j < y1; j++) col[TOP_H - 1 - j] = color;
}

void draw_rect(u16 *fb, int x, int y, int w, int h, u16 color)
{
    if (w <= 0 || h <= 0) return;
    draw_hline(fb, x, y, w, color);
    draw_hline(fb, x, y + h - 1, w, color);
    draw_vline(fb, x, y, h, color);
    draw_vline(fb, x + w - 1, y, h, color);
}

void draw_rect_filled(u16 *fb, int x, int y, int w, int h, u16 color)
{
    for (int i = 0; i < w; i++) draw_vline(fb, x + i, y, h, color);
}

void draw_char(u16 *fb, int x, int y, char c, u16 fg, u16 bg)
{
    unsigned char uc = (unsigned char)c;
    if (uc < FONT5X8_FIRST || uc > FONT5X8_LAST) uc = '?';
    const uint8_t *g = FONT5X8[uc - FONT5X8_FIRST];

    for (int col = 0; col < FONT_ADV_X; col++) {
        uint8_t bits = (col < FONT_W) ? g[col] : 0;
        for (int row = 0; row < FONT_ADV_Y; row++) {
            bool on = (row < 8) && ((bits >> row) & 1);
            draw_pixel(fb, x + col, y + row, on ? fg : bg);
        }
    }
}

int draw_string(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg)
{
    if (!s) return x;
    for (; *s; s++) {
        if (x >= TOP_W) break;
        draw_char(fb, x, y, *s, fg, bg);
        x += FONT_ADV_X;
    }
    return x;
}

int draw_stringf(u16 *fb, int x, int y, u16 fg, u16 bg, const char *fmt, ...)
{
    char buf[96];   /* 400 px / 6 px = 66 characters max per line */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return draw_string(fb, x, y, buf, fg, bg);
}

/* Triangle 4 px wide, 7 px tall, centred in a 6x9 cell */
void draw_arrow_left(u16 *fb, int x, int y, u16 fg, u16 bg)
{
    draw_rect_filled(fb, x, y, FONT_ADV_X, FONT_ADV_Y, bg);
    for (int i = 0; i < 4; i++)
        draw_vline(fb, x + 1 + i, y + 3 - i, 1 + 2 * i, fg);
}

void draw_arrow_right(u16 *fb, int x, int y, u16 fg, u16 bg)
{
    draw_rect_filled(fb, x, y, FONT_ADV_X, FONT_ADV_Y, bg);
    for (int i = 0; i < 4; i++)
        draw_vline(fb, x + 4 - i, y + 3 - i, 1 + 2 * i, fg);
}

/* ---- bottom screen (320x240, same column stride as the top screen) ------- */

static inline void put_bot(u16 *fb, int x, int y, u16 color)
{
    if (x < 0 || y < 0 || x >= BOT_W || y >= TOP_H) return;
    fb[x * TOP_H + (TOP_H - 1 - y)] = color;
}

void draw_clear_bot(u16 *fb, u16 color)
{
    if (!fb) return;
    for (int i = 0; i < BOT_W * TOP_H; i++) fb[i] = color;
}

void draw_fill_bot(u16 *fb, int x, int y, int w, int h, u16 color)
{
    if (!fb || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x, x1 = x + w > BOT_W ? BOT_W : x + w;
    int y0 = y < 0 ? 0 : y, y1 = y + h > TOP_H ? TOP_H : y + h;
    for (int i = x0; i < x1; i++) {
        u16 *col = fb + i * TOP_H;
        for (int j = y0; j < y1; j++) col[TOP_H - 1 - j] = color;
    }
}

static void draw_char_bot(u16 *fb, int x, int y, char c, u16 fg, u16 bg)
{
    unsigned char uc = (unsigned char)c;
    if (uc < FONT5X8_FIRST || uc > FONT5X8_LAST) uc = '?';
    const uint8_t *g = FONT5X8[uc - FONT5X8_FIRST];

    for (int col = 0; col < FONT_ADV_X; col++) {
        uint8_t bits = (col < FONT_W) ? g[col] : 0;
        for (int row = 0; row < FONT_ADV_Y; row++) {
            bool on = (row < 8) && ((bits >> row) & 1);
            put_bot(fb, x + col, y + row, on ? fg : bg);
        }
    }
}

int draw_string_bot(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg)
{
    if (!fb || !s) return x;
    for (; *s; s++) {
        if (x >= BOT_W) break;
        draw_char_bot(fb, x, y, *s, fg, bg);
        x += FONT_ADV_X;
    }
    return x;
}

int draw_stringf_bot(u16 *fb, int x, int y, u16 fg, u16 bg, const char *fmt, ...)
{
    char buf[64];   /* 320 px / 6 px = 53 characters max per line */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return draw_string_bot(fb, x, y, buf, fg, bg);
}

#pragma once
/* draw.h - pixel drawing primitives for the top screen
 *
 * Top screen: 400x240, RGB565 (u16), column-major and y-inverted:
 *   fb[x * TOP_H + (TOP_H - 1 - y)] = color
 * The framebuffer must be set up with gfxSetScreenFormat(GFX_TOP,
 * GSP_RGB565_OES). All functions clip to the screen bounds.
 */

#include <3ds.h>

#define TOP_W 400
#define TOP_H 240

/* 8-bit RGB -> RGB565 */
#define RGB565(r, g, b) \
    ((u16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

void draw_clear(u16 *fb, u16 color);
void draw_pixel(u16 *fb, int x, int y, u16 color);
void draw_hline(u16 *fb, int x, int y, int w, u16 color);
void draw_vline(u16 *fb, int x, int y, int h, u16 color);
void draw_rect(u16 *fb, int x, int y, int w, int h, u16 color);
void draw_rect_filled(u16 *fb, int x, int y, int w, int h, u16 color);

/* Text: one character cell is FONT_ADV_X x FONT_ADV_Y pixels; the cell
 * (including the gap) is filled with bg. Non-printable chars render as '?'. */
void draw_char(u16 *fb, int x, int y, char c, u16 fg, u16 bg);
/* Returns the x position after the last character. */
int  draw_string(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg);
int  draw_stringf(u16 *fb, int x, int y, u16 fg, u16 bg, const char *fmt, ...)
     __attribute__((format(printf, 6, 7)));

/* Small filled triangles (used for the scroll-list arrows). The glyph
 * occupies one character cell at (x, y). */
void draw_arrow_left(u16 *fb, int x, int y, u16 fg, u16 bg);
void draw_arrow_right(u16 *fb, int x, int y, u16 fg, u16 bg);

/* ---- Bottom-screen text ------------------------------------------------- *
 * The bottom screen is 320×240 (BOT_W × TOP_H). Same column-major stride as  *
 * the top screen (each column is TOP_H pixels, y-inverted). Clipping is      *
 * enforced at BOT_W to prevent framebuffer overruns.                          */
#define BOT_W 320
void draw_clear_bot(u16 *fb, u16 color);
void draw_fill_bot(u16 *fb, int x, int y, int w, int h, u16 color);
int  draw_string_bot(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg);
int  draw_stringf_bot(u16 *fb, int x, int y, u16 fg, u16 bg, const char *fmt, ...)
     __attribute__((format(printf, 6, 7)));

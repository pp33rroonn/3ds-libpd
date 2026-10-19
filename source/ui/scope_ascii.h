#pragma once
/* scope_ascii.h - text-mode ("ASCII art") audio views for scope_view.c
 *
 * The drawing area is split into 6x9 px character cells (FONT_ADV_X x
 * FONT_ADV_Y). Besides the 5x7 ASCII font, a procedural extended glyph set
 * is used: shade blocks, eighth blocks, braille dots, box/line drawing and
 * random "rune" glyphs.
 *
 *   ASCII_WAVE    : scrolling waveform (min/max per frame), blocks + ramp
 *   ASCII_RAIN    : scrolling waveform made of flickering random glyphs
 *   ASCII_SCOPE   : triggered oscilloscope with _ - ` / \ | characters
 *   ASCII_BRAILLE : triggered oscilloscope in braille dots (2x4 per cell)
 *   ASCII_PHOSPHOR: triggered oscilloscope with afterglow + glitch on clip
 */

#include <3ds.h>
#include <stdint.h>

typedef enum {
    ASCII_WAVE = 0,
    ASCII_RAIN,
    ASCII_SCOPE,
    ASCII_BRAILLE,
    ASCII_PHOSPHOR,
    ASCII_VIEW_COUNT
} AsciiView;

/* Draw view `v` into the inner rectangle (x, y, w, h). l / r hold n frames,
 * oldest first. Call only while visible (WAVE / RAIN / PHOSPHOR keep state). */
void scope_ascii_draw(u16 *fb, int x, int y, int w, int h, int v,
                      const int16_t *l, const int16_t *r, int n);

/* Forget history / afterglow. */
void scope_ascii_reset(void);

#pragma once
/* theme.h - colour schemes for the pixel UI
 *
 * 18 schemes, selected via g_theme_idx (OPTIONS -> Eye).
 * Use TH.fg / TH.bg / TH.accent / TH.dim when drawing. The last scheme
 * ("RGB") colours the foreground by screen row; use theme_fg_at(y) for
 * text and lines so that it is handled automatically.
 */

#include <3ds.h>

#define THEME_COUNT 18
#define THEME_RGB   17   /* index of the rainbow scheme */

typedef struct {
    u16 fg;
    u16 bg;
    u16 accent;  /* highlight / selected row  */
    u16 dim;     /* grayed-out / empty slots  */
    const char *name;
} Theme;

extern const Theme g_themes[THEME_COUNT];
extern int g_theme_idx;   /* default 0 */

#define TH (g_themes[g_theme_idx])

/* Rainbow colour for screen row y (HSV: H = y * 360 / 240, S = V = 1). */
u16 theme_rgb_fg(int y);

/* Effective foreground colour at screen row y (rainbow for "RGB"). */
u16 theme_fg_at(int y);

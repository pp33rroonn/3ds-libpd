/* theme.c - colour schemes for the pixel UI (see theme.h) */

#include "theme.h"
#include "draw.h"

#define C_BLACK   RGB565(  0,   0,   0)
#define C_WHITE   RGB565(255, 255, 255)
#define C_GREEN   RGB565(  0, 255,  64)
#define C_AMBER   RGB565(255, 176,   0)
#define C_CYAN    RGB565(  0, 255, 255)
#define C_PURPLE  RGB565(110,   0, 150)
#define C_YELLOW  RGB565(255, 255,   0)
#define C_RED     RGB565(255,  40,  40)
#define C_ORANGE  RGB565(255, 128,   0)
#define C_LAV     RGB565(200, 180, 255)
#define C_DKBLUE  RGB565(  0,  40, 120)
#define C_ICE     RGB565(200, 235, 255)
#define C_DKBROWN RGB565( 90,  50,  10)
#define C_LTYEL   RGB565(255, 250, 200)
#define C_SOFTCY  RGB565(120, 220, 230)
#define C_NIGHT   RGB565(  5,  10,  35)

const Theme g_themes[THEME_COUNT] = {
    /*  fg         bg         accent                  dim                     name */
    { C_BLACK,   C_WHITE,   RGB565(  0,   0, 200), RGB565(140, 140, 140), "B/W" },
    { C_WHITE,   C_BLACK,   C_YELLOW,              RGB565(100, 100, 100), "W/B" },
    { C_GREEN,   C_BLACK,   C_WHITE,               RGB565(  0, 110,  30), "G/B" },
    { C_BLACK,   C_GREEN,   RGB565(  0,   0, 160), RGB565(  0, 120,  30), "B/G" },
    { C_AMBER,   C_BLACK,   C_WHITE,               RGB565(120,  80,   0), "A/B" },
    { C_BLACK,   C_AMBER,   RGB565(128,   0,   0), RGB565(150, 100,   0), "B/A" },
    { C_CYAN,    C_BLACK,   C_WHITE,               RGB565(  0, 110, 110), "C/B" },
    { C_BLACK,   C_CYAN,    RGB565(  0,   0, 160), RGB565(  0, 140, 140), "B/C" },
    { C_PURPLE,  C_WHITE,   RGB565(200,   0, 120), RGB565(180, 160, 190), "P/W" },
    { C_WHITE,   C_PURPLE,  C_YELLOW,              RGB565(170, 120, 200), "W/P" },
    { C_YELLOW,  C_BLACK,   C_WHITE,               RGB565(120, 120,   0), "Y/B" },
    { C_RED,     C_BLACK,   C_WHITE,               RGB565(120,   0,   0), "R/B" },
    { C_ORANGE,  C_BLACK,   C_WHITE,               RGB565(120,  60,   0), "O/B" },
    { C_BLACK,   C_LAV,     RGB565( 90,   0, 160), RGB565(130, 110, 170), "Lav" },
    { C_DKBLUE,  C_ICE,     RGB565(  0, 120, 200), RGB565(120, 160, 190), "Ice" },
    { C_DKBROWN, C_LTYEL,   RGB565(200,  90,   0), RGB565(180, 160, 110), "Sun" },
    { C_SOFTCY,  C_NIGHT,   C_WHITE,               RGB565( 50,  90, 110), "Ngt" },
    /* RGB: fg is computed per row by theme_fg_at(); White is the fallback */
    { C_WHITE,   C_BLACK,   C_WHITE,               RGB565(100, 100, 100), "RGB" },
};

int g_theme_idx = 0;

u16 theme_rgb_fg(int y)
{
    if (y < 0) y = 0;
    if (y >= TOP_H) y = TOP_H - 1;

    /* HSV -> RGB with S = V = 1; hue in degrees 0..359 */
    int h = y * 360 / TOP_H;
    int sector = h / 60;
    int f = (h % 60) * 255 / 60;   /* 0..255 within the sector */
    int r, g, b;
    switch (sector) {
        case 0:  r = 255;     g = f;       b = 0;       break;
        case 1:  r = 255 - f; g = 255;     b = 0;       break;
        case 2:  r = 0;       g = 255;     b = f;       break;
        case 3:  r = 0;       g = 255 - f; b = 255;     break;
        case 4:  r = f;       g = 0;       b = 255;     break;
        default: r = 255;     g = 0;       b = 255 - f; break;
    }
    return RGB565(r, g, b);
}

u16 theme_fg_at(int y)
{
    return (g_theme_idx == THEME_RGB) ? theme_rgb_fg(y) : TH.fg;
}

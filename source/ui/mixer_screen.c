/* mixer_screen.c - pixel mixer on the bottom screen (see mixer_screen.h)
 *
 * draw.c clips to the 400 px top screen, so this file uses its own
 * primitives clipped to 320 px to never write past the bottom framebuffer.
 */

#include "mixer_screen.h"
#include "font5x8.h"
#include "theme.h"
#include "../pd_bridge/pd_bridge.h"

#include <stdio.h>

#define HEADER_H   15
#define STRIP_Y0   15
#define STRIP_H    40
#define NAME_H     18
#define FOOTER_Y   215

#define FADER_X    20
#define FADER_W    280
#define FADER_PAD  2            /* bar inset inside the 20 px fader area */
#define VOL_X      302
#define PEAK_H     4

/* slot whose fader is being dragged; -1 = no touch, -2 = touch not on a fader */
static int s_grab = -1;

/* ---- bottom-screen primitives (clipped to 320x240) ------------------------ */

static inline void put(u16 *fb, int x, int y, u16 c)
{
    if (x < 0 || y < 0 || x >= MIXER_W || y >= MIXER_H) return;
    fb[x * MIXER_H + (MIXER_H - 1 - y)] = c;
}

static void fill(u16 *fb, int x, int y, int w, int h, u16 c)
{
    for (int i = x; i < x + w; i++)
        for (int j = y; j < y + h; j++) put(fb, i, j, c);
}

static void bchar(u16 *fb, int x, int y, char ch, u16 fg, u16 bg)
{
    unsigned char uc = (unsigned char)ch;
    if (uc < FONT5X8_FIRST || uc > FONT5X8_LAST) uc = '?';
    const uint8_t *g = FONT5X8[uc - FONT5X8_FIRST];
    for (int col = 0; col < FONT_ADV_X; col++) {
        uint8_t bits = (col < FONT_W) ? g[col] : 0;
        for (int row = 0; row < FONT_ADV_Y; row++)
            put(fb, x + col, y + row, (row < 8 && ((bits >> row) & 1)) ? fg : bg);
    }
}

static int bstring(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg)
{
    for (; s && *s && x + FONT_W <= MIXER_W; s++, x += FONT_ADV_X)
        bchar(fb, x, y, *s, fg, bg);
    return x;
}

/* 50 % darker RGB565 colour */
static inline u16 darker(u16 c) { return (u16)((c >> 1) & 0x7BEF); }

/* ---- drawing ---------------------------------------------------------------- */

static void draw_strip(u16 *fb, int slot, bool active, int peak)
{
    const Theme *t = theme_get_current();
    int  y0     = STRIP_Y0 + slot * STRIP_H;
    bool loaded = pd_slot_is_loaded(slot);

    u16 bg   = active ? t->accent : t->bg;
    u16 text = active ? t->bg : (loaded ? theme_fg_at(y0) : t->dim);
    fill(fb, 0, y0, MIXER_W, STRIP_H, bg);

    /* name row */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d:", slot + 1);
    int x = bstring(fb, 4, y0 + 5, buf, text, bg) + FONT_ADV_X;
    if (loaded) {
        snprintf(buf, sizeof(buf), "%.18s", pd_slot_display_name(slot));
        bstring(fb, x, y0 + 5, buf, text, bg);
    } else {
        bstring(fb, x, y0 + 5, "Empty", active ? text : t->dim, bg);
    }

    /* fader */
    int fy = y0 + NAME_H + FADER_PAD;
    int fh = STRIP_H - NAME_H - 2 * FADER_PAD;
    if (!loaded) {
        fill(fb, FADER_X, fy, FADER_W, fh, darker(t->dim));
        bstring(fb, VOL_X, fy + (fh - FONT_H) / 2, "--", active ? text : t->dim, bg);
        return;
    }

    float vol = pd_slot_get_vol(slot);
    int filled = (int)(vol * FADER_W + 0.5f);
    u16 on  = active ? t->bg : theme_fg_at(fy);
    u16 off = (u16)(darker(on) + darker(bg));   /* 50 % mix with the strip bg */
    fill(fb, FADER_X, fy, filled, fh, on);
    fill(fb, FADER_X + filled, fy, FADER_W - filled, fh, off);

    /* peak meter (master output), centred in the fader bar */
    int pw = peak * FADER_W / 32767;
    if (pw > FADER_W) pw = FADER_W;
    fill(fb, FADER_X, fy + (fh - PEAK_H) / 2, pw, PEAK_H,
         active ? t->fg : t->accent);

    snprintf(buf, sizeof(buf), "%3d", (int)(vol * 100.0f + 0.5f));
    bstring(fb, VOL_X, fy + (fh - FONT_H) / 2, buf, text, bg);
}

void mixer_screen_draw(u16 *fb, int active_slot, int dsp_pct,
                       int peak_l, int peak_r)
{
    if (!fb) return;
    const Theme *t = theme_get_current();
    char buf[16];

    /* header */
    fill(fb, 0, 0, MIXER_W, HEADER_H, t->bg);
    bstring(fb, 4, 3, "MIXER", theme_fg_at(3), t->bg);
    snprintf(buf, sizeof(buf), "DSP:%d%%", dsp_pct);
    int len = 0;
    while (buf[len]) len++;
    bstring(fb, MIXER_W - 4 - len * FONT_ADV_X, 3, buf, theme_fg_at(3), t->bg);
    fill(fb, 0, HEADER_H - 1, MIXER_W, 1, t->dim);

    int peak = peak_l > peak_r ? peak_l : peak_r;
    if (peak < 0) peak = 0;
    for (int i = 0; i < PD_SLOT_COUNT; i++)
        draw_strip(fb, i, i == active_slot, peak);

    /* footer */
    fill(fb, 0, FOOTER_Y, MIXER_W, MIXER_H - FOOTER_Y, t->bg);
    fill(fb, 0, FOOTER_Y, MIXER_W, 1, t->dim);
    bstring(fb, 4, FOOTER_Y + 9, "A:load  START:opts  R:ctrl", t->dim, t->bg);
}

/* ---- touch -------------------------------------------------------------------- */

static int fader_hit(int x, int y)
{
    if (x < FADER_X || x >= FADER_X + FADER_W) return -1;
    for (int i = 0; i < PD_SLOT_COUNT; i++) {
        int fy = STRIP_Y0 + i * STRIP_H + (STRIP_H - 20);
        if (y >= fy && y < fy + 20) return i;
    }
    return -1;
}

bool mixer_screen_handle_touch(int x, int y, bool touching)
{
    if (!touching) {
        s_grab = -1;
        return false;
    }
    if (s_grab == -1) {                 /* touch just started */
        int hit = fader_hit(x, y);
        s_grab = (hit >= 0 && pd_slot_is_loaded(hit)) ? hit : -2;
    }
    if (s_grab < 0) return false;       /* gesture belongs to PD */
    if (!pd_slot_is_loaded(s_grab)) {   /* unloaded while dragging */
        s_grab = -2;
        return false;
    }
    float v = (float)(x - FADER_X) / (float)FADER_W;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    pd_slot_set_vol(s_grab, v);
    return true;
}

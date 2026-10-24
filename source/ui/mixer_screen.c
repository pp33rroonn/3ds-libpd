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
#include <string.h>

#define HEADER_H   15
#define FOOTER_Y   239          /* columns fill down to the screen edge */
#define COL_W      64           /* 5 columns x 64 px = 320 */
#define NAME_Y     19
#define FADER_TOP  32
#define FADER_BOT  224          /* exclusive - extended toward the edge */
#define FADER_H    (FADER_BOT - FADER_TOP)
#define FADER_DX   18           /* fader x offset inside a column */
#define FADER_W    22
#define METER_DX   44           /* level meter x offset */
#define METER_W    6
#define VOL_Y      228          /* value row just below the longer fader */
#define TOUCH_DX0  6            /* generous touch area per column */
#define TOUCH_DX1  58
#define TOUCH_Y0   26
#define TOUCH_Y1   234

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

static void draw_column(u16 *fb, int slot)
{
    const Theme *t = theme_get_current();
    int  x0     = slot * COL_W;
    bool loaded = pd_slot_is_loaded(slot);
    u16  text   = loaded ? theme_fg_at(NAME_Y) : t->dim;
    char buf[16];

    fill(fb, x0, HEADER_H, COL_W, FOOTER_Y - HEADER_H, t->bg);
    if (slot > 0) fill(fb, x0, HEADER_H, 1, FOOTER_Y - HEADER_H, t->dim);

    /* "N" + name (max 8 chars) */
    snprintf(buf, sizeof(buf), "%d", slot + 1);
    bstring(fb, x0 + 3, NAME_Y - 2, buf, t->accent, t->bg);
    snprintf(buf, sizeof(buf), "%.8s", loaded ? pd_slot_display_name(slot) : "Empty");
    bstring(fb, x0 + 12, NAME_Y - 2, buf, text, t->bg);

    int fx = x0 + FADER_DX;
    if (!loaded) {
        fill(fb, fx, FADER_TOP, FADER_W, FADER_H, darker(t->dim));
        bstring(fb, x0 + 26, VOL_Y, "--", t->dim, t->bg);
        return;
    }

    /* fader: filled from the bottom up to the volume */
    float vol    = pd_slot_get_vol(slot);
    int   filled = (int)(vol * FADER_H + 0.5f);
    int   fy     = FADER_BOT - filled;
    for (int y = FADER_TOP; y < FADER_BOT; y++) {
        fill(fb, fx, y, FADER_W, 1, y >= fy ? theme_fg_at(y) : t->dim);
    }
    /* handle line at the current value */
    fill(fb, fx - 3, fy - 1 < FADER_TOP ? FADER_TOP : fy - 1, FADER_W + 6, 2, t->accent);

    /* per-slot level meter (post fader) */
    int lh = (int)(pd_slot_get_level(slot) * FADER_H + 0.5f);
    if (lh > FADER_H) lh = FADER_H;
    fill(fb, x0 + METER_DX, FADER_TOP, METER_W, FADER_H - lh, darker(darker(t->dim)));
    fill(fb, x0 + METER_DX, FADER_BOT - lh, METER_W, lh, t->accent);

    snprintf(buf, sizeof(buf), "%3d", (int)(vol * 100.0f + 0.5f));
    bstring(fb, x0 + 20, VOL_Y, buf, text, t->bg);
}

void mixer_screen_draw(u16 *fb, int dsp_pct)
{
    if (!fb) return;
    const Theme *t = theme_get_current();

    (void)dsp_pct;   /* DSP readout removed from the mixer */

    /* Clear the WHOLE screen first. The columns only paint y 15..215 and the
     * header y 0..15, leaving y 215..240 unpainted -> a black bar at the bottom.
     * With double buffering each back buffer kept its own stale/black content,
     * which is what flickered when switching patches. A full clear every frame
     * fixes both. */
    fill(fb, 0, 0, MIXER_W, MIXER_H, t->bg);

    /* header */
    fill(fb, 0, 0, MIXER_W, HEADER_H, t->bg);
    bstring(fb, 4, 3, "MIXER", theme_fg_at(3), t->bg);
    fill(fb, 0, HEADER_H - 1, MIXER_W, 1, t->dim);

    for (int i = 0; i < PD_SLOT_COUNT; i++) draw_column(fb, i);
}

/* ---- touch -------------------------------------------------------------------- */

static int fader_hit(int x, int y)
{
    if (y < TOUCH_Y0 || y >= TOUCH_Y1 || x < 0 || x >= MIXER_W) return -1;
    int col = x / COL_W, dx = x % COL_W;
    if (col >= PD_SLOT_COUNT || dx < TOUCH_DX0 || dx >= TOUCH_DX1) return -1;
    return col;
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
    (void)x;
    float v = (float)(FADER_BOT - y) / (float)FADER_H;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    pd_slot_set_vol(s_grab, v);
    return true;
}

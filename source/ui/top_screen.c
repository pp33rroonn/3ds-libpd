/* top_screen.c - page dispatcher for the pixel top screen (see top_screen.h) */

#include "top_screen.h"
#include "draw.h"
#include "font5x8.h"
#include "theme.h"
#include "pd_log.h"
#include "scope_view.h"
#include "../pd_bridge/pd_bridge.h"

#include <stdio.h>
#include <string.h>

#define X0        6
#define Y_HEADER  4
#define Y_SEP     16
#define Y_BODY    22
#define LINE_H    12
#define Y_HELP    (TOP_H - FONT_ADV_Y - 4)

AppPage g_app_page = PAGE_WAVEFORM;

static char s_status[64] = "";
static bool s_mic_on = false;   /* Mikrofon-Indikator im Header */
static bool s_cam_on = false;   /* Kamera-Indikator im Header   */
static int  s_peak_l = 0, s_peak_r = 0;
static const int16_t *s_scope_l = NULL, *s_scope_r = NULL;
static int  s_scope_n = 0;

void top_screen_set_mic(bool on)
{
    s_mic_on = on;
}

void top_screen_set_cam(bool on)
{
    s_cam_on = on;
}

void top_screen_set_status(const char *msg)
{
    snprintf(s_status, sizeof(s_status), "%s", msg ? msg : "");
}

void top_screen_set_peak(int l, int r)
{
    s_peak_l = l;
    s_peak_r = r;
}

void top_screen_set_scope(const int16_t *l, const int16_t *r, int n)
{
    s_scope_l = l;
    s_scope_r = r;
    s_scope_n = (l && r && n > 0) ? n : 0;
}

/* ---- navigation ------------------------------------------------------------ */

/* Pages of the ZL / ZR cycle: Waveform + patch slots that hold a patch.
 * PAGE_PD_LOG is deliberately excluded (ZL+ZR combo only). */
#define CYCLE_COUNT (PAGE_PATCH_4 + 1)

static bool page_in_cycle(const OptionsMenu *m, int p)
{
    if (p == PAGE_WAVEFORM) return true;
    if (p >= PAGE_PATCH_0 && p <= PAGE_PATCH_4)
        return options_menu_patch_name(m, p - PAGE_PATCH_0) != NULL;
    return false;
}

static AppPage step_page(const OptionsMenu *m, int dir)
{
    int p = (int)g_app_page;
    if (p < 0 || p >= CYCLE_COUNT) p = PAGE_WAVEFORM;   /* e.g. PD Log */
    for (int i = 0; i < CYCLE_COUNT; i++) {
        p = (p + dir + CYCLE_COUNT) % CYCLE_COUNT;
        if (page_in_cycle(m, p)) return (AppPage)p;
    }
    return PAGE_WAVEFORM;
}

int top_screen_get_active_slot(void)
{
    if (g_app_page >= PAGE_PATCH_0 && g_app_page <= PAGE_PATCH_4)
        return (int)g_app_page - PAGE_PATCH_0;
    return -1;
}

AppPage top_screen_next_page(const OptionsMenu *m) { return step_page(m, +1); }
AppPage top_screen_prev_page(const OptionsMenu *m) { return step_page(m, -1); }

/* ---- common parts ---------------------------------------------------------- */

static void draw_header(u16 *fb, const char *title)
{
    u16 bg = TH.bg;
    draw_string(fb, X0, Y_HEADER, title, theme_fg_at(Y_HEADER), bg);
    draw_string(fb, TOP_W - X0 - 9 * FONT_ADV_X, Y_HEADER, "ZL < > ZR",
                TH.dim, bg);
    /* Mikrofon aktiv: dezentes "MIC" in Akzentfarbe links der ZL/ZR-Anzeige */
    if (s_mic_on)
        draw_string(fb, TOP_W - X0 - 14 * FONT_ADV_X, Y_HEADER, "MIC",
                    TH.accent, bg);
    /* Kamera aktiv: dezentes "CAM" in Akzentfarbe links des MIC-Indikators */
    if (s_cam_on)
        draw_string(fb, TOP_W - X0 - 18 * FONT_ADV_X, Y_HEADER, "CAM",
                    TH.accent, bg);
    draw_hline(fb, 4, Y_SEP, TOP_W - 8, theme_fg_at(Y_SEP));
}

static void draw_help(u16 *fb)
{
    draw_hline(fb, 4, Y_HELP - 5, TOP_W - 8, theme_fg_at(Y_HELP - 5));
    draw_string(fb, X0, Y_HELP, "ZL/ZR: page  SELECT: Mixer  ZL+ZR: Log  START: options",
                TH.dim, TH.bg);
}

/* Framed placeholder area for views that arrive in later steps */
static void draw_placeholder(u16 *fb, int y, int h, const char *text)
{
    draw_rect(fb, X0, y, TOP_W - 2 * X0, h, TH.dim);
    int len = (int)strlen(text);
    int tx  = (TOP_W - len * FONT_ADV_X) / 2;
    draw_string(fb, tx, y + (h - FONT_ADV_Y) / 2, text, TH.dim, TH.bg);
}

/* ---- pages ---------------------------------------------------------------- */

static void draw_waveform(u16 *fb, const OptionsMenu *m, float bpm,
                          int dsp_pct, unsigned glitches)
{
    u16 bg = TH.bg;
    int y = Y_BODY;
    draw_header(fb, "pd_3ds - Waveform");

    const char *name = options_menu_patch_name(m, 0);
    if (name)
        draw_stringf(fb, X0, y, theme_fg_at(y), bg, "Patch: %.40s (%02d/%02d)",
                     name, m->patch_idx[0] + 1, options_menu_patch_count());
    else
        draw_string(fb, X0, y, "Patch: none", TH.dim, bg);
    y += LINE_H;

    draw_stringf(fb, X0, y, theme_fg_at(y), bg, "BPM: %.1f   Sync: %s",
                 (double)bpm, options_menu_sync_label(m));
    y += LINE_H;

    draw_stringf(fb, X0, y, theme_fg_at(y), bg,
                 "DSP: %d%%   Glitch: %u   Out L:%5d R:%5d%s", dsp_pct,
                 glitches, s_peak_l, s_peak_r,
                 (s_peak_l | s_peak_r) ? "" : " (silent)");
    y += LINE_H;

    if (s_status[0]) draw_string(fb, X0, y, s_status, TH.accent, bg);
    y += LINE_H + 2;

    /* audio view (OPTIONS -> SpecAn selects the mode) */
    scope_view_draw(fb, X0, y, TOP_W - 2 * X0, Y_HELP - 10 - y, m->specan_mode,
                    s_scope_l, s_scope_r, s_scope_n);
    draw_help(fb);
}

static void draw_patch_page(u16 *fb, const OptionsMenu *m, int slot)
{
    u16 bg = TH.bg;
    int y = Y_BODY;
    char title[32];
    snprintf(title, sizeof(title), "pd_3ds - Patch %d", slot + 1);
    draw_header(fb, title);

    const char *name = options_menu_patch_name(m, slot);
    if (name)
        draw_stringf(fb, X0, y, theme_fg_at(y), bg, "%.50s (%02d/%02d)", name,
                     m->patch_idx[slot] + 1, options_menu_patch_count());
    else
        draw_string(fb, X0, y, "Empty", TH.dim, bg);
    y += LINE_H;
    if (pd_slot_is_loaded(slot))
        draw_stringf(fb, X0, y, TH.dim, bg, "Running in slot %d   Vol: %d%%",
                     slot + 1, (int)(pd_slot_get_vol(slot) * 100.0f + 0.5f));
    else
        draw_string(fb, X0, y, "Not loaded", TH.dim, bg);
    y += LINE_H + 2;

    draw_placeholder(fb, y, Y_HELP - 10 - y,
                     "Patch view / waveform - coming in a later step");
    draw_help(fb);
}

static void draw_pd_log(u16 *fb)
{
    u16 bg = TH.bg;
    draw_header(fb, "pd_3ds - PD Log");

    int n = pd_log_count();
    if (n == 0) {
        draw_string(fb, X0, Y_BODY, "(no messages yet)", TH.dim, bg);
    }
    for (int i = 0; i < n && i < PD_LOG_LINES; i++) {
        int y = Y_BODY + i * FONT_ADV_Y;
        if (y + FONT_ADV_Y > Y_HELP - 6) break;
        const char *line = pd_log_get(i);   /* 0 = newest, on top */
        if (line) draw_string(fb, X0, y, line, theme_fg_at(y), bg);
    }
    draw_help(fb);
}

void top_screen_draw(u16 *fb, const OptionsMenu *m, float bpm,
                     int dsp_pct, unsigned glitches)
{
    if (!fb || !m) return;

    /* the selected patch slot may have been emptied in OPTIONS (PD Log is
     * not in the ZL / ZR cycle but is a valid page to show) */
    if (g_app_page != PAGE_PD_LOG && !page_in_cycle(m, g_app_page))
        g_app_page = PAGE_WAVEFORM;

    draw_clear(fb, TH.bg);
    switch (g_app_page) {
        case PAGE_PATCH_0: case PAGE_PATCH_1: case PAGE_PATCH_2:
        case PAGE_PATCH_3: case PAGE_PATCH_4:
            draw_patch_page(fb, m, (int)g_app_page - PAGE_PATCH_0);
            break;
        case PAGE_PD_LOG:
            draw_pd_log(fb);
            break;
        case PAGE_WAVEFORM:
        default:
            draw_waveform(fb, m, bpm, dsp_pct, glitches);
            break;
    }
}

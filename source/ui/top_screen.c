/* top_screen.c - page dispatcher for the pixel top screen (see top_screen.h) */

#include "top_screen.h"
#include "draw.h"
#include "font5x8.h"
#include "theme.h"
#include "pd_log.h"
#include "scope_view.h"
#include "cam_view.h"
#include "camtune.h"   /* live Cam Tune values for the [dbg] overlay */
#include "canvas_view.h"
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
static int  s_cam_mode = 0;     /* 0 Aus,1 Fader,2 Bild,3 Bild+Matrix */
static int  s_peak_l = 0, s_peak_r = 0;
static const int16_t *s_scope_l = NULL, *s_scope_r = NULL;
static int  s_scope_n = 0;
static const u16 *s_cam_frame = NULL;   /* raw cam frame or NULL (see setter) */

/* TEMPORARY camera capture diagnostics (remove once the cam image works). */
static bool     s_cam_dbg_active = false;
static unsigned s_cam_dbg_serial = 0;
static float    s_cam_dbg_bright = 0.0f;
static unsigned s_cam_dbg_err    = 0;
static unsigned s_cam_dbg_tu     = 0;   /* DMA transfer unit (GetMaxBytes)   */
static unsigned s_cam_dbg_rs     = 0;   /* stale-frame hard resync count     */

void top_screen_set_cam_frame(const u16 *frame)
{
    s_cam_frame = frame;
}

/* TEMPORARY: live camera diagnostics shown in the patch view (see header). */
void top_screen_set_cam_debug(bool active, unsigned serial, float bright,
                              unsigned err, unsigned transfer, unsigned resync)
{
    s_cam_dbg_active = active;
    s_cam_dbg_serial = serial;
    s_cam_dbg_bright = bright;
    s_cam_dbg_err    = err;
    s_cam_dbg_tu     = transfer;
    s_cam_dbg_rs     = resync;
}

void top_screen_set_mic(bool on)
{
    s_mic_on = on;
}

void top_screen_set_cam_mode(int mode)
{
    s_cam_mode = (mode < 0) ? 0 : (mode > 3 ? 3 : mode);
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
    /* Kamera aktiv: "CAM" + Moduszusatz in Akzentfarbe links des MIC-Indikators
     * (Fader = F, Bild = B, Bild+Matrix = BM) */
    if (s_cam_mode > 0) {
        const char *suffix = s_cam_mode == 1 ? "F"
                           : s_cam_mode == 2 ? "I" : "IM";
        char label[8];
        snprintf(label, sizeof(label), "CAM %s", suffix);
        int cols = 14 + 1 + (int)strlen(label);   /* left of the MIC slot */
        draw_string(fb, TOP_W - X0 - cols * FONT_ADV_X, Y_HEADER, label,
                    TH.accent, bg);
    }
    draw_hline(fb, 4, Y_SEP, TOP_W - 8, theme_fg_at(Y_SEP));
}

static void draw_help(u16 *fb)
{
    draw_hline(fb, 4, Y_HELP - 5, TOP_W - 8, theme_fg_at(Y_HELP - 5));
    draw_string(fb, X0, Y_HELP, "ZL/ZR: page  SELECT: Mixer  ZL+ZR: Log  START: options",
                TH.dim, TH.bg);
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

/* Header for the patch page: title left, CAM / MIC indicators far right (no
 * "ZL < > ZR" hint and no separator line, so the big frame can start higher). */
static void draw_patch_header(u16 *fb, const char *title)
{
    u16 bg = TH.bg;
    draw_string(fb, X0, Y_HEADER, title, theme_fg_at(Y_HEADER), bg);

    int rx = TOP_W - X0;
    if (s_mic_on) {
        rx -= 3 * FONT_ADV_X;
        draw_string(fb, rx, Y_HEADER, "MIC", TH.accent, bg);
        rx -= 2 * FONT_ADV_X;   /* gap before CAM */
    }
    if (s_cam_mode > 0) {
        const char *suffix = s_cam_mode == 1 ? "F"
                           : s_cam_mode == 2 ? "I" : "IM";
        char label[8];
        snprintf(label, sizeof(label), "CAM %s", suffix);
        rx -= (int)strlen(label) * FONT_ADV_X;
        draw_string(fb, rx, Y_HEADER, label, TH.accent, bg);
    }
}

static void draw_patch_page(u16 *fb, const OptionsMenu *m, int slot)
{
    u16 bg = TH.bg;
    char title[32];
    snprintf(title, sizeof(title), "pd_3ds - Patch %d", slot + 1);
    draw_patch_header(fb, title);

    /* Big frame over almost the whole screen (no help line, no header sep). */
    const int fx = X0;
    const int fy = Y_HEADER + FONT_ADV_Y + 3;       /* just under the title   */
    const int fw = TOP_W - 2 * X0;
    const int fh = TOP_H - fy - 4;                   /* all the way down       */
    u16 frame_col = (g_theme_idx == 0 || g_theme_idx == 1) ? TH.fg : TH.accent;
    draw_rect(fb, fx, fy, fw, fh, frame_col);

    /* Patch name inside the frame, top-left, in normal body font. */
    const char *name = options_menu_patch_name(m, slot);
    int name_x = fx + 8;
    int name_y = fy + 6;
    if (name)
        draw_stringf(fb, name_x, name_y, theme_fg_at(name_y), bg, "%.40s.pd", name);
    else
        draw_string(fb, name_x, name_y, "Empty", TH.dim, bg);

    /* Is this the camera patch? (filename contains "cam"). The image is only
     * shown for that patch and only in cam modes >= 2 ("Bild"/"Bild+Matrix"). */
    bool is_cam_patch = name && strstr(name, "cam") != NULL;
    bool show_cam = is_cam_patch && s_cam_mode >= 2 && s_cam_frame != NULL;

    int interior_top = name_y + FONT_ADV_Y + 4;
    int cx, cy, cw, ch;   /* canvas sub-rectangle */

    if (show_cam) {
        int img_ox = fx + 12;
        int img_oy = interior_top + CAM_VIEW_BORDER;
        cam_view_draw_top_scaled(fb, s_cam_frame, img_ox, img_oy,
                                 CAM_TOP_W, CAM_TOP_H);
        /* canvas fills the area to the right of the image */
        cx = img_ox + CAM_TOP_W + CAM_VIEW_BORDER + 14;
        cy = interior_top;
        cw = (fx + fw) - cx - 8;
        ch = (fy + fh) - cy - 8;
    } else {
        /* canvas fills the whole frame interior, under the patch name */
        cx = fx + 10;
        cy = interior_top;
        cw = fw - 20;
        ch = (fy + fh) - cy - 8;
    }

    /* TEMPORARY diagnostic line: reserve space at the bottom of the frame so
     * the canvas does not overlap it (remove once the cam image works). */
    ch -= FONT_ADV_Y + 6;
    if (ch < 0) ch = 0;

    canvas_view_draw(fb, cx, cy, cw, ch, theme_fg_at(cy), TH.dim);

    /* TEMPORARY on-screen camera diagnostics (remove later). The trailing Cam
     * Tune tokens read off the live fader values (0..99 = fader percent) so the
     * sweet-spot numbers can be noted from hardware:
     *   E = Exposure, H = Hold, T = Temporal, S = Spatial, U = Sharpen. */
    int dbg_y = fy + fh - FONT_ADV_Y - 4;
    draw_stringf(fb, fx + 8, dbg_y, TH.dim, bg,
                 "[dbg] a=%d ser=%u br=%d%% e=%u tu=%u rs=%u E%d H%d T%d S%d U%d",
                 s_cam_dbg_active ? 1 : 0, s_cam_dbg_serial,
                 (int)(s_cam_dbg_bright * 100.0f), s_cam_dbg_err,
                 s_cam_dbg_tu, s_cam_dbg_rs,
                 (int)(g_camtune_exposure * 99.0f + 0.5f),
                 (int)(g_camtune_hold     * 99.0f + 0.5f),
                 (int)(g_camtune_temporal * 99.0f + 0.5f),
                 (int)(g_camtune_spatial  * 99.0f + 0.5f),
                 (int)(g_camtune_sharpen  * 99.0f + 0.5f));
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

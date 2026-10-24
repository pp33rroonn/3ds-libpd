/* ctrl_panel.c - scrollbares Fader-Panel fuer den CTRL-Modus
 * (Layout und Bedienung siehe ctrl_panel.h)
 */

#include "ctrl_panel.h"
#include "../app_state.h"

#include <3ds.h>
#include "pd_bridge.h"   /* pd_input_float/bang: routed to the active slot */
#include "../ui/draw.h"
#include "../ui/font5x8.h"
#include "../ui/theme.h"
#include "../ui/camtune.h"   /* Cam Tune fader bank -> live C globals */
#include "../ui/cam_view.h"  /* g_cam_smooth + cam_view_stage_inspect() */
#include <stdio.h>
#include <string.h>

#define LAST_PANEL_ROW 30   /* hint line removed: panel owns the full height */
#define LINE_W         39   /* letzte Spalte frei lassen (kein Umbruch) */
#define FIRST_ROW       3

/* Horizontal: Eintrag = Namenszeile + 4 Zeilen Widget (24 px + Abstand) */
#define H_ENTRY_ROWS   5
#define H_X0           8    /* Widget x = 8..311 */
#define H_X1         311
#define H_WIDGET_H    24    /* doppelt so hoch wie vorher (12 px) */
#define H_WIDGET_DY    3    /* Abstand Unterkante Namenszeile -> Widget */

/* Vertikal: 8 Streifen a 40 px = 5 Konsolenspalten */
#define V_STRIP_W     40
#define V_STRIP_COLS   5
#define V_LABEL_ROW    3
#define V_VALUE_ROW   30    /* value row moved to the very bottom row */
#define V_AREA_Y0     24    /* Zeilen 4..27 */
#define V_AREA_Y1    239    /* fader area extends to the screen edge */
#define V_X_OFF        1    /* Fader x = strip*40 + 1 .. +30 (unter dem Kurznamen) */
#define V_FADER_W     30
#define V_Y0          28
#define V_Y1         228    /* fader bottom near the edge (value row below) */

#define BOX_SZ        23    /* Toggle/Bang-Kasten: (BOX_SZ+1) = 24 px Kante */
#define HIT_TOL        2    /* Toleranz um Toggle/Bang-Kasten (px) */
#define SLIDE_TOL      4    /* Toleranz um Slider/Radio quer zur Zugrichtung */

/* Pixel text on the former 8-px console row grid (row r = 1..30):
 * row r starts at y = (r-1)*8, so all positions match the touch hitboxes.
 * Text uses the 6x9 font5x8 cell; a full line is LINE_CHARS cells wide. */
#define ROW_Y(r)      (((r) - 1) * 8)
#define LINE_CHARS    (BOT_W / FONT_ADV_X)   /* 53 */
#define PANEL_Y1      ROW_Y(LAST_PANEL_ROW + 1)   /* panel = y 0..231 */

typedef struct { int x0, y0, x1, y1; } Rect;

static int s_layout = CTRL_LAYOUT_HORIZ;

/* Camera reserve: when the CAM mode is "Bild"/"Bild+Matrix", a fixed block at
 * the top of the bottom screen is occupied by the 160x120 camera image (drawn
 * by main.c via cam_view). The whole fader area is then pushed down by
 * CAM_RESERVE_ROWS text rows (8 px each) so it never overlaps the image.
 * In horizontal layout fewer entries fit per page (the view scrolls for the
 * rest); in vertical layout the faders keep their width but become shorter. */
#define CAM_RESERVE_ROWS 16            /* 128 px: covers image y6..129 + "CAM" label */
static int s_cam_reserve = 0;

static int cam_off_rows(void) { return s_cam_reserve ? CAM_RESERVE_ROWS : 0; }
static int cam_off_px(void)   { return cam_off_rows() * 8; }

/* Horizontal entries that still fit below the (optional) CAM reserve.
 * Equals CTRL_PANEL_PAGE_H (5) when no reserve is active. */
static int h_page(void)
{
    int rows = (LAST_PANEL_ROW + 1) - (FIRST_ROW + cam_off_rows());
    int n = rows / H_ENTRY_ROWS;
    if (n < 1) n = 1;
    if (n > CTRL_PANEL_PAGE_H) n = CTRL_PANEL_PAGE_H;
    return n;
}

void ctrl_panel_set_cam_reserve(int on)
{
    s_cam_reserve = on ? 1 : 0;
}

/* --- Cam-Tune gating + Touch-XY frame state ----------------------------- */
static int s_cam_tune_on = 0;                 /* cam faders only on cam patches */
static int s_touch_grab  = 0;                 /* drag inside Touch-XY frame active */
static int s_touch_lx = -1, s_touch_ly = -1;  /* last sent touch-x/y (de-dupe) */

/* --- Unified CTRL-screen layout rule ------------------------------------ *
 * One spacing rule for every object drawn on the CTRL bottom screen:
 *   CTRL_M          px margin from the screen edge (left / right / bottom),
 *                   matching the horizontal fader inset (H_X0 = 8).
 *   CTRL_CAP_Y      caption row: same height as the MIXER label (y = 3), so
 *                   the header text keeps its position when toggling
 *                   MIXER <-> CTRL.
 *   CTRL_CONTENT_Y0 content (Touch-XY frame / faders) begins here: aligns with
 *                   the vertical fader label row 3 (y = 16). */
#define CTRL_M          8
#define CTRL_CAP_Y      3
#define CTRL_CONTENT_Y0 16

void ctrl_panel_set_cam_tune(int on)
{
    s_cam_tune_on = on ? 1 : 0;
}

/* ---- Hilfsfunktionen --------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Wert -> Anteil 0..1 innerhalb des Bereichs (hi < lo erlaubt) */
static float ctrl_fraction(const PdCtrl *c)
{
    float range = c->hi - c->lo;
    if (range == 0.0f) return 0.0f;
    return clampf((c->val - c->lo) / range, 0.0f, 1.0f);
}

static int count_of(const CtrlPanel *p)
{
    return (p && p->list) ? p->list->count : 0;
}

/* ---- Cam Tune virtual fader bank --------------------------------------- *
 * The 5 "Cam Tune" faders are NOT PD receivers: they are internal C params
 * that drive the live camera Off-stage globals (camtune.h). They are appended
 * as virtual entries AFTER the real PD receivers, so the panel treats the
 * logical entry list as [PD receivers ...][Exposure, Hold, Temporal, Spatial,
 * Sharpen]. total_count() is the real length; entry_ptr() returns the backing
 * PdCtrl for any logical index; a touch on a Cam Tune entry writes the matching
 * global directly instead of sending a float to PD. */
#define CAMTUNE_N 5
static PdCtrl s_camtune[CAMTUNE_N];
static int    s_camtune_init = 0;

/* Rebuild the fixed fader metadata once, then refresh values from the globals
 * on every call so the strips always show the live numbers. */
static void camtune_ensure(void)
{
    if (!s_camtune_init) {
        static const char *names[CAMTUNE_N] =
            { "Exposure", "Hold", "Temporal", "Spatial", "Sharpen" };
        for (int i = 0; i < CAMTUNE_N; i++) {
            memset(&s_camtune[i], 0, sizeof(s_camtune[i]));
            snprintf(s_camtune[i].name, sizeof(s_camtune[i].name), "%s", names[i]);
            s_camtune[i].type   = CTRL_FLOAT;
            s_camtune[i].lo     = 0.0f;
            s_camtune[i].hi     = 1.0f;
            s_camtune[i].steps  = 0;
            s_camtune[i].orient = PD_ORIENT_VERT;
        }
        s_camtune_init = 1;
    }
    /* Show the EFFECTIVE values of the active Buffer FX stage: Off (g_cam_smooth
     * 0) mirrors the live globals (editable); X1..X10 show their fixed ladder
     * values (display only - see camtune_readonly() below). */
    float v[5];
    cam_view_stage_inspect(g_cam_smooth, v);
    for (int i = 0; i < CAMTUNE_N; i++) s_camtune[i].val = v[i];
}

/* Logical entry count including the Cam Tune bank (only on cam patches). */
static int total_count(const CtrlPanel *p)
{
    return count_of(p) + (s_cam_tune_on ? CAMTUNE_N : 0);
}

/* 1 if the logical index addresses a Cam Tune fader (past the PD receivers). */
static int is_camtune_idx(const CtrlPanel *p, int idx)
{
    return idx >= count_of(p);
}

/* A Cam Tune fader is read-only whenever a glitch stage (X1..X10) is selected:
 * those stages are fixed, so the faders only REPORT their values there. At Off
 * (g_cam_smooth 0) the faders stay live and editable. */
static int camtune_readonly(const CtrlPanel *p, int idx)
{
    return is_camtune_idx(p, idx) && g_cam_smooth != 0;
}

/* Backing PdCtrl for a logical index (PD receiver or Cam Tune fader). */
static PdCtrl *entry_ptr(const CtrlPanel *p, int idx)
{
    int base = count_of(p);
    if (idx < base) return &p->list->ctrls[idx];
    int k = idx - base;
    if (k < 0) k = 0;
    if (k >= CAMTUNE_N) k = CAMTUNE_N - 1;
    return &s_camtune[k];
}

/* Write a Cam Tune fader value (0..1) to its live global. */
static void camtune_write_global(int k, float v)
{
    switch (k) {
        case 0: g_camtune_exposure = v; break;
        case 1: g_camtune_hold     = v; break;
        case 2: g_camtune_temporal = v; break;
        case 3: g_camtune_spatial  = v; break;
        case 4: g_camtune_sharpen  = v; break;
        default: break;
    }
}

static void send_ctrl(const PdCtrl *c);   /* fwd: defined with the other helpers */

/* Commit a changed value: Cam Tune entries write their global, real PD
 * receivers send a float to the patch. */
static void ctrl_commit(const CtrlPanel *p, int idx, PdCtrl *c)
{
    if (is_camtune_idx(p, idx)) camtune_write_global(idx - count_of(p), c->val);
    else                        send_ctrl(c);
}

int ctrl_panel_is_vertical(const CtrlPanel *p)
{
    if (s_layout == CTRL_LAYOUT_VERT)  return 1;
    if (s_layout != CTRL_LAYOUT_AUTO)  return 0;
    int v = 0, h = 0, n = count_of(p);
    for (int i = 0; i < n; i++) {
        if (p->list->ctrls[i].orient == PD_ORIENT_VERT)  v++;
        if (p->list->ctrls[i].orient == PD_ORIENT_HORIZ) h++;
    }
    return v > h;
}

static int page_size(const CtrlPanel *p)
{
    return ctrl_panel_is_vertical(p) ? CTRL_PANEL_PAGE_V : h_page();
}

static int h_name_row(int vi) { return FIRST_ROW + cam_off_rows() + vi * H_ENTRY_ROWS; }

/* --- Touch-XY frame ----------------------------------------------------- *
 * A patch that receives touch-x / touch-y gets a drawn rectangle: touching
 * inside it sends ONLY touch-x/y (normalised to the frame, scaled to the full
 * 0..319 / 0..239 range so the .pd patches stay unchanged). When the patch has
 * no faders the frame fills the whole bottom screen; with faders it sits to the
 * right of the vertical strips. */
static int panel_has_touch(const CtrlPanel *p)
{
    return p && p->list && p->list->has_touch_xy;
}

static Rect touch_frame_rect(const CtrlPanel *p)
{
    Rect r;
    r.y0 = CTRL_CONTENT_Y0;
    r.y1 = 240 - 1 - CTRL_M;
    r.x1 = BOT_W - 1 - CTRL_M;
    int nf = count_of(p);
    if (nf > 0 && ctrl_panel_is_vertical(p)) {
        int nc = nf > CTRL_PANEL_PAGE_V ? CTRL_PANEL_PAGE_V : nf;
        r.x0 = nc * V_STRIP_W + CTRL_M;
        if (r.x0 > r.x1 - 10) r.x0 = CTRL_M;   /* no room -> fullscreen */
    } else {
        r.x0 = CTRL_M;
    }
    return r;
}

static int in_frame(Rect r, int px, int py)
{
    return px >= r.x0 && px <= r.x1 && py >= r.y0 && py <= r.y1;
}

static void touch_xy_send(const CtrlPanel *p, int px, int py, int first)
{
    Rect fr = touch_frame_rect(p);
    float fx = (float)(px - fr.x0) / (float)(fr.x1 - fr.x0);
    float fy = (float)(py - fr.y0) / (float)(fr.y1 - fr.y0);
    int X = (int)(clampf(fx, 0.0f, 1.0f) * 319.0f + 0.5f);
    int Y = (int)(clampf(fy, 0.0f, 1.0f) * 239.0f + 0.5f);
    if (first) pd_input_bang("touch-on");
    if (X != s_touch_lx) { pd_input_float("touch-x", (float)X); s_touch_lx = X; }
    if (Y != s_touch_ly) { pd_input_float("touch-y", (float)Y); s_touch_ly = Y; }
}

/* Widget-Rechteck des sichtbaren Eintrags vi (Slider/Radio-Flaeche) */
static Rect widget_rect(int vert, int vi)
{
    Rect r;
    if (vert) {
        r.x0 = vi * V_STRIP_W + V_X_OFF;
        r.x1 = r.x0 + V_FADER_W - 1;
        r.y0 = V_Y0 + cam_off_px();   /* shorter fader when CAM image reserved */
        r.y1 = V_Y1;
    } else {
        r.x0 = H_X0;
        r.x1 = H_X1;
        r.y0 = h_name_row(vi) * 8 + H_WIDGET_DY;
        r.y1 = r.y0 + H_WIDGET_H - 1;
    }
    return r;
}

/* Toggle/Bang-Kasten innerhalb des Widget-Rechtecks */
static Rect box_rect(int vert, Rect w)
{
    Rect b;
    b.x0 = vert ? w.x0 + (V_FADER_W - (BOX_SZ + 1)) / 2 : w.x0;
    b.y0 = w.y0;
    b.x1 = b.x0 + BOX_SZ;
    b.y1 = b.y0 + BOX_SZ;
    return b;
}

static int in_rect(Rect r, int x, int y, int tol)
{
    return x >= r.x0 - tol && x <= r.x1 + tol &&
           y >= r.y0 - tol && y <= r.y1 + tol;
}

/* Langer Wert fuer Namenszeile / Detailzeile */
static void fmt_value(const PdCtrl *c, char *out, size_t n)
{
    if (c->type == CTRL_TOGGLE) { snprintf(out, n, "%s", c->val >= 0.5f ? "AN" : "AUS"); return; }
    if (c->type == CTRL_BANG)   { snprintf(out, n, "%s", "BANG"); return; }
    if (c->type == CTRL_RADIO)  { snprintf(out, n, "%d/%d", (int)c->val + 1, c->steps); return; }
    float av = c->val < 0.f ? -c->val : c->val;
    if (c->val == (float)(int)c->val && av < 100000.f) snprintf(out, n, "%d", (int)c->val);
    else if (av < 10.f)   snprintf(out, n, "%.3f", (double)c->val);
    else if (av < 100.f)  snprintf(out, n, "%.2f", (double)c->val);
    else if (av < 1000.f) snprintf(out, n, "%.1f", (double)c->val);
    else                  snprintf(out, n, "%.0f", (double)c->val);
}

/* Kurzwert (max. 4 Zeichen) fuer die Streifen im vertikalen Layout */
static void fmt_short(const PdCtrl *c, char *out, size_t n)
{
    if (c->type == CTRL_TOGGLE) { snprintf(out, n, "%s", c->val >= 0.5f ? "AN" : "AUS"); return; }
    if (c->type == CTRL_BANG)   { snprintf(out, n, "%s", "BNG"); return; }
    if (c->type == CTRL_RADIO)  { snprintf(out, n, "%d", ((int)c->val + 1) % 1000); return; }
    float v = c->val, av = v < 0.f ? -v : v;
    char buf[16];
    if (v == (float)(int)v && av < 10000.f) snprintf(buf, sizeof(buf), "%d", (int)v);
    else if (av >= 10000.f) snprintf(buf, sizeof(buf), "%dk", (int)(v / 1000.f));
    else if (av >= 1000.f)  snprintf(buf, sizeof(buf), "%.0f", (double)v);
    else if (av >= 100.f)   snprintf(buf, sizeof(buf), "%.0f", (double)v);
    else if (av >= 10.f)    snprintf(buf, sizeof(buf), "%.1f", (double)v);
    else if (v < 0.f)       snprintf(buf, sizeof(buf), "%.1f", (double)v);
    else                    snprintf(buf, sizeof(buf), "%.2f", (double)v);
    snprintf(out, n, "%.4s", buf);
}

static const char *title_of(const PdCtrl *c)
{
    return c->label[0] ? c->label : c->name;
}

/* Bottom framebuffer (RGB565 required), NULL if not available */
static u16 *bottom_fb(void)
{
    if (gfxGetScreenFormat(GFX_BOTTOM) != GSP_RGB565_OES) return NULL;
    return (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
}

/* Text colour of a row (rainbow-aware) */
static u16 row_fg(int row) { return theme_fg_at(ROW_Y(row) + 4); }

/* One full-width text line (padded with blanks to the right edge) */
static void draw_line(u16 *fb, int row, const char *text, u16 fg)
{
    char buf[LINE_CHARS + 1];
    snprintf(buf, sizeof(buf), "%-*.*s", LINE_CHARS, LINE_CHARS, text ? text : "");
    int x = draw_string_bot(fb, 0, ROW_Y(row), buf, fg, TH.bg);
    draw_fill_bot(fb, x, ROW_Y(row), BOT_W - x, FONT_ADV_Y, TH.bg);
}

/* Name/value line: '>' marks the selection, value right-aligned */
static void draw_entry_line(u16 *fb, int row, const PdCtrl *c, int selected)
{
    char value[16], line[LINE_CHARS + 1];
    fmt_value(c, value, sizeof(value));
    int name_w = LINE_CHARS - 2 - 11;   /* marker + gap + 10-char value */
    snprintf(line, sizeof(line), "%c%-*.*s %10.10s", selected ? '>' : ' ',
             name_w, name_w, title_of(c), value);
    draw_line(fb, row, line, selected ? TH.accent : row_fg(row));
}

static void send_ctrl(const PdCtrl *c)
{
    pd_input_float(c->name, c->val);
}

/* Touch-Position -> Sliderwert (Griff-Bereich wie beim Zeichnen) */
static float slider_value(const PdCtrl *c, int vert, Rect w, int px, int py)
{
    float t;
    if (vert) t = (float)((w.y1 - 1) - py) / (float)((w.y1 - 1) - (w.y0 + 1));
    else      t = (float)(px - (w.x0 + 1)) / (float)((w.x1 - 1) - (w.x0 + 1));
    t = clampf(t, 0.0f, 1.0f);
    return c->lo + t * (c->hi - c->lo);
}

/* Touch-Position -> Radio-Zelle 0..steps-1 (vertikal: Zelle 0 oben) */
static float radio_value(const PdCtrl *c, int vert, Rect w, int px, int py)
{
    int steps = c->steps > 0 ? c->steps : 1;
    int cell  = vert ? (py - w.y0) * steps / (w.y1 - w.y0 + 1)
                     : (px - w.x0) * steps / (w.x1 - w.x0 + 1);
    if (cell < 0) cell = 0;
    if (cell > steps - 1) cell = steps - 1;
    return (float)cell;
}

/* Auswahl im sichtbaren Bereich halten (Seitengroesse haengt vom Layout ab) */
static void keep_visible(CtrlPanel *p)
{
    int count = total_count(p);
    int ps    = page_size(p);
    if (p->selected >= count) p->selected = count > 0 ? count - 1 : 0;
    if (p->selected < 0) p->selected = 0;
    int max_scroll = count - ps;
    if (max_scroll < 0) max_scroll = 0;

    if (p->selected < p->scroll) p->scroll = p->selected;
    if (p->selected >= p->scroll + ps) p->scroll = p->selected - ps + 1;
    if (p->scroll > max_scroll) p->scroll = max_scroll;
    if (p->scroll < 0) p->scroll = 0;
}

/* ---- Oeffentliche API -------------------------------------------------- */

void ctrl_panel_init(CtrlPanel *p, PdCtrlList *list)
{
    if (!p) return;
    camtune_ensure();
    p->list     = list;
    p->scroll   = 0;
    p->selected = 0;
    p->active   = -1;
    s_touch_grab = 0;
    s_touch_lx = s_touch_ly = -1;
}

void ctrl_panel_set_layout(CtrlPanel *p, int mode)
{
    if (mode < 0 || mode >= CTRL_LAYOUT_COUNT) mode = CTRL_LAYOUT_HORIZ;
    s_layout = mode;
    if (!p) return;
    p->active = -1;     /* Griff gehoert zum alten Layout */
    keep_visible(p);    /* sofort an neue Seitengroesse anpassen */
}

static void draw_text_horizontal(u16 *fb, const CtrlPanel *p)
{
    int count = total_count(p);
    for (int vi = 0; vi < h_page(); vi++) {
        int row = h_name_row(vi);
        int idx = p->scroll + vi;
        if (idx >= count) continue;   /* area already cleared */
        draw_entry_line(fb, row, entry_ptr(p, idx), idx == p->selected);
        /* widget rows are cleared + drawn by ctrl_panel_draw_faders() */
    }
}

/* One strip cell (label / value row): 6 chars = 36 px, rest of the
 * 40-px strip filled with bg */
static void draw_strip_cell(u16 *fb, int k, int row, const char *text, u16 fg)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%-6.6s", text);
    int x0 = k * V_STRIP_W;
    int x  = draw_string_bot(fb, x0, ROW_Y(row), buf, fg, TH.bg);
    draw_fill_bot(fb, x, ROW_Y(row), x0 + V_STRIP_W - x, FONT_ADV_Y, TH.bg);
}

static void draw_text_vertical(u16 *fb, const CtrlPanel *p)
{
    int count = total_count(p);
    int label_row = V_LABEL_ROW + cam_off_rows();

    /* row 2: full name + value of the selected entry (hidden under the CAM
     * image, so skip it when the camera reserve is active) */
    if (!s_cam_reserve && p->selected >= 0 && p->selected < count)
        draw_entry_line(fb, 2, entry_ptr(p, p->selected), 1);

    (void)V_STRIP_COLS;
    for (int k = 0; k < CTRL_PANEL_PAGE_V; k++) {
        int idx = p->scroll + k;
        if (idx >= count) continue;   /* area already cleared */
        const PdCtrl *c = entry_ptr(p, idx);
        char sv[8];
        fmt_short(c, sv, sizeof(sv));
        int sel = idx == p->selected;
        draw_strip_cell(fb, k, label_row, title_of(c),
                        sel ? TH.accent : row_fg(label_row));
        draw_strip_cell(fb, k, V_VALUE_ROW, sv,
                        sel ? TH.accent : row_fg(V_VALUE_ROW));
    }
}

void ctrl_panel_draw(const CtrlPanel *p, const char *patch_name)
{
    if (!p) return;
    (void)patch_name;   /* patch name shown on the top screen, not in CTRL */
    camtune_ensure();
    u16 *fb = bottom_fb();
    if (!fb) return;
    int count = total_count(p);

    /* clear the panel area (rows 1..29); row 30 (hint) belongs to main.c */
    draw_fill_bot(fb, 0, 0, BOT_W, PANEL_Y1, TH.bg);

    if (s_cam_reserve) {
        /* The header line sits under the CAM image; skip it and instead draw a
         * small "CAM" caption just below the image (patch name is on the top
         * screen). The image itself is drawn afterwards by main.c. */
        draw_string_bot(fb, H_X0, 131, "CAM", theme_fg_at(131), TH.bg);
    }

    if (count == 0) {
        /* Touch-only patch (e.g. theremin): the Touch-XY frame is the whole UI,
         * drawn by ctrl_panel_draw_faders(); no "no receivers" hint. */
        if (panel_has_touch(p)) return;
        int r0 = FIRST_ROW + cam_off_rows();
        draw_line(fb, r0,     "Keine steuerbaren Receiver gefunden.", row_fg(r0));
        draw_line(fb, r0 + 1, "Tipp: [r name] oder Kommentar", TH.dim);
        draw_line(fb, r0 + 2, "\"@ctrl name lo hi\" im Patch.", TH.dim);
        return;
    }

    if (ctrl_panel_is_vertical(p)) {
        draw_text_vertical(fb, p);
    } else {
        draw_text_horizontal(fb, p);
    }
}

/* Treffer beim Aufsetzen: Widget eines sichtbaren Eintrags oder Name.
 * *select_only = 1 wenn nur der Name/Kurzname getroffen wurde. */
static int hit_test(const CtrlPanel *p, int vert, int px, int py, int *select_only)
{
    int count = total_count(p);
    int ps    = vert ? CTRL_PANEL_PAGE_V : h_page();
    int vlab  = V_LABEL_ROW + cam_off_rows();
    *select_only = 0;

    for (int vi = 0; vi < ps; vi++) {
        int idx = p->scroll + vi;
        if (idx >= count) break;
        const PdCtrl *c = entry_ptr(p, idx);
        Rect w = widget_rect(vert, vi);

        int hit;
        if (c->type == CTRL_TOGGLE || c->type == CTRL_BANG) {
            hit = in_rect(box_rect(vert, w), px, py, HIT_TOL);
        } else if (vert) {
            hit = px >= w.x0 - SLIDE_TOL && px <= w.x1 + SLIDE_TOL &&
                  py >= w.y0 - HIT_TOL && py <= w.y1 + HIT_TOL;
        } else {
            hit = px >= w.x0 - HIT_TOL && px <= w.x1 + HIT_TOL &&
                  py >= w.y0 - SLIDE_TOL && py <= w.y1 + SLIDE_TOL;
        }
        if (hit) return idx;

        /* Name antippen = nur auswaehlen */
        int name_hit;
        if (vert) {
            int sx0 = vi * V_STRIP_W;
            name_hit = px >= sx0 && px < sx0 + V_STRIP_W &&
                       ((py >= (vlab - 1) * 8 && py < vlab * 8) ||
                        (py >= (V_VALUE_ROW - 1) * 8 && py < V_VALUE_ROW * 8));
        } else {
            int row  = h_name_row(vi);
            name_hit = py >= (row - 1) * 8 && py < row * 8 && px < LINE_W * 8;
        }
        if (name_hit) { *select_only = 1; return idx; }
    }
    return -1;
}

int ctrl_panel_poll_touch(CtrlPanel *p)
{
    if (!p) return 0;
    camtune_ensure();   /* total_count() always >= CAMTUNE_N, so never empty */

    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();

    if (!(held & KEY_TOUCH)) {
        /* losgelassen: Touch-XY-Rahmen schliessen (touch-off an PD) */
        if (s_touch_grab) {
            pd_input_bang("touch-off");
            s_touch_grab = 0;
            s_touch_lx = s_touch_ly = -1;
        }
        /* losgelassen: eine gedrueckte Bang-Taste wieder leer zeichnen */
        int was_bang = p->active >= 0 && p->active < total_count(p) &&
                       entry_ptr(p, p->active)->type == CTRL_BANG;
        p->active = -1;
        return was_bang;
    }

    touchPosition tp;
    hidTouchRead(&tp);
    int vert = ctrl_panel_is_vertical(p);

    /* Touch-XY frame owns its area exclusively: once grabbed, keep sending
     * touch-x/y and ignore the faders until the finger lifts. */
    if (s_touch_grab) {
        if (in_frame(touch_frame_rect(p), tp.px, tp.py)) {
            touch_xy_send(p, tp.px, tp.py, 0);
        } else {
            /* finger dragged out of the frame -> end the gesture, so touch-x/y
             * are only ever sent while the finger is inside the frame. */
            pd_input_bang("touch-off");
            s_touch_grab = 0;
            s_touch_lx = s_touch_ly = -1;
        }
        return 0;
    }
    if ((down & KEY_TOUCH) && panel_has_touch(p) &&
        in_frame(touch_frame_rect(p), tp.px, tp.py)) {
        s_touch_grab = 1;
        p->active = -1;
        touch_xy_send(p, tp.px, tp.py, 1);
        return 0;
    }

    if (down & KEY_TOUCH) {
        p->active = -1;

        int select_only;
        int idx = hit_test(p, vert, tp.px, tp.py, &select_only);
        if (idx < 0) return 0;          /* daneben: nichts passiert */

        int changed = idx != p->selected;
        p->selected = idx;
        if (select_only) return changed;

        p->active = idx;
        if (camtune_readonly(p, idx)) {   /* X1..X10: faders only report, no drag */
            p->active = -1;
            return changed;
        }
        PdCtrl *c = entry_ptr(p, idx);
        Rect w = widget_rect(vert, idx - p->scroll);
        if (c->type == CTRL_TOGGLE) {
            c->val = (c->val >= 0.5f) ? 0.0f : 1.0f;   /* nur beim Aufsetzen */
            ctrl_commit(p, idx, c);
        } else if (c->type == CTRL_BANG) {
            pd_input_bang(c->name);                    /* nur beim Aufsetzen */
        } else if (c->type == CTRL_RADIO) {
            c->val = radio_value(c, vert, w, tp.px, tp.py);
            ctrl_commit(p, idx, c);
        } else {
            c->val = slider_value(c, vert, w, tp.px, tp.py);
            ctrl_commit(p, idx, c);
        }
        return 1;
    }

    /* Gehaltener Touch: aktiven Slider/Radio nachziehen */
    if (p->active < 0 || p->active >= total_count(p)) return 0;
    int vi = p->active - p->scroll;
    if (vi < 0 || vi >= page_size(p)) return 0;   /* weggescrollt */
    PdCtrl *c = entry_ptr(p, p->active);
    Rect w = widget_rect(vert, vi);
    float v;
    if (c->type == CTRL_FLOAT)      v = slider_value(c, vert, w, tp.px, tp.py);
    else if (c->type == CTRL_RADIO) v = radio_value(c, vert, w, tp.px, tp.py);
    else return 0;

    if (v == c->val) return 0;
    c->val = v;
    ctrl_commit(p, p->active, c);
    return 1;
}

/* ---- Pixel-Widgets ------------------------------------------------------ */

/* Bottom screen RGB565, stored rotated by 90 degrees (column-major,
 * y from the bottom): index = sx * 240 + (239 - sy). Widgets are drawn in
 * the EYE theme: foreground = theme_fg_at(y) (TH.accent for the selected
 * entry), background = TH.bg. */

static inline void set_px(u16 *fb, int sx, int sy, u16 col)
{
    if ((unsigned)sx >= 320u || (unsigned)sy >= 240u) return;
    fb[sx * 240 + (239 - sy)] = col;
}

static void fill_rect(u16 *fb, int x0, int y0, int x1, int y1, u16 col)
{
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            set_px(fb, x, y, col);
}

static void frame_rect(u16 *fb, Rect r, u16 fg)
{
    for (int x = r.x0; x <= r.x1; x++) { set_px(fb, x, r.y0, fg); set_px(fb, x, r.y1, fg); }
    for (int y = r.y0; y <= r.y1; y++) { set_px(fb, r.x0, y, fg); set_px(fb, r.x1, y, fg); }
}

/* Toggle: square box, ON = cross (as in Pd) */
static void draw_toggle(u16 *fb, int on, Rect b, u16 fg)
{
    int sz = b.x1 - b.x0;
    frame_rect(fb, b, fg);
    if (!on) return;
    for (int i = 3; i <= sz - 3; i++) {
        set_px(fb, b.x0 + i,      b.y0 + i, fg);
        set_px(fb, b.x0 + i + 1,  b.y0 + i, fg);   /* 2 px Strich */
        set_px(fb, b.x0 + sz - i, b.y0 + i, fg);
        set_px(fb, b.x0 + sz - i - 1, b.y0 + i, fg);
    }
}

/* Bang: Kasten mit Kreis, gefuellt solange gedrueckt */
static void draw_bang(u16 *fb, int pressed, Rect b, u16 fg)
{
    int sz = b.x1 - b.x0;
    frame_rect(fb, b, fg);
    int cx2 = 2 * b.x0 + sz, cy2 = 2 * b.y0 + sz;   /* doppelte Mittelpunkt-Koordinaten */
    int r2  = sz - 4;                                 /* doppelter Radius */
    for (int y = b.y0 + 1; y < b.y1; y++) {
        for (int x = b.x0 + 1; x < b.x1; x++) {
            int dx = 2 * x - cx2, dy = 2 * y - cy2;
            int d  = dx * dx + dy * dy;
            if (pressed ? d <= r2 * r2
                        : (d <= r2 * r2 && d >= (r2 - 3) * (r2 - 3)))
                set_px(fb, x, y, fg);
        }
    }
}

/* Radio: steps Zellen ueber das Widget, gewaehlte Zelle gefuellt.
 * Vertikal: Zellen von oben nach unten. Zellen < 4 px: ohne Gitter. */
static void draw_radio(u16 *fb, const PdCtrl *c, int vert, Rect w, u16 fg)
{
    int steps = c->steps > 0 ? c->steps : 1;
    int sel   = (int)c->val;
    if (sel < 0) sel = 0;
    if (sel > steps - 1) sel = steps - 1;
    int len   = vert ? (w.y1 - w.y0 + 1) : (w.x1 - w.x0 + 1);
    int base  = vert ? w.y0 : w.x0;
    int grid  = (len / steps) >= 4;

    frame_rect(fb, w, fg);
    for (int i = 0; i < steps; i++) {
        int a = base + i * len / steps;
        int b = base + (i + 1) * len / steps - 1;
        if (grid && i > 0) {
            if (vert) for (int x = w.x0; x <= w.x1; x++) set_px(fb, x, a, fg);
            else      for (int y = w.y0; y <= w.y1; y++) set_px(fb, a, y, fg);
        }
        if (i != sel) continue;
        int a2 = a + 2, b2 = b - 2 < a + 2 ? a + 2 : b - 2;
        if (vert) fill_rect(fb, w.x0 + 2, a2, w.x1 - 2, b2, fg);
        else      fill_rect(fb, a2, w.y0 + 2, b2, w.y1 - 2, fg);
    }
}

/* Slider: Rahmen + 1-px-Griff (horizontal: x, vertikal: y, unten = Minimum) */
static void draw_slider(u16 *fb, const PdCtrl *c, int vert, Rect w, u16 fg)
{
    frame_rect(fb, w, fg);
    float frac = ctrl_fraction(c);
    if (vert) {
        int lo = w.y1 - 1, hi = w.y0 + 1;
        int gy = lo - (int)(frac * (float)(lo - hi) + 0.5f);
        for (int x = w.x0; x <= w.x1; x++) set_px(fb, x, gy, fg);
    } else {
        int lo = w.x0 + 1, hi = w.x1 - 1;
        int gx = lo + (int)(frac * (float)(hi - lo) + 0.5f);
        for (int y = w.y0; y <= w.y1; y++) set_px(fb, gx, y, fg);
    }
}

void ctrl_panel_draw_faders(const CtrlPanel *p)
{
    camtune_ensure();
    int count = total_count(p);

    u16 *fb = bottom_fb();
    if (!fb) return;

    /* Touch-XY frame: static rectangle + caption (touch sends touch-x/y). */
    if (panel_has_touch(p)) {
        Rect fr = touch_frame_rect(p);
        frame_rect(fb, fr, theme_fg_at((fr.y0 + fr.y1) / 2));
        draw_string_bot(fb, fr.x0, CTRL_CAP_Y, "TOUCH X/Y",
                        theme_fg_at(CTRL_CAP_Y), TH.bg);
    }

    if (count == 0) return;

    int vert = ctrl_panel_is_vertical(p);
    int ps   = vert ? CTRL_PANEL_PAGE_V : h_page();
    int bang_held = (hidKeysHeld() & KEY_TOUCH) != 0;

    for (int vi = 0; vi < ps; vi++) {
        int idx = p->scroll + vi;

        /* Widget-Flaeche loeschen (ersetzt das Leeren per Konsolentext) */
        if (vert) {
            fill_rect(fb, vi * V_STRIP_W, V_AREA_Y0 + cam_off_px(),
                      vi * V_STRIP_W + V_STRIP_W - 1, V_AREA_Y1, TH.bg);
        } else {
            if (idx >= count) break;   /* leere Eintraege loescht der Text */
            int y = h_name_row(vi) * 8;
            fill_rect(fb, 0, y, BOT_W - 1, y + (H_ENTRY_ROWS - 1) * 8 - 1, TH.bg);
        }
        if (idx >= count) continue;

        const PdCtrl *c = entry_ptr(p, idx);
        Rect w  = widget_rect(vert, vi);
        u16  fg = idx == p->selected ? TH.accent
                                     : theme_fg_at((w.y0 + w.y1) / 2);
        switch (c->type) {
            case CTRL_TOGGLE: draw_toggle(fb, c->val >= 0.5f, box_rect(vert, w), fg); break;
            case CTRL_BANG:   draw_bang(fb, p->active == idx && bang_held, box_rect(vert, w), fg); break;
            case CTRL_RADIO:  draw_radio(fb, c, vert, w, fg); break;
            default:          draw_slider(fb, c, vert, w, fg); break;
        }
    }
}

void ctrl_panel_scroll(CtrlPanel *p, int delta)
{
    if (!p || delta == 0) return;
    camtune_ensure();
    int count = total_count(p);
    p->selected += delta;
    if (p->selected < 0) p->selected = 0;
    if (p->selected >= count) p->selected = count - 1;
    keep_visible(p);
}

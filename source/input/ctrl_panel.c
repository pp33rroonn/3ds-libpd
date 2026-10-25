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
#include <math.h>   /* fabsf: inertia glide velocity magnitude */

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
#define V_LABEL_ROW    1    /* heading at the very top, level with the TOUCH X/Y caption */
#define V_VALUE_ROW   30    /* value row on the very bottom row, below the fader */
#define V_AREA_Y0      8    /* strip clear starts just under the one-row heading */
#define V_AREA_Y1    231    /* clear down to the TOUCH X/Y frame bottom (value row survives) */
#define V_X_OFF        1    /* Fader x = strip*40 + 1 .. +30 (unter dem Kurznamen) */
#define V_FADER_W     30
#define V_Y0          14    /* fader top = TOUCH X/Y frame top (CTRL_CONTENT_Y0) */
#define V_Y1         231    /* fader bottom = TOUCH X/Y frame bottom (240-1-CTRL_M) */

#define BOX_SZ        23    /* Toggle/Bang-Kasten: (BOX_SZ+1) = 24 px Kante */
#define HIT_TOL        2    /* Toleranz um Toggle/Bang-Kasten (px) */
#define SLIDE_TOL      4    /* Toleranz um Slider/Radio quer zur Zugrichtung */
/* Horizontal sliders: no vertical bleed, so the gaps between the stacked faders
 * stay a free scroll zone (clean separation between a draggable fader bar and
 * the empty-area scroll grab). The bar is H_WIDGET_H (24 px) tall, so it stays
 * comfortably draggable along its length without any cross-axis tolerance. */
#define H_SLIDE_TOL_Y  0

/* --- Stylus scroll geometry --------------------------------------------- *
 * H_ENTRY_PX  : pixel height of one horizontal entry (name + widget) = 40.
 * V_X_BASE    : left inset of the vertical strip block. Same 8 px edge margin
 *               as the horizontal faders (CTRL_M); in the Touch-XY+faders view
 *               the strips start 8 px from the left and the frame ends 8 px from
 *               the right, so both sides match horizontal. Scrolling uses the
 *               gaps between strips + the name/value rows (gap/inertia scroll).
 * SCROLL_TAP_SLOP  : finger travel (px) below which a touch stays a tap.
 * V_SCROLL_STEP_PX : finger travel (px) per one strip step in vertical layout. */
#define H_ENTRY_PX       (H_ENTRY_ROWS * 8)                       /* 40 */
#define V_X_BASE         CTRL_M                                   /* 8, like horizontal */
#define SCROLL_TAP_SLOP  6
#define V_SCROLL_STEP_PX 24

/* --- Inertia / momentum scrolling (HORIZONTAL layout only) -------------- *
 * After a flick the list keeps gliding and decelerates. The vertical layout
 * keeps its strip-snap and never glides.
 *   SCROLL_FRICTION : per-frame velocity decay (lower = stops sooner).
 *   SCROLL_MIN_V    : px/frame; below this the glide stops (also the launch
 *                     threshold on release).
 *   SCROLL_VEL_CAP  : px/frame; max launch speed (clamps a very fast flick). */
#define SCROLL_FRICTION  0.88f
#define SCROLL_MIN_V     0.6f
#define SCROLL_VEL_CAP   24.0f

/* Pixel text on the former 8-px console row grid (row r = 1..30):
 * row r starts at y = (r-1)*8, so all positions match the touch hitboxes.
 * Text uses the 6x9 font5x8 cell; a full line is LINE_CHARS cells wide. */
#define ROW_Y(r)      (((r) - 1) * 8)
#define LINE_CHARS    (BOT_W / FONT_ADV_X)   /* 53 */
#define PANEL_Y1      ROW_Y(LAST_PANEL_ROW + 1)   /* panel = y 0..231 */

typedef struct { int x0, y0, x1, y1; } Rect;

/* Pixel primitives + band-clipped text (defined with the other pixel helpers
 * near the bottom; forward-declared so the horizontal renderer can use them). */
static inline void set_px(u16 *fb, int sx, int sy, u16 col);
static void band_string(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg);
static void clip_set(int y0, int y1);
static void clip_reset(void);

static int s_layout = CTRL_LAYOUT_HORIZ;

/* Camera reserve: when the CAM mode is "Bild"/"Bild+Matrix", a fixed block at
 * the top of the bottom screen is occupied by the 160x120 camera image (drawn
 * by main.c via cam_view). The whole fader area is then pushed down by
 * CAM_RESERVE_ROWS text rows (8 px each) so it never overlaps the image.
 * In horizontal layout fewer entries fit per page (the view scrolls for the
 * rest); in vertical layout the faders keep their width but become shorter. */
#define CAM_RESERVE_ROWS 16            /* 128 px: covers image y6..129 + "CAM" label */
static int s_cam_reserve = 0;

/* Whether the active panel has a Touch-XY control. Refreshed every frame by
 * ctrl_panel_draw()/ctrl_panel_draw_faders() (both receive the panel). It lets
 * the void helpers h_band_top()/h_band_bot() reserve the "TOUCH X/Y" caption
 * row only when the frame is present; a fader-only patch then has NO top/bottom
 * white strips (content runs edge to edge, 8 px left/right inset only). */
static int s_has_touch = 0;

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

/* --- Stylus scroll (empty-area pan). Vertical finger gesture only. -------- *
 * Horizontal layout pans pixel-smooth (s_scroll_px is the sub-entry offset in
 * pixels, 0..H_ENTRY_PX-1, on top of the integer p->scroll). Vertical layout
 * snaps strip-by-strip instead: the content moves sideways there, so an
 * animated horizontal pixel slide would contradict a purely vertical gesture.
 * A scroll grab never touches p->selected (pure pan). Inertia is deliberately
 * omitted for now, but the grab/offset split makes it easy to add later. */
static int s_scroll_px       = 0;   /* horizontal sub-entry pixel offset (0..H_ENTRY_PX-1) */
static int s_scroll_grab     = 0;   /* 1 while an empty-area scroll grab is active */
static int s_scroll_moved    = 0;   /* 1 once the drag passed SCROLL_TAP_SLOP */
static int s_scroll_start_py = 0;   /* stylus py when the grab began */
static int s_scroll_start_off = 0;  /* horiz: pixel total at grab start; vert: p->scroll */
/* Pending selection for a grab that began on a name/label row: if the finger is
 * released within the slop (a tap) this entry is selected; if it is dragged past
 * the slop the grab turns into a pure scroll pan and this is ignored. -1 = the
 * grab began in a truly empty area, so a tap selects nothing. */
static int s_scroll_pending_sel = -1;
/* Inertia glide velocity in px/frame (horizontal layout only). Non-zero only
 * between a flick-release and the glide decaying below SCROLL_MIN_V; at rest it
 * is exactly 0.0f, so the glide logic is fully inert and the base image is
 * bit-identical to before. */
static float s_scroll_vel = 0.0f;

/* --- Unified CTRL-screen layout rule ------------------------------------ *
 * One spacing rule for every object drawn on the CTRL bottom screen:
 *   CTRL_M          px margin from the screen edge (left / right / bottom),
 *                   matching the horizontal fader inset (H_X0 = 8).
 *   CTRL_CAP_X      caption text x: one marker-cell (FONT_ADV_X = 6) from the
 *                   edge, exactly where the fader name text starts, and shared
 *                   with the MIXER label so the header never shifts sideways.
 *   CTRL_CAP_Y      caption row: same height as the MIXER label (y = 3), so
 *                   the header text keeps its position when toggling
 *                   MIXER <-> CTRL.
 *   CTRL_CONTENT_Y0 content top (Touch-XY frame): same y as the MIXER header
 *                   separator line (HEADER_H - 1 = 14). */
#define CTRL_M          8
#define CTRL_CAP_X      FONT_ADV_X
#define CTRL_CAP_Y      3
#define CTRL_CONTENT_Y0 14

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

/* Top pixel of the horizontal scroll band (top of the first entry's name row).
 * Non-cam: aligned to the TOUCH X/Y frame top (CTRL_CONTENT_Y0 = 14), so the
 * fader content starts at the same height as the Touch-XY frame; the left/right
 * inset stays CTRL_M (8) -> same margins as the Touch-XY frame.
 * Cam reserve on: keep the camera image block reserved at the top (unchanged).
 * All three consumers (draw_text_horizontal, the fader drawing and hit_test)
 * anchor their name/widget pixel-y to this value, so an offset of 0 stays a
 * consistent base image and the touch hitboxes match the drawn pixels. */
static int h_band_top(void)
{
    if (s_cam_reserve) return (FIRST_ROW + cam_off_rows() - 1) * 8;
    /* Touch patch: keep the "TOUCH X/Y" caption row (frame top at y14).
     * Fader-only patch: no caption -> start at the very top, NO white strip. */
    return s_has_touch ? CTRL_CONTENT_Y0 : 1;
}

/* Bottom pixel of the horizontal scroll band. Touch patch: stop at the Touch-XY
 * frame bottom (231). Fader-only patch: run to the screen edge, NO bottom strip. */
static int h_band_bot(void) { return s_has_touch ? (240 - 1 - CTRL_M) : (240 - 1); }

/* Entries that can be (partially) visible in the band, including the extra rows
 * a pixel-smooth drag reveals. Band height is not a multiple of H_ENTRY_PX, so
 * we round up and add one spare row. Used as the loop bound for drawing and
 * hit-testing the horizontal layout. */
static int h_vis_count(void)
{
    return (h_band_bot() - h_band_top()) / H_ENTRY_PX + 2;
}

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
        r.x0 = V_X_BASE + nc * V_STRIP_W + CTRL_M;   /* right of the centred strips */
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
        r.x0 = V_X_BASE + vi * V_STRIP_W + V_X_OFF;   /* centred strip block */
        r.x1 = r.x0 + V_FADER_W - 1;
        r.y0 = V_Y0 + cam_off_px();   /* shorter fader when CAM image reserved */
        r.y1 = V_Y1;
    } else {
        r.x0 = H_X0;
        r.x1 = H_X1;
        /* Anchor to the band top: name row (8 px) + gap, then shift the whole
         * entry up by the sub-entry scroll offset for pixel-smooth panning. */
        r.y0 = h_band_top() + vi * H_ENTRY_PX + 8 + H_WIDGET_DY - s_scroll_px;
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
static void draw_entry_line(u16 *fb, int row, const PdCtrl *c, int selected) __attribute__((unused));
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
    /* reset stylus scroll state (new patch -> aligned, no leftover offset) */
    s_scroll_px = 0;
    s_scroll_grab = 0;
    s_scroll_moved = 0;
    s_scroll_start_py = 0;
    s_scroll_start_off = 0;
    s_scroll_pending_sel = -1;
    s_scroll_vel = 0.0f;
}

void ctrl_panel_set_layout(CtrlPanel *p, int mode)
{
    if (mode < 0 || mode >= CTRL_LAYOUT_COUNT) mode = CTRL_LAYOUT_HORIZ;
    s_layout = mode;
    /* clear any leftover scroll offset so the other layout starts aligned */
    s_scroll_px = 0;
    s_scroll_grab = 0;
    s_scroll_moved = 0;
    s_scroll_pending_sel = -1;
    s_scroll_vel = 0.0f;
    if (!p) return;
    p->active = -1;     /* Griff gehoert zum alten Layout */
    keep_visible(p);    /* sofort an neue Seitengroesse anpassen */
}

/* Name/value line at an explicit pixel y (pixel-smooth path). Clipping to the
 * active band is handled by set_px/clip_set, so the caller sets the band once. */
static void draw_entry_line_px(u16 *fb, int y, const PdCtrl *c, int selected)
{
    char value[16], line[LINE_CHARS + 1];
    fmt_value(c, value, sizeof(value));
    int name_w = LINE_CHARS - 2 - 11;   /* marker + gap + 10-char value */
    snprintf(line, sizeof(line), "%c%-*.*s %10.10s", selected ? '>' : ' ',
             name_w, name_w, title_of(c), value);
    band_string(fb, 0, y, line, selected ? TH.accent : theme_fg_at(y + 4), TH.bg);
}

static void draw_text_horizontal(u16 *fb, const CtrlPanel *p)
{
    int count = total_count(p);
    int bt    = h_band_top();
    int bb    = h_band_bot();

    /* Draw every (partially) visible row up to h_vis_count(); rows whose top is
     * below the band are clipped away by clip_set(), so at s_scroll_px == 0 the
     * image is a consistent, correctly-margined base image. */
    clip_set(bt, bb);
    for (int vi = 0; vi < h_vis_count(); vi++) {
        int idx = p->scroll + vi;
        if (idx >= count) break;   /* area already cleared */
        int y = bt + vi * H_ENTRY_PX - s_scroll_px;   /* entry top (name row) */
        if (y > bb) break;                            /* fully below the band */
        draw_entry_line_px(fb, y, entry_ptr(p, idx), idx == p->selected);
        /* widget rows are cleared + drawn by ctrl_panel_draw_faders() */
    }
    clip_reset();
}

/* One strip cell (label / value row): 6 chars = 36 px, rest of the
 * 40-px strip filled with bg */
static void draw_strip_cell(u16 *fb, int k, int row, const char *text, u16 fg)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%-6.6s", text);
    int x0 = V_X_BASE + k * V_STRIP_W;   /* centred strip block */
    int x  = draw_string_bot(fb, x0, ROW_Y(row), buf, fg, TH.bg);
    draw_fill_bot(fb, x, ROW_Y(row), x0 + V_STRIP_W - x, FONT_ADV_Y, TH.bg);
}

static void draw_text_vertical(u16 *fb, const CtrlPanel *p)
{
    int count = total_count(p);
    int label_row = V_LABEL_ROW + cam_off_rows();

    /* No top-left/right selected name+value line here: the per-strip heading
     * sits at the top (V_LABEL_ROW) and the enlarged faders use the full
     * reclaimed height below it. */

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
    s_has_touch = panel_has_touch(p);   /* band geometry depends on it */
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
    /* horizontal also hit-tests the extra partially visible rows (pixel-smooth) */
    int ps    = vert ? CTRL_PANEL_PAGE_V : h_vis_count();
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
            /* Horizontal slider: full x tolerance (drag along its length), but no
             * vertical bleed, so the gaps above/below stay a free scroll zone. */
            hit = px >= w.x0 - HIT_TOL && px <= w.x1 + HIT_TOL &&
                  py >= w.y0 - H_SLIDE_TOL_Y && py <= w.y1 + H_SLIDE_TOL_Y;
        }
        if (hit) return idx;

        /* Name antippen = nur auswaehlen */
        int name_hit;
        if (vert) {
            int sx0 = V_X_BASE + vi * V_STRIP_W;   /* centred strip block */
            name_hit = px >= sx0 && px < sx0 + V_STRIP_W &&
                       ((py >= (vlab - 1) * 8 && py < vlab * 8) ||
                        (py >= (V_VALUE_ROW - 1) * 8 && py < V_VALUE_ROW * 8));
        } else {
            /* pixel-smooth: name row anchored to the band top, shifted up by
             * the sub-entry scroll offset (matches draw_text_horizontal). */
            int ntop = h_band_top() + vi * H_ENTRY_PX - s_scroll_px;
            name_hit = py >= ntop && py < ntop + 8 && px < LINE_W * 8;
        }
        if (name_hit) { *select_only = 1; return idx; }
    }
    return -1;
}

/* --- Stylus scroll helpers ---------------------------------------------- *
 * Pure pan: these change only p->scroll / s_scroll_px, never p->selected, and
 * never call keep_visible() (so the selection may legitimately scroll off). */

/* Horizontal: set the total pixel scroll position, clamped so the last page
 * stays aligned; derive the integer entry scroll + sub-entry pixel offset. */
static int scroll_set_px(CtrlPanel *p, int total)
{
    int ps = h_page();
    int max_total = (total_count(p) - ps) * H_ENTRY_PX;
    if (max_total < 0) max_total = 0;          /* fits on one page -> no scroll */
    if (total < 0) total = 0;
    if (total > max_total) total = max_total;
    int ns  = total / H_ENTRY_PX;
    int npx = total % H_ENTRY_PX;
    if (ns == p->scroll && npx == s_scroll_px) return 0;
    p->scroll   = ns;
    s_scroll_px = npx;
    return 1;
}

/* Vertical: snap to a target strip index (no pixel-smooth horizontal slide). */
static int scroll_set_strip(CtrlPanel *p, int target)
{
    int maxs = total_count(p) - CTRL_PANEL_PAGE_V;
    if (maxs < 0) maxs = 0;
    if (target < 0) target = 0;
    if (target > maxs) target = maxs;
    if (target == p->scroll) return 0;
    p->scroll = target;
    return 1;
}

/* Horizontal pixel scroll position (total px). Used by the inertia glide. */
static int h_cur_total(const CtrlPanel *p) { return p->scroll * H_ENTRY_PX + s_scroll_px; }

/* Continue an active empty-area scroll grab. Vertical finger motion only:
 *  - horizontal layout pans pixel-smooth (dy px added to the start offset);
 *  - vertical layout snaps one strip per V_SCROLL_STEP_PX of finger travel.
 * Horizontal also keeps a smoothed per-frame velocity (px/frame) so a flick can
 * glide on after release; vertical never builds velocity (it keeps snapping). */
static int scroll_grab_update(CtrlPanel *p, int py, int vert)
{
    int dy = s_scroll_start_py - py;   /* drag up (py falls) -> dy > 0 -> content up */
    if (!s_scroll_moved) {
        if (dy < SCROLL_TAP_SLOP && dy > -SCROLL_TAP_SLOP) return 0;   /* still a tap */
        s_scroll_moved = 1;
    }
    if (vert) return scroll_set_strip(p, s_scroll_start_off + dy / V_SCROLL_STEP_PX);
    int before = h_cur_total(p);
    int changed = scroll_set_px(p, s_scroll_start_off + dy);
    float last_delta = (float)(h_cur_total(p) - before);   /* 0 if clamped */
    s_scroll_vel = 0.5f * s_scroll_vel + 0.5f * last_delta;
    return changed;
}

/* Advance an in-progress inertia glide by one frame (horizontal layout only).
 * Returns 1 if the scroll position moved (caller must redraw), 0 otherwise.
 * Fully inert when s_scroll_vel == 0.0f, so the at-rest image is unchanged. */
static int glide_step(CtrlPanel *p)
{
    if (ctrl_panel_is_vertical(p)) { s_scroll_vel = 0.0f; return 0; }
    if (fabsf(s_scroll_vel) < SCROLL_MIN_V) { s_scroll_vel = 0.0f; return 0; }
    int step  = (int)(s_scroll_vel >= 0.0f ? s_scroll_vel + 0.5f : s_scroll_vel - 0.5f);
    int moved = scroll_set_px(p, h_cur_total(p) + step);
    s_scroll_vel *= SCROLL_FRICTION;
    /* Clamped at a boundary (couldn't move) -> stop so it doesn't spin forever. */
    if (!moved || fabsf(s_scroll_vel) < SCROLL_MIN_V) s_scroll_vel = 0.0f;
    return moved;
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
        /* losgelassen: Scroll-Griff beenden. A grab that never passed the slop
         * (a tap) and began on a name row selects that entry now -> tap-to-select
         * on the name is preserved. A grab that panned (s_scroll_moved) selects
         * nothing (pure pan). */
        int sel_changed = 0;
        int was_pan = s_scroll_grab && s_scroll_moved;   /* released after a real pan */
        if (s_scroll_grab && !s_scroll_moved && s_scroll_pending_sel >= 0 &&
            s_scroll_pending_sel < total_count(p)) {
            sel_changed = s_scroll_pending_sel != p->selected;
            p->selected = s_scroll_pending_sel;
        }
        s_scroll_grab        = 0;
        s_scroll_moved       = 0;
        s_scroll_pending_sel = -1;
        /* On the release frame: launch an inertia glide if the flick was fast
         * enough (horizontal only; vertical never builds velocity). A tap-select
         * (no pan) leaves the velocity at 0, so it never glides. */
        if (was_pan) {
            if (fabsf(s_scroll_vel) >= SCROLL_MIN_V) {
                if (s_scroll_vel >  SCROLL_VEL_CAP) s_scroll_vel =  SCROLL_VEL_CAP;
                if (s_scroll_vel < -SCROLL_VEL_CAP) s_scroll_vel = -SCROLL_VEL_CAP;
            } else {
                s_scroll_vel = 0.0f;
            }
        }
        /* losgelassen: eine gedrueckte Bang-Taste wieder leer zeichnen */
        int was_bang = p->active >= 0 && p->active < total_count(p) &&
                       entry_ptr(p, p->active)->type == CTRL_BANG;
        p->active = -1;
        /* Advance the glide one frame (inert when velocity == 0). main.c polls
         * this every frame, so the glide keeps redrawing until it settles. */
        int glided = glide_step(p);
        return was_bang || sel_changed || glided;
    }

    touchPosition tp;
    hidTouchRead(&tp);
    int vert = ctrl_panel_is_vertical(p);

    /* Any new touch-down cancels an in-progress glide immediately, so the user
     * can catch the list mid-glide (new grab/tap, slider, Touch-XY, etc.). */
    if (down & KEY_TOUCH) s_scroll_vel = 0.0f;

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

    /* Empty-area scroll grab (started below): keep panning until the finger
     * lifts. A pure tap (below the slop) does nothing and selects nothing. */
    if (s_scroll_grab) return scroll_grab_update(p, tp.py, vert);

    if (down & KEY_TOUCH) {
        p->active = -1;

        int select_only;
        int idx = hit_test(p, vert, tp.px, tp.py, &select_only);
        if (idx < 0 || select_only) {
            /* (c) Empty area OR a name/label row -> start a *pending* scroll grab.
             * Dragging past the slop turns it into a pure pan (never changes
             * p->selected); releasing within the slop is a tap that selects the
             * name's entry (pending_sel), or nothing for an empty-area tap (-1).
             * This frees the gaps between faders as scroll zone while keeping
             * today's tap-to-select on the name row. */
            s_scroll_grab        = 1;
            s_scroll_moved       = 0;
            s_scroll_start_py    = tp.py;
            s_scroll_start_off   = vert ? p->scroll
                                        : p->scroll * H_ENTRY_PX + s_scroll_px;
            s_scroll_pending_sel = select_only ? idx : -1;
            return 0;                   /* nothing moves until the slop is passed */
        }

        int changed = idx != p->selected;
        p->selected = idx;

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
    /* horizontal has one extra partially visible entry (pixel-smooth) */
    int vi_max = page_size(p) + (vert ? 0 : 1);
    if (vi < 0 || vi >= vi_max) return 0;   /* weggescrollt */
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

/* Optional vertical clip band for pixel drawing (set_px): a pixel outside
 * [s_clip_y0, s_clip_y1] is dropped. Default = whole screen (no effect); the
 * horizontal pixel-smooth renderer narrows it so partially scrolled entries are
 * clipped cleanly to the scroll band. */
static int s_clip_y0 = 0, s_clip_y1 = 239;
static void clip_set(int y0, int y1) { s_clip_y0 = y0; s_clip_y1 = y1; }
static void clip_reset(void)         { s_clip_y0 = 0;  s_clip_y1 = 239; }

static inline void set_px(u16 *fb, int sx, int sy, u16 col)
{
    if ((unsigned)sx >= 320u || (unsigned)sy >= 240u) return;
    if (sy < s_clip_y0 || sy > s_clip_y1) return;
    fb[sx * 240 + (239 - sy)] = col;
}

/* Band-clipped text (uses set_px, so it respects clip_set()). Draws the glyph
 * background too, so each line clears its own cells within the clip band. */
static void band_char(u16 *fb, int x, int y, char ch, u16 fg, u16 bg)
{
    unsigned char uc = (unsigned char)ch;
    if (uc < FONT5X8_FIRST || uc > FONT5X8_LAST) uc = '?';
    const uint8_t *g = FONT5X8[uc - FONT5X8_FIRST];
    for (int col = 0; col < FONT_ADV_X; col++) {
        uint8_t bits = (col < FONT_W) ? g[col] : 0;
        for (int row = 0; row < FONT_ADV_Y; row++) {
            int on = (row < 8) && ((bits >> row) & 1);
            set_px(fb, x + col, y + row, on ? fg : bg);
        }
    }
}

static void band_string(u16 *fb, int x, int y, const char *s, u16 fg, u16 bg)
{
    if (!s) return;
    for (; *s; s++) { if (x >= BOT_W) break; band_char(fb, x, y, *s, fg, bg); x += FONT_ADV_X; }
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
    s_has_touch = panel_has_touch(p);   /* band geometry depends on it */
    int count = total_count(p);

    u16 *fb = bottom_fb();
    if (!fb) return;

    /* Touch-XY frame: static rectangle + caption (touch sends touch-x/y). */
    if (panel_has_touch(p)) {
        Rect fr = touch_frame_rect(p);
        frame_rect(fb, fr, theme_fg_at((fr.y0 + fr.y1) / 2));
        draw_string_bot(fb, CTRL_CAP_X, CTRL_CAP_Y, "TOUCH X/Y",
                        theme_fg_at(CTRL_CAP_Y), TH.bg);
    }

    if (count == 0) return;

    int vert = ctrl_panel_is_vertical(p);
    int ps   = vert ? CTRL_PANEL_PAGE_V : h_page();
    int bang_held = (hidKeysHeld() & KEY_TOUCH) != 0;

    /* Horizontal pixel-smooth: clip every widget to the scroll band so the top
     * and bottom partially visible entries never bleed past the band edges. One
     * extra entry is drawn at the bottom for a smooth reveal; at s_scroll_px == 0
     * it falls fully below the band and is clipped away (identical base image). */
    int hb_top = h_band_top();
    int hb_bot = h_band_bot();
    int hi_max = vert ? ps : h_vis_count();
    if (!vert) clip_set(hb_top, hb_bot);

    for (int vi = 0; vi < hi_max; vi++) {
        int idx = p->scroll + vi;

        /* Widget-Flaeche loeschen (ersetzt das Leeren per Konsolentext) */
        if (vert) {
            fill_rect(fb, V_X_BASE + vi * V_STRIP_W, V_AREA_Y0 + cam_off_px(),
                      V_X_BASE + vi * V_STRIP_W + V_STRIP_W - 1, V_AREA_Y1, TH.bg);
        } else {
            if (idx >= count) break;   /* leere Eintraege loescht der Text */
            /* clear only this entry's widget rows (below its name); clipped to
             * the band so it never erases the neighbouring entries' names */
            int top = hb_top + vi * H_ENTRY_PX - s_scroll_px;
            fill_rect(fb, 0, top + 8, BOT_W - 1, top + H_ENTRY_PX - 1, TH.bg);
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
    if (!vert) clip_reset();
}

void ctrl_panel_scroll(CtrlPanel *p, int delta)
{
    if (!p || delta == 0) return;
    camtune_ensure();
    s_scroll_vel = 0.0f;   /* D-pad navigation cancels any in-progress glide */
    s_scroll_px = 0;       /* D-pad navigation realigns to the entry grid */
    int count = total_count(p);
    p->selected += delta;
    if (p->selected < 0) p->selected = 0;
    if (p->selected >= count) p->selected = count - 1;
    keep_visible(p);
}

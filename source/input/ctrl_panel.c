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
#include <stdio.h>
#include <string.h>

#define LAST_PANEL_ROW 29
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
#define V_VALUE_ROW   28
#define V_AREA_Y0     24    /* Zeilen 4..27 */
#define V_AREA_Y1    215
#define V_X_OFF        1    /* Fader x = strip*40 + 1 .. +30 (unter dem Kurznamen) */
#define V_FADER_W     30
#define V_Y0          28
#define V_Y1         211

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
    return ctrl_panel_is_vertical(p) ? CTRL_PANEL_PAGE_V : CTRL_PANEL_PAGE_H;
}

static int h_name_row(int vi) { return FIRST_ROW + vi * H_ENTRY_ROWS; }

/* Widget-Rechteck des sichtbaren Eintrags vi (Slider/Radio-Flaeche) */
static Rect widget_rect(int vert, int vi)
{
    Rect r;
    if (vert) {
        r.x0 = vi * V_STRIP_W + V_X_OFF;
        r.x1 = r.x0 + V_FADER_W - 1;
        r.y0 = V_Y0;
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
    int count = count_of(p);
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
    p->list     = list;
    p->scroll   = 0;
    p->selected = 0;
    p->active   = -1;
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
    int count = count_of(p);
    for (int vi = 0; vi < CTRL_PANEL_PAGE_H; vi++) {
        int row = h_name_row(vi);
        int idx = p->scroll + vi;
        if (idx >= count) continue;   /* area already cleared */
        draw_entry_line(fb, row, &p->list->ctrls[idx], idx == p->selected);
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
    int count = count_of(p);

    /* row 2: full name + value of the selected entry */
    if (p->selected >= 0 && p->selected < count)
        draw_entry_line(fb, 2, &p->list->ctrls[p->selected], 1);

    (void)V_STRIP_COLS;
    for (int k = 0; k < CTRL_PANEL_PAGE_V; k++) {
        int idx = p->scroll + k;
        if (idx >= count) continue;   /* area already cleared */
        const PdCtrl *c = &p->list->ctrls[idx];
        char sv[8];
        fmt_short(c, sv, sizeof(sv));
        int sel = idx == p->selected;
        draw_strip_cell(fb, k, V_LABEL_ROW, title_of(c),
                        sel ? TH.accent : row_fg(V_LABEL_ROW));
        draw_strip_cell(fb, k, V_VALUE_ROW, sv,
                        sel ? TH.accent : row_fg(V_VALUE_ROW));
    }
}

void ctrl_panel_draw(const CtrlPanel *p, const char *patch_name)
{
    if (!p) return;
    u16 *fb = bottom_fb();
    if (!fb) return;
    int count = count_of(p);

    /* clear the panel area (rows 1..29); row 30 (hint) belongs to main.c */
    draw_fill_bot(fb, 0, 0, BOT_W, PANEL_Y1, TH.bg);

    /* row 1: header (name shortened so the counter always fits) */
    char counter[16], head[LINE_CHARS + 1];
    snprintf(counter, sizeof(counter), "[%d/%d]",
             count > 0 ? p->selected + 1 : 0, count);
    int name_w = LINE_CHARS - 6 - 1 - (int)strlen(counter);
    snprintf(head, sizeof(head), "CTRL: %-*.*s %s", name_w, name_w,
             patch_name ? patch_name : "", counter);
    draw_line(fb, 1, head, row_fg(1));

    if (count == 0) {
        draw_line(fb, FIRST_ROW,     "Keine steuerbaren Receiver gefunden.", row_fg(FIRST_ROW));
        draw_line(fb, FIRST_ROW + 1, "Tipp: [r name] oder Kommentar", TH.dim);
        draw_line(fb, FIRST_ROW + 2, "\"@ctrl name lo hi\" im Patch.", TH.dim);
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
    int count = count_of(p);
    int ps    = vert ? CTRL_PANEL_PAGE_V : CTRL_PANEL_PAGE_H;
    *select_only = 0;

    for (int vi = 0; vi < ps; vi++) {
        int idx = p->scroll + vi;
        if (idx >= count) break;
        const PdCtrl *c = &p->list->ctrls[idx];
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
                       ((py >= (V_LABEL_ROW - 1) * 8 && py < V_LABEL_ROW * 8) ||
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
    if (!p || count_of(p) == 0) return 0;

    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();

    if (!(held & KEY_TOUCH)) {
        /* losgelassen: eine gedrueckte Bang-Taste wieder leer zeichnen */
        int was_bang = p->active >= 0 && p->active < p->list->count &&
                       p->list->ctrls[p->active].type == CTRL_BANG;
        p->active = -1;
        return was_bang;
    }

    touchPosition tp;
    hidTouchRead(&tp);
    int vert = ctrl_panel_is_vertical(p);

    if (down & KEY_TOUCH) {
        p->active = -1;

        int select_only;
        int idx = hit_test(p, vert, tp.px, tp.py, &select_only);
        if (idx < 0) return 0;          /* daneben: nichts passiert */

        int changed = idx != p->selected;
        p->selected = idx;
        if (select_only) return changed;

        p->active = idx;
        PdCtrl *c = &p->list->ctrls[idx];
        Rect w = widget_rect(vert, idx - p->scroll);
        if (c->type == CTRL_TOGGLE) {
            c->val = (c->val >= 0.5f) ? 0.0f : 1.0f;   /* nur beim Aufsetzen */
            send_ctrl(c);
        } else if (c->type == CTRL_BANG) {
            pd_input_bang(c->name);                    /* nur beim Aufsetzen */
        } else if (c->type == CTRL_RADIO) {
            c->val = radio_value(c, vert, w, tp.px, tp.py);
            send_ctrl(c);
        } else {
            c->val = slider_value(c, vert, w, tp.px, tp.py);
            send_ctrl(c);
        }
        return 1;
    }

    /* Gehaltener Touch: aktiven Slider/Radio nachziehen */
    if (p->active < 0 || p->active >= p->list->count) return 0;
    int vi = p->active - p->scroll;
    if (vi < 0 || vi >= page_size(p)) return 0;   /* weggescrollt */
    PdCtrl *c = &p->list->ctrls[p->active];
    Rect w = widget_rect(vert, vi);
    float v;
    if (c->type == CTRL_FLOAT)      v = slider_value(c, vert, w, tp.px, tp.py);
    else if (c->type == CTRL_RADIO) v = radio_value(c, vert, w, tp.px, tp.py);
    else return 0;

    if (v == c->val) return 0;
    c->val = v;
    send_ctrl(c);
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
    int count = count_of(p);
    if (count == 0) return;

    u16 *fb = bottom_fb();
    if (!fb) return;

    int vert = ctrl_panel_is_vertical(p);
    int ps   = vert ? CTRL_PANEL_PAGE_V : CTRL_PANEL_PAGE_H;
    int bang_held = (hidKeysHeld() & KEY_TOUCH) != 0;

    for (int vi = 0; vi < ps; vi++) {
        int idx = p->scroll + vi;

        /* Widget-Flaeche loeschen (ersetzt das Leeren per Konsolentext) */
        if (vert) {
            fill_rect(fb, vi * V_STRIP_W, V_AREA_Y0, vi * V_STRIP_W + V_STRIP_W - 1,
                      V_AREA_Y1, TH.bg);
        } else {
            if (idx >= count) break;   /* leere Eintraege loescht der Text */
            int y = h_name_row(vi) * 8;
            fill_rect(fb, 0, y, BOT_W - 1, y + (H_ENTRY_ROWS - 1) * 8 - 1, TH.bg);
        }
        if (idx >= count) continue;

        const PdCtrl *c = &p->list->ctrls[idx];
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
    if (!p || count_of(p) == 0 || delta == 0) return;
    p->selected += delta;
    if (p->selected < 0) p->selected = 0;
    if (p->selected >= p->list->count) p->selected = p->list->count - 1;
    keep_visible(p);
}

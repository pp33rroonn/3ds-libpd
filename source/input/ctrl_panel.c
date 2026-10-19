/* ctrl_panel.c - scrollbares Slider-Panel fuer den CTRL-Modus
 * (Layout und Bedienung siehe ctrl_panel.h)
 */

#include "ctrl_panel.h"
#include "../app_state.h"

#include <3ds.h>
#include "pd_bridge.h"   /* pd_input_float/bang: routed to the active slot */
#include <stdio.h>
#include <string.h>

/* Fader: Spalte 2..39 (1-basiert) = 38 Zellen, x = 8..311 px
 * Rahmen-Zeichen belegen Spalte 2 und 39; innere Breite = 36 Zellen.
 * Thumb = 2 invertierte Zeichen; verschiebt sich ueber 35 Positionen (0..34). */
#define BAR_COL     2
#define BAR_CELLS   38
#define BAR_INNER   (BAR_CELLS - 2)     /* 36 Innenfelder */
#define THUMB_W     2                   /* Breite des Fader-Griffs in Zellen */
#define THUMB_POS   (BAR_INNER - THUMB_W)  /* 34 moegliche Positionen */
#define BAR_X0      ((BAR_COL - 1) * 8) /* 8 px */
#define BAR_PX      (BAR_CELLS * 8)     /* 304 px */

/* Jeder Eintrag belegt ENTRY_ROWS Zeilen: Name, Balken, Leerzeile */
#define FIRST_ROW   3
#define ENTRY_ROWS  3
#define ENTRY_PX    (ENTRY_ROWS * 8)    /* 24 px pro Eintrag */
#define ROW_Y0      ((FIRST_ROW - 1) * 8)
#define ROWS_Y1     (ROW_Y0 + ENTRY_PX * CTRL_PANEL_PAGE_SIZE)

#define LAST_PANEL_ROW 29
#define LINE_W         39   /* letzte Spalte frei lassen (kein Umbruch) */

/* Textkonsole-Farben */
#define COL_PINK    "\x1b[35m"
#define COL_RESET   "\x1b[0m"

/* Pixel-Fader-Geometrie (unterer Bildschirm 320x240, RGB565 durch consoleInit):
 *   Kasten: x0=BAR_X0(8)..x1=311, Hoehe FADER_H, Abstand FADER_GAP oben zur Namenszeile
 *   Rahmen: 1 px weiss rundum, innen schwarz, Griff 1 px weiss */
#define FADER_H     12
#define FADER_GAP    2
#define FADER_TH     1      /* Rahmen- und Griffbreite in px */

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

static void blank_row(int row)
{
    printf("\x1b[%d;1H%*s", row, LINE_W, "");
}

static void draw_entry(const CtrlPanel *p, int vi, int idx)
{
    int name_row = FIRST_ROW + vi * ENTRY_ROWS;
    int bar_row  = name_row + 1;
    int sep_row  = name_row + 2;   /* Leerzeile als Abstand */

    if (idx < 0 || idx >= p->list->count) {
        blank_row(name_row);
        blank_row(bar_row);
        blank_row(sep_row);
        return;
    }

    const PdCtrl *c = &p->list->ctrls[idx];
    const char *title = c->label[0] ? c->label : c->name;
    char value[16];
    if (c->type == CTRL_TOGGLE) {
        snprintf(value, sizeof(value), "%s", c->val >= 0.5f ? "AN" : "AUS");
    } else if (c->type == CTRL_BANG) {
        snprintf(value, sizeof(value), "%s", "BANG");
    } else if (c->type == CTRL_RADIO) {
        snprintf(value, sizeof(value), "%d/%d", (int)c->val + 1, c->steps);
    } else {
        /* Zahlenformat: kein wissenschaftlicher Stil, passende Nachkommastellen */
        float av = (c->val < 0.f ? -c->val : c->val);
        if (c->val == (float)(int)c->val && av < 100000.f)
            snprintf(value, sizeof(value), "%d", (int)c->val);
        else if (av < 10.f)
            snprintf(value, sizeof(value), "%.3f", (double)c->val);
        else if (av < 100.f)
            snprintf(value, sizeof(value), "%.2f", (double)c->val);
        else if (av < 1000.f)
            snprintf(value, sizeof(value), "%.1f", (double)c->val);
        else
            snprintf(value, sizeof(value), "%.0f", (double)c->val);
    }

    /* Namenszeile: ausgewaehlt -> Text in Pink, sonst Normal */
    if (idx == p->selected) {
        printf("\x1b[%d;1H" COL_PINK ">%-27.27s %10.10s" COL_RESET,
               name_row, title, value);
    } else {
        printf("\x1b[%d;1H %-27.27s %10.10s", name_row, title, value);
    }

    /* Fader-Zeile: wird komplett per Pixel gezeichnet (ctrl_panel_draw_faders).
     * Hier nur leeren damit kein Konsolen-Text daruebersteht. */
    blank_row(bar_row);

    /* Leerzeile als Abstand zum naechsten Eintrag */
    blank_row(sep_row);
}

static void send_ctrl(const PdCtrl *c)
{
    pd_input_float(c->name, c->val);
}

/* x-Position (px) -> Sliderwert */
static float value_from_x(const PdCtrl *c, int px)
{
    float t = (float)(px - BAR_X0) / (float)(BAR_PX - 1);
    t = clampf(t, 0.0f, 1.0f);
    return c->lo + t * (c->hi - c->lo);
}

/* x-Position (px) -> Radio-Zelle 0..steps-1 */
static float radio_from_x(const PdCtrl *c, int px)
{
    int steps = c->steps > 0 ? c->steps : 1;
    int cell  = (px - BAR_X0) * steps / BAR_PX;
    if (cell < 0) cell = 0;
    if (cell > steps - 1) cell = steps - 1;
    return (float)cell;
}

/* Auswahl im sichtbaren Bereich halten */
static void keep_visible(CtrlPanel *p)
{
    int count = p->list ? p->list->count : 0;
    int max_scroll = count - CTRL_PANEL_PAGE_SIZE;
    if (max_scroll < 0) max_scroll = 0;

    if (p->selected < p->scroll) p->scroll = p->selected;
    if (p->selected >= p->scroll + CTRL_PANEL_PAGE_SIZE)
        p->scroll = p->selected - CTRL_PANEL_PAGE_SIZE + 1;
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

void ctrl_panel_draw(const CtrlPanel *p, const char *patch_name)
{
    if (!p) return;
    int count = p->list ? p->list->count : 0;

    /* Kopfzeile */
    char head[64];
    snprintf(head, sizeof(head), "CTRL: %-20.20s [%d/%d]",
             patch_name ? patch_name : "",
             count > 0 ? p->selected + 1 : 0, count);
    printf("\x1b[1;1H%-39.39s", head);
    blank_row(2);

    if (count == 0) {
        printf("\x1b[%d;1H%-39.39s", FIRST_ROW, "Keine steuerbaren Receiver gefunden.");
        printf("\x1b[%d;1H%-39.39s", FIRST_ROW + 1, "Tipp: [r name] oder Kommentar");
        printf("\x1b[%d;1H%-39.39s", FIRST_ROW + 2, "\"@ctrl name lo hi\" im Patch.");
        for (int row = FIRST_ROW + 3; row <= LAST_PANEL_ROW; row++) blank_row(row);
        return;
    }

    for (int vi = 0; vi < CTRL_PANEL_PAGE_SIZE; vi++) {
        draw_entry(p, vi, p->scroll + vi);
    }
    /* Letzte sep_row des letzten Eintrags (Zeile 29) wird von
     * draw_entry selbst als Leerzeile gezeichnet. */
}

int ctrl_panel_poll_touch(CtrlPanel *p)
{
    if (!p || !p->list || p->list->count == 0) return 0;

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

    if (down & KEY_TOUCH) {
        p->active = -1;

        /* Hinweiszeile unten rechts (ehem. OPT-Bereich) ignorieren */
        if (tp.px >= OPT_BTN_X0 && tp.py >= OPT_BTN_CTRL_Y0) return 0;
        if (tp.py < ROW_Y0 || tp.py >= ROWS_Y1) return 0;

        int idx = p->scroll + (tp.py - ROW_Y0) / ENTRY_PX;
        if (idx >= p->list->count) return 0;

        p->active   = idx;
        p->selected = idx;

        PdCtrl *c = &p->list->ctrls[idx];
        if (c->type == CTRL_TOGGLE) {
            /* Toggle: nur beim Aufsetzen umschalten */
            c->val = (c->val >= 0.5f) ? 0.0f : 1.0f;
            send_ctrl(c);
        } else if (c->type == CTRL_BANG) {
            pd_input_bang(c->name);   /* nur beim Aufsetzen */
        } else if (c->type == CTRL_RADIO) {
            c->val = radio_from_x(c, tp.px);
            send_ctrl(c);
        } else {
            c->val = value_from_x(c, tp.px);
            send_ctrl(c);
        }
        return 1;   /* Auswahl und/oder Wert geaendert */
    }

    /* Gehaltener Touch: aktiven Slider nachziehen */
    if (p->active < 0 || p->active >= p->list->count) return 0;
    PdCtrl *c = &p->list->ctrls[p->active];
    float v;
    if (c->type == CTRL_FLOAT)      v = value_from_x(c, tp.px);
    else if (c->type == CTRL_RADIO) v = radio_from_x(c, tp.px);   /* Zelle wechseln */
    else return 0;

    if (v == c->val) return 0;
    c->val = v;
    send_ctrl(c);
    return 1;
}

/* ---- Pixel-Fader -------------------------------------------------------- */

/* consoleInit() stellt den unteren Bildschirm auf RGB565 (2 Byte/Pixel) und
 * schaltet Double-Buffering ab. Der Puffer ist um 90 Grad gedreht gespeichert
 * (spaltenweise, y von unten): Index = sx * 240 + (239 - sy). */
#define PX_WHITE 0xFFFFu
#define PX_BLACK 0x0000u

static inline void set_px(u16 *fb, int sx, int sy, u16 col)
{
    if ((unsigned)sx >= 320u || (unsigned)sy >= 240u) return;
    fb[sx * 240 + (239 - sy)] = col;
}

static void fill_rect(u16 *fb, int x0, int y0, int x1, int y1, u16 col)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            set_px(fb, x, y, col);
}

static void frame_rect(u16 *fb, int x0, int y0, int x1, int y1)
{
    for (int x = x0; x <= x1; x++) { set_px(fb, x, y0, PX_WHITE); set_px(fb, x, y1, PX_WHITE); }
    for (int y = y0; y <= y1; y++) { set_px(fb, x0, y, PX_WHITE); set_px(fb, x1, y, PX_WHITE); }
}

/* Toggle: quadratischer Kasten, AN = weisses Kreuz (wie in Pd) */
static void draw_toggle(u16 *fb, const PdCtrl *c, int x0, int y0, int y1)
{
    int sz = y1 - y0;                     /* Kastenkante = Faderhoehe */
    fill_rect(fb, x0 - 1, y0, x0 + BAR_PX, y1, PX_BLACK);   /* Zeile leeren */
    frame_rect(fb, x0, y0, x0 + sz, y1);
    if (c->val >= 0.5f) {
        for (int i = 2; i <= sz - 2; i++) {
            set_px(fb, x0 + i, y0 + i, PX_WHITE);
            set_px(fb, x0 + sz - i, y0 + i, PX_WHITE);
        }
    }
}

/* Bang: Kasten mit Kreis, gefuellt solange gedrueckt */
static void draw_bang(u16 *fb, int pressed, int x0, int y0, int y1)
{
    int sz = y1 - y0;
    fill_rect(fb, x0 - 1, y0, x0 + BAR_PX, y1, PX_BLACK);
    frame_rect(fb, x0, y0, x0 + sz, y1);
    int cx2 = 2 * x0 + sz, cy2 = 2 * y0 + sz;   /* doppelte Mittelpunkt-Koordinaten */
    int r2  = sz - 3;                             /* doppelter Radius */
    for (int y = y0 + 1; y < y1; y++) {
        for (int x = x0 + 1; x < x0 + sz; x++) {
            int dx = 2 * x - cx2, dy = 2 * y - cy2;
            int d  = dx * dx + dy * dy;
            if (pressed ? d <= r2 * r2
                        : (d <= r2 * r2 && d >= (r2 - 2) * (r2 - 2)))
                set_px(fb, x, y, PX_WHITE);
        }
    }
}

/* Radio: steps Zellen ueber die Balkenbreite, gewaehlte Zelle gefuellt.
 * Bei sehr vielen Zellen (< 4 px) nur Rahmen + gefuellte Zelle. */
static void draw_radio(u16 *fb, const PdCtrl *c, int x0, int y0, int x1, int y1)
{
    int steps = c->steps > 0 ? c->steps : 1;
    int sel   = (int)c->val;
    if (sel < 0) sel = 0;
    if (sel > steps - 1) sel = steps - 1;
    int w = x1 - x0 + 1;

    fill_rect(fb, x0, y0, x1, y1, PX_BLACK);
    frame_rect(fb, x0, y0, x1, y1);
    int grid = (w / steps) >= 4;
    for (int i = 0; i < steps; i++) {
        int cx0 = x0 + i * w / steps;
        int cx1 = x0 + (i + 1) * w / steps - 1;
        if (grid && i > 0)
            for (int y = y0; y <= y1; y++) set_px(fb, cx0, y, PX_WHITE);
        if (i == sel) fill_rect(fb, cx0 + 2, y0 + 2, cx1 - 2 < cx0 + 2 ? cx0 + 2 : cx1 - 2,
                                y1 - 2, PX_WHITE);
    }
}

/* Zeichnet alle sichtbaren Fader als Pixel-Rechtecke in den Framebuffer.
 * Muss nach ctrl_panel_draw() aufgerufen werden (Text zuerst, dann Pixel). */
void ctrl_panel_draw_faders(const CtrlPanel *p)
{
    if (!p || !p->list || p->list->count == 0) return;

    /* Konsole laeuft auf dem unteren Bildschirm mit RGB565 */
    if (gfxGetScreenFormat(GFX_BOTTOM) != GSP_RGB565_OES) return;

    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!fb) return;

    for (int vi = 0; vi < CTRL_PANEL_PAGE_SIZE; vi++) {
        int idx = p->scroll + vi;
        if (idx >= p->list->count) break;

        const PdCtrl *c = &p->list->ctrls[idx];

        /* Kasten-Koordinaten */
        int bar_row = FIRST_ROW + vi * ENTRY_ROWS + 1; /* 1-basierte Konsolen-Zeile */
        int y0 = (bar_row - 1) * 8 + FADER_GAP;        /* obere Kante Kasten */
        int y1 = y0 + FADER_H - 1;                      /* untere Kante (inkl.) */
        int x0 = BAR_X0;                                 /* 8 */
        int x1 = BAR_X0 + BAR_PX - 1;                   /* 311 */

        if (c->type == CTRL_TOGGLE) { draw_toggle(fb, c, x0, y0, y1); continue; }
        if (c->type == CTRL_BANG) {
            draw_bang(fb, p->active == idx && (hidKeysHeld() & KEY_TOUCH), x0, y0, y1);
            continue;
        }
        if (c->type == CTRL_RADIO) { draw_radio(fb, c, x0, y0, x1, y1); continue; }

        /* 1-px weisser Rahmen rundum */
        for (int x = x0; x <= x1; x++) {
            set_px(fb, x, y0, PX_WHITE);
            set_px(fb, x, y1, PX_WHITE);
        }
        for (int y = y0 + 1; y < y1; y++) {
            set_px(fb, x0, y, PX_WHITE);
            set_px(fb, x1, y, PX_WHITE);
        }

        /* Innenflaeche schwarz (loescht den alten Griff) */
        for (int y = y0 + FADER_TH; y <= y1 - FADER_TH; y++)
            for (int x = x0 + FADER_TH; x <= x1 - FADER_TH; x++)
                set_px(fb, x, y, PX_BLACK);

        /* Weisser 1-px-Griff: Position haengt vom aktuellen Wert ab */
        int inner_x0 = x0 + FADER_TH;
        int inner_x1 = x1 - FADER_TH;
        int inner_w  = inner_x1 - inner_x0;  /* 301 px bei FADER_TH=1 */
        float frac   = ctrl_fraction(c);
        int gx = inner_x0 + (int)(frac * (float)inner_w + 0.5f);
        if (gx < inner_x0) gx = inner_x0;
        if (gx > inner_x1) gx = inner_x1;
        for (int y = y0; y <= y1; y++)
            set_px(fb, gx, y, PX_WHITE);
    }
}

void ctrl_panel_scroll(CtrlPanel *p, int delta)
{
    if (!p || !p->list || p->list->count == 0 || delta == 0) return;
    p->selected += delta;
    if (p->selected < 0) p->selected = 0;
    if (p->selected >= p->list->count) p->selected = p->list->count - 1;
    keep_visible(p);
}

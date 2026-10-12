/* ctrl_panel.c - scrollbares Slider-Panel fuer den CTRL-Modus
 * (Layout und Bedienung siehe ctrl_panel.h)
 */

#include "ctrl_panel.h"
#include "../app_state.h"

#include <3ds.h>
#include <z_libpd.h>   /* libpd_float() */
#include <stdio.h>
#include <string.h>

/* Balken: Spalte 2..39 (1-basiert) = 38 Zellen, x = 8..311 px */
#define BAR_COL     2
#define BAR_CELLS   38
#define BAR_X0      ((BAR_COL - 1) * 8)
#define BAR_PX      (BAR_CELLS * 8)

/* Eintrag i belegt die Pixelzeilen ROW_Y0 + 16*i .. +15 (Name + Balken) */
#define FIRST_ROW   3
#define ROW_Y0      ((FIRST_ROW - 1) * 8)
#define ROWS_Y1     (ROW_Y0 + 16 * CTRL_PANEL_PAGE_SIZE)

#define LAST_PANEL_ROW 29
#define LINE_W         39   /* letzte Spalte frei lassen (kein Umbruch) */

#define COL_FILLED  "\x1b[37;44m"   /* weiss auf blau */
#define COL_EMPTY   "\x1b[30;47m"   /* schwarz auf grau */
#define COL_SEL     "\x1b[30;43m"   /* schwarz auf gelb (Auswahl) */
#define COL_RESET   "\x1b[0m"

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
    int name_row = FIRST_ROW + vi * 2;
    int bar_row  = name_row + 1;

    if (idx < 0 || idx >= p->list->count) {
        blank_row(name_row);
        blank_row(bar_row);
        return;
    }

    const PdCtrl *c = &p->list->ctrls[idx];
    char value[16];
    if (c->type == CTRL_TOGGLE) {
        snprintf(value, sizeof(value), "%s", c->val >= 0.5f ? "AN" : "AUS");
    } else {
        snprintf(value, sizeof(value), "%.3g", (double)c->val);
    }

    /* Namenszeile: Auswahl-Markierung + Name + Wert (rechtsbuendig) */
    if (idx == p->selected) {
        printf("\x1b[%d;1H%s>%-27.27s %10.10s" COL_RESET,
               name_row, COL_SEL, c->name, value);
    } else {
        printf("\x1b[%d;1H %-27.27s %10.10s", name_row, c->name, value);
    }

    /* Balkenzeile: gefuellter und leerer Anteil per Hintergrundfarbe */
    int filled = (int)(ctrl_fraction(c) * (float)BAR_CELLS + 0.5f);
    if (filled > BAR_CELLS) filled = BAR_CELLS;
    printf("\x1b[%d;1H \x1b[%d;%dH" COL_FILLED "%*s" COL_EMPTY "%*s" COL_RESET,
           bar_row, bar_row, BAR_COL,
           filled, "", BAR_CELLS - filled, "");
}

static void send_ctrl(const PdCtrl *c)
{
    libpd_float(c->name, c->val);
}

/* x-Position (px) -> Sliderwert */
static float value_from_x(const PdCtrl *c, int px)
{
    float t = (float)(px - BAR_X0) / (float)(BAR_PX - 1);
    t = clampf(t, 0.0f, 1.0f);
    return c->lo + t * (c->hi - c->lo);
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
    blank_row(LAST_PANEL_ROW);
}

int ctrl_panel_poll_touch(CtrlPanel *p)
{
    if (!p || !p->list || p->list->count == 0) return 0;

    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();

    if (!(held & KEY_TOUCH)) {
        p->active = -1;   /* losgelassen */
        return 0;
    }

    touchPosition tp;
    hidTouchRead(&tp);

    if (down & KEY_TOUCH) {
        p->active = -1;

        /* OPT-Bereich gehoert main.c */
        if (tp.px >= OPT_BTN_X0 && tp.py >= OPT_BTN_CTRL_Y0) return 0;
        if (tp.py < ROW_Y0 || tp.py >= ROWS_Y1) return 0;

        int idx = p->scroll + (tp.py - ROW_Y0) / 16;
        if (idx >= p->list->count) return 0;

        p->active   = idx;
        p->selected = idx;

        PdCtrl *c = &p->list->ctrls[idx];
        if (c->type == CTRL_TOGGLE) {
            /* Toggle: nur beim Aufsetzen umschalten */
            c->val = (c->val >= 0.5f) ? 0.0f : 1.0f;
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
    if (c->type != CTRL_FLOAT) return 0;

    float v = value_from_x(c, tp.px);
    if (v == c->val) return 0;
    c->val = v;
    send_ctrl(c);
    return 1;
}

void ctrl_panel_scroll(CtrlPanel *p, int delta)
{
    if (!p || !p->list || p->list->count == 0 || delta == 0) return;
    p->selected += delta;
    if (p->selected < 0) p->selected = 0;
    if (p->selected >= p->list->count) p->selected = p->list->count - 1;
    keep_visible(p);
}

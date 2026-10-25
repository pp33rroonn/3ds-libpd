#pragma once
/* ctrl_panel.h - scrollbares Fader-Panel fuer den CTRL-Modus
 *
 * Shows the receivers found by pd_parse() on the bottom screen (RGB565,
 * 320x240). Everything is drawn as pixels: text with font5x8 on an 8-px
 * row grid (row r = 1..30 starts at y = (r-1)*8, matching the touch
 * hitboxes), widgets afterwards. Colours follow the EYE theme (TH.bg,
 * theme_fg_at(y) for text/widgets, TH.accent for the selection, TH.dim for
 * hints). Row 30 (hint line) is drawn by main.c.
 *
 * Zwei Layouts (OPTIONS -> "Faders": Horizontal / Vertical / Auto):
 *
 * HORIZONTAL (5 Eintraege pro Seite, je 5 Zeilen = 40 px)
 *   Zeile 1          : "CTRL: <patch> [ausgewaehlt/anzahl]"
 *   Zeile 3+5*i      : name + value (selected = TH.accent with '>')
 *   Zeilen 4..7+5*i  : Widget, 24 px hoch, x = 8..311
 *
 * VERTIKAL (8 Streifen pro Seite, je 40 px breit)
 *   Zeile 1          : Kopfzeile
 *   Zeile 2          : voller Name + Wert des ausgewaehlten Eintrags
 *   Zeile 3          : short name (up to 6 chars) per strip
 *   Zeilen 4..27     : Widget, Fader 30 x 184 px (unten = Minimum)
 *   Zeile 28         : Kurzwert pro Streifen
 *
 * Auto: vertikal, wenn der Patch mehr vsl/vradio als hsl/hradio enthaelt.
 *
 * Widgets: Slider (Rahmen + 1-px-Griff), Toggle (24-px-Kasten, AN = Kreuz),
 * Bang (24-px-Kasten mit Kreis, gefuellt solange gedrueckt), Radio (Zellen).
 *
 * Touch: wirkt nur innerhalb des Widgets (Toggle/Bang: nur im Kasten,
 * +-HIT_TOL px). Antippen des Namens waehlt nur aus. Slider/Radio koennen
 * gezogen werden; der beim Aufsetzen getroffene Eintrag bleibt bis zum
 * Loslassen aktiv. Toggles schalten, Bangs senden nur beim Aufsetzen.
 * Jede Aenderung geht per pd_input_float()/pd_input_bang() an den Slot;
 * Werte aus dem Patch kommen ueber pd_bridge zurueck ins Panel.
 */

#include "../fs/pd_parser.h"

/* Layout-Modi (Reihenfolge = OPTIONS-Eintrag "Faders") */
#define CTRL_LAYOUT_HORIZ 0
#define CTRL_LAYOUT_VERT  1
#define CTRL_LAYOUT_AUTO  2
#define CTRL_LAYOUT_COUNT 3

#define CTRL_PANEL_PAGE_H 5   /* Eintraege pro Seite, horizontal */
/* Vertical: 7 strips (not 8) so the strip block is centred with symmetric
 * left/right margins ((320 - 7*40)/2 = 20 px each) that double as the vertical
 * scroll zone - strips no longer touch the screen edge. */
#define CTRL_PANEL_PAGE_V 7   /* Streifen pro Seite, vertikal */

typedef struct {
    PdCtrlList *list;
    int scroll;     /* Index des obersten/linken sichtbaren Eintrags */
    int selected;   /* ausgewaehlter Eintrag (hervorgehoben) */
    int active;     /* per Touch gegriffener Eintrag, -1 = keiner */
} CtrlPanel;

/** Panel an eine Liste binden und Scroll/Auswahl zuruecksetzen. */
void ctrl_panel_init(CtrlPanel *p, PdCtrlList *list);

/**
 * ctrl_panel_set_layout - Layout-Modus setzen (CTRL_LAYOUT_*, global fuer
 * alle Slots). Scroll wird sofort an die neue Seitengroesse angepasst.
 * Danach muss der Aufrufer den unteren Bildschirm komplett neu zeichnen.
 */
void ctrl_panel_set_layout(CtrlPanel *p, int mode);

/** 1 wenn das Panel aktuell vertikal dargestellt wird. */
int ctrl_panel_is_vertical(const CtrlPanel *p);

/**
 * ctrl_panel_draw - clear the panel area (rows 1..29) with TH.bg and draw
 * the panel text as pixels into the bottom framebuffer (fetched here; does
 * nothing unless the bottom screen is RGB565). Widgets follow with
 * ctrl_panel_draw_faders().
 */
void ctrl_panel_draw(const CtrlPanel *p, const char *patch_name);

/**
 * ctrl_panel_poll_touch - Touch auswerten (einmal pro Frame, nach
 * hidScanInput()).
 * @return 1 wenn sich Wert oder Auswahl geaendert hat (neu zeichnen), sonst 0.
 */
int ctrl_panel_poll_touch(CtrlPanel *p);

/** Auswahl um delta Eintraege verschieben; Scroll folgt der Auswahl. */
void ctrl_panel_scroll(CtrlPanel *p, int delta);

/**
 * ctrl_panel_set_cam_reserve - reserve (on != 0) or release (on == 0) a fixed
 * block at the top of the bottom screen for the 160x120 camera image. When
 * reserved, the whole fader area is pushed down so it never overlaps the image
 * (horizontal: fewer entries per page + scroll; vertical: shorter faders).
 * The image itself is drawn by main.c via cam_view after ctrl_panel_draw().
 * Call once on every CAM-mode change, then redraw the bottom screen.
 */
void ctrl_panel_set_cam_reserve(int on);

/* ctrl_panel_set_cam_tune - show the 5 "Cam Tune" faders only on cam patches
 * (on != 0). Off by default, so non-cam patches show just their own widgets. */
void ctrl_panel_set_cam_tune(int on);

/**
 * ctrl_panel_draw_faders - Widgets als Pixel in den Framebuffer zeichnen.
 * Muss NACH ctrl_panel_draw() aufgerufen werden (Text zuerst, Pixel danach).
 * Nur im CTRL-Modus aufrufen.
 */
void ctrl_panel_draw_faders(const CtrlPanel *p);

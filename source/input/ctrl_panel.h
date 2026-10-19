#pragma once
/* ctrl_panel.h - scrollbares Fader-Panel fuer den CTRL-Modus
 *
 * Zeigt die von pd_parse() gefundenen Empfaenger auf dem unteren
 * Bildschirm (Konsole 40x30 Zeichen, 8x8 px pro Zelle). Text kommt von der
 * Konsole, die Widgets werden danach als Pixel gezeichnet. Zeile 30
 * (Hinweiszeile) zeichnet main.c.
 *
 * Zwei Layouts (OPTIONS -> "Faders": Horizontal / Vertical / Auto):
 *
 * HORIZONTAL (5 Eintraege pro Seite, je 5 Zeilen = 40 px)
 *   Zeile 1          : "CTRL: <patch> [ausgewaehlt/anzahl]"
 *   Zeile 3+5*i      : Name + Wert (ausgewaehlt = Pink mit '>')
 *   Zeilen 4..7+5*i  : Widget, 24 px hoch, x = 8..311
 *
 * VERTIKAL (8 Streifen pro Seite, je 40 px breit)
 *   Zeile 1          : Kopfzeile
 *   Zeile 2          : voller Name + Wert des ausgewaehlten Eintrags
 *   Zeile 3          : Kurzname (4 Zeichen) pro Streifen
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
#define CTRL_PANEL_PAGE_V 8   /* Streifen pro Seite, vertikal */

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
 * ctrl_panel_draw - Text des Panels zeichnen (Zeilen 1..29).
 * Der Aufrufer muss vorher die untere Konsole per consoleSelect() aktiv
 * machen. Widget-Flaechen werden nicht per Text geloescht, das erledigt
 * ctrl_panel_draw_faders() (nach einem Layoutwechsel: consoleClear()).
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
 * ctrl_panel_draw_faders - Widgets als Pixel in den Framebuffer zeichnen.
 * Muss NACH ctrl_panel_draw() aufgerufen werden (Text zuerst, Pixel danach).
 * Nur im CTRL-Modus aufrufen.
 */
void ctrl_panel_draw_faders(const CtrlPanel *p);

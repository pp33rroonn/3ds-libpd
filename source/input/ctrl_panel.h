#pragma once
/* ctrl_panel.h - scrollbares Slider-Panel fuer den CTRL-Modus
 *
 * Zeigt die von pd_parse() gefundenen Empfaenger auf dem unteren
 * Bildschirm (Konsole 40x30 Zeichen, 8x8 px pro Zelle):
 *
 *   Zeile 1      : Kopfzeile "CTRL: <patch> [ausgewaehlt/anzahl]"
 *   Zeile 2      : leer
 *   Zeile 3+2*i  : Name + aktueller Wert   (i = 0..CTRL_PANEL_PAGE_SIZE-1)
 *   Zeile 4+2*i  : Balken (38 Zeichen breit, Spalte 2..39)
 *   Zeile 29     : leer
 *   Zeile 30     : wird von main.c gezeichnet (Hinweis + OPT-Button)
 *
 * Touch: Name- oder Balkenzeile antippen waehlt den Eintrag. Bei Slidern
 * setzt die x-Position den Wert (Ziehen moeglich, der beim Aufsetzen
 * getroffene Slider bleibt bis zum Loslassen aktiv). Toggles schalten
 * nur bei neuem Touch um. Jede Aenderung geht per libpd_float() an PD.
 */

#include "../fs/pd_parser.h"

#define CTRL_PANEL_PAGE_SIZE 13

typedef struct {
    PdCtrlList *list;
    int scroll;     /* Index des obersten sichtbaren Eintrags */
    int selected;   /* ausgewaehlter Eintrag (hervorgehoben) */
    int active;     /* per Touch gegriffener Eintrag, -1 = keiner */
} CtrlPanel;

/** Panel an eine Liste binden und Scroll/Auswahl zuruecksetzen. */
void ctrl_panel_init(CtrlPanel *p, PdCtrlList *list);

/**
 * ctrl_panel_draw - Panel zeichnen (Zeilen 1..29).
 * Der Aufrufer muss vorher die untere Konsole per consoleSelect() aktiv
 * machen. Es wird nicht geloescht, sondern jede Zeile ueberschrieben.
 */
void ctrl_panel_draw(const CtrlPanel *p, const char *patch_name);

/**
 * ctrl_panel_poll_touch - Touch auswerten (einmal pro Frame, nach
 * hidScanInput()). Beruehrungen im OPT-Bereich werden ignoriert.
 * @return 1 wenn sich Wert oder Auswahl geaendert hat (neu zeichnen), sonst 0.
 */
int ctrl_panel_poll_touch(CtrlPanel *p);

/** Auswahl um delta Eintraege verschieben; Scroll folgt der Auswahl. */
void ctrl_panel_scroll(CtrlPanel *p, int delta);

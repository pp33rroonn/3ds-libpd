#pragma once
/* ctrl_panel.h - scrollbares Slider-Panel fuer den CTRL-Modus
 *
 * Zeigt die von pd_parse() gefundenen Empfaenger auf dem unteren
 * Bildschirm (Konsole 40x30 Zeichen, 8x8 px pro Zelle):
 *
 *   Zeile 1      : Kopfzeile "CTRL: <patch> [ausgewaehlt/anzahl]"
 *   Zeile 2      : leer
 *   Zeile 3+3*i  : Name + aktueller Wert  (i = 0..CTRL_PANEL_PAGE_SIZE-1)
 *   Zeile 4+3*i  : Balken  |XXXXX      |  (Rahmen + schwarz/Standard)
 *   Zeile 5+3*i  : leer (Abstand zwischen den Eintraegen)
 *   Zeile 30     : wird von main.c gezeichnet (Hinweiszeile)
 *
 * Balken (38 Zeichen, Spalte 2..39):
 *   Spalte 2        : linker Rahmen  '|'
 *   Spalten 3..38   : 36 Innenfelder, gefuellt = schwarzer Hintergrund
 *   Spalte 39       : rechter Rahmen '|'
 *
 * Auswahl-Markierung: Text der Namenszeile erscheint in Pink (Magenta),
 * kein farbiger Hintergrund mehr.
 *
 * Touch: Eintrag antippen waehlt aus. Bei Slidern setzt die x-Position
 * den Wert (Ziehen moeglich, der beim Aufsetzen getroffene Slider bleibt
 * bis zum Loslassen aktiv). Toggles schalten nur bei neuem Touch um.
 * Jede Aenderung geht per libpd_float() an PD.
 */

#include "../fs/pd_parser.h"

#define CTRL_PANEL_PAGE_SIZE 9

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
 * hidScanInput()). Beruehrungen unterhalb der Slider werden ignoriert.
 * @return 1 wenn sich Wert oder Auswahl geaendert hat (neu zeichnen), sonst 0.
 */
int ctrl_panel_poll_touch(CtrlPanel *p);

/** Auswahl um delta Eintraege verschieben; Scroll folgt der Auswahl. */
void ctrl_panel_scroll(CtrlPanel *p, int delta);

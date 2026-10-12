#pragma once
/* pd_parser.h - einfacher .pd-Datei-Parser fuer den CTRL-Modus
 *
 * Sucht im Patch nach Empfaengern, die sich sinnvoll per Slider/Toggle
 * steuern lassen:
 *
 *   #X obj x y r NAME;  /  #X obj x y receive NAME;   -> Slider 0..1
 *   #X obj x y vsl|hsl ... (Send-Name, Bereich min..max) -> Slider
 *   #X obj x y tgl ...     (Send-Name)                  -> Toggle 0/1
 *   #X obj x y nbx ...     (Send-Name, Bereich min..max) -> Slider
 *   #X text x y @ctrl NAME LO HI;                       -> Bereich setzen
 *
 * Mit einem Kommentar "@ctrl NAME LO HI" im Patch laesst sich der
 * Wertebereich eines Empfaengers festlegen (z.B. "@ctrl cutoff 200 5000").
 *
 * Nicht aufgenommen werden: Namen mit '$' (Dollar-Argumente sind zur
 * Ladezeit nicht aufloesbar), "empty", sowie die Hardware-Empfaenger der
 * App selbst (btn-*, pad-*, touch-*) und PD-interne Namen (pd-*).
 */

#define PD_CTRL_MAX 64

typedef enum { CTRL_FLOAT, CTRL_TOGGLE } PdCtrlType;

typedef struct {
    char       name[48];   /* PD-Empfaengername (Ziel fuer libpd_float) */
    PdCtrlType type;
    float      lo, hi;     /* Wertebereich */
    float      val;        /* aktueller Wert (Start: lo) */
} PdCtrl;

typedef struct {
    PdCtrl ctrls[PD_CTRL_MAX];
    int    count;
} PdCtrlList;

/**
 * pd_parse - .pd-Datei einlesen und steuerbare Empfaenger sammeln.
 * Die Liste wird vorher geleert.
 * @return Anzahl gefundener Eintraege, oder -1 wenn die Datei nicht
 *         geoeffnet werden konnte (Liste ist dann leer).
 */
int pd_parse(const char *path, PdCtrlList *list);

/**
 * pd_ctrl_find - Wertebereich eines Eintrags nachschlagen.
 * lo/hi bleiben unveraendert, wenn der Name nicht gefunden wird.
 */
void pd_ctrl_find(PdCtrlList *list, const char *name, float *lo, float *hi);

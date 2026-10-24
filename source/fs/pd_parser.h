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
 *   #X obj x y bng ...     (Send-Name)                  -> Bang-Taste
 *   #X obj x y hradio|vradio ... (Send-Name, Anzahl n)  -> Auswahl 0..n-1
 *   #X text x y @ctrl NAME LO HI;                       -> Bereich setzen
 *
 * Mit einem Kommentar "@ctrl NAME LO HI" im Patch laesst sich der
 * Wertebereich eines Empfaengers festlegen (z.B. "@ctrl cutoff 200 5000").
 *
 * Nicht aufgenommen werden: Namen mit '$' (Dollar-Argumente sind zur
 * Ladezeit nicht aufloesbar), "empty", sowie die Hardware-Empfaenger der
 * App selbst (btn-*, pad-*, touch-*, accel-*, gyro-*) und PD-interne Namen (pd-*).
 *
 * Labels der IEM-GUIs (vsl/hsl/nbx/tgl/bng/radio) werden uebernommen und
 * im Panel statt des Empfaengernamens angezeigt.
 */

#define PD_CTRL_MAX 64

#define PD_RADIO_MAX 128   /* max. Zellen eines hradio/vradio */

typedef enum { CTRL_FLOAT, CTRL_TOGGLE, CTRL_BANG, CTRL_RADIO } PdCtrlType;

typedef struct {
    char       name[48];   /* PD-Empfaengername (Ziel fuer libpd_float) */
    PdCtrlType type;
    float      lo, hi;     /* Wertebereich (Radio: 0..steps-1, Bang: 0..1) */
    float      val;        /* aktueller Wert (Start: lo) */
    char       label[24];  /* Anzeigename aus dem GUI-Label, "" = name */
    int        steps;      /* Radio: Anzahl Zellen (2..PD_RADIO_MAX), sonst 0 */
    int        orient;     /* GUI-Ausrichtung: PD_ORIENT_* (fuer Layout "Auto") */
} PdCtrl;

#define PD_ORIENT_NONE  0   /* [r], nbx, tgl, bng, @ctrl */
#define PD_ORIENT_HORIZ 1   /* hsl, hradio, hdl */
#define PD_ORIENT_VERT  2   /* vsl, vradio, vdl */

typedef struct {
    PdCtrl ctrls[PD_CTRL_MAX];
    int    count;
    int    has_touch_xy;   /* 1 if the patch receives touch-x / touch-y
                            * (drawn as a Touch-XY frame, not as faders) */
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

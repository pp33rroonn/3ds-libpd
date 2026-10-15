/* pd_parser.c - .pd-Datei-Parser fuer den CTRL-Modus (siehe pd_parser.h)
 *
 * Aufbau einer .pd-Datei: eine Anweisung pro Datensatz, abgeschlossen mit
 * ';'. PD bricht lange Datensaetze beim Speichern auf mehrere Zeilen um
 * (typisch bei vsl/hsl/nbx), deshalb werden Zeilen erst zu einem
 * vollstaendigen Datensatz zusammengesetzt und dann in Tokens zerlegt.
 *
 * Token-Positionen (0 = "#X"):
 *   #X obj X Y r NAME
 *   #X obj X Y vsl W H MIN MAX LOG INIT SEND RECV ...
 *   #X obj X Y tgl SIZE INIT SEND RECV ...
 *   #X obj X Y nbx DIGITS H MIN MAX LOG INIT SEND RECV ...
 *   #X text X Y @ctrl NAME LO HI
 */

#include "pd_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PD_LINE_MAX    512    /* max. Laenge einer Dateizeile */
#define PD_RECORD_MAX  2048   /* max. Laenge eines Datensatzes */
#define PD_TOKENS_MAX  32     /* weitere Tokens werden ignoriert */

/* Unbrauchbar grosse Bereiche (nbx-Standard ist -1e37..1e37) ersetzen */
#define PD_RANGE_LIMIT 1.0e6f
#define PD_FALLBACK_LO 0.0f
#define PD_FALLBACK_HI 127.0f

/* ---- Hilfsfunktionen --------------------------------------------------- */

/* Abschliessendes ";" bzw. "\;" (und Zeilenende-Reste) entfernen. */
static void strip_token(char *t)
{
    size_t n = strlen(t);
    while (n > 0 && (t[n - 1] == ';' || t[n - 1] == '\\' ||
                     t[n - 1] == '\r' || t[n - 1] == '\n')) {
        t[--n] = '\0';
    }
}

/* Hardware-Empfaenger der App, PD-interne und nicht aufloesbare Namen
 * gehoeren nicht ins Panel. */
static int name_is_usable(const char *name)
{
    if (name[0] == '\0') return 0;
    if (strcmp(name, "empty") == 0) return 0;
    if (strchr(name, '$') != NULL) return 0;
    if (strncmp(name, "btn-", 4) == 0) return 0;
    if (strncmp(name, "pad-", 4) == 0) return 0;
    if (strncmp(name, "touch-", 6) == 0) return 0;
    if (strncmp(name, "pd-", 3) == 0) return 0;
    if (strncmp(name, "accel-", 6) == 0) return 0;
    if (strncmp(name, "gyro-", 5) == 0) return 0;
    if (strlen(name) >= sizeof(((PdCtrl *)0)->name)) return 0;
    return 1;
}

static PdCtrl *find_ctrl(PdCtrlList *list, const char *name)
{
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->ctrls[i].name, name) == 0) return &list->ctrls[i];
    }
    return NULL;
}

/* Bereich plausibel machen: riesige Bereiche ersetzen, lo == hi vermeiden */
static void sanitize_range(float *lo, float *hi)
{
    if (*lo > PD_RANGE_LIMIT || *lo < -PD_RANGE_LIMIT ||
        *hi > PD_RANGE_LIMIT || *hi < -PD_RANGE_LIMIT) {
        *lo = PD_FALLBACK_LO;
        *hi = PD_FALLBACK_HI;
    }
    if (*lo == *hi) *hi = *lo + 1.0f;
}

/* Neuen Eintrag anlegen; Duplikate, ungueltige Namen und volle Liste
 * werden still uebersprungen. */
static void add_ctrl(PdCtrlList *list, const char *name, PdCtrlType type,
                     float lo, float hi)
{
    if (!name_is_usable(name)) return;
    if (list->count >= PD_CTRL_MAX) return;
    if (find_ctrl(list, name) != NULL) return;

    sanitize_range(&lo, &hi);

    PdCtrl *c = &list->ctrls[list->count++];
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->type = type;
    c->lo   = lo;
    c->hi   = hi;
    c->val  = lo;
}

/* "@ctrl NAME LO HI": vorhandenen Eintrag aktualisieren oder neu anlegen */
static void apply_ctrl_comment(PdCtrlList *list, const char *name,
                               float lo, float hi)
{
    if (!name_is_usable(name)) return;

    PdCtrl *c = find_ctrl(list, name);
    if (c == NULL) {
        add_ctrl(list, name, CTRL_FLOAT, lo, hi);
        return;
    }
    sanitize_range(&lo, &hi);
    c->lo  = lo;
    c->hi  = hi;
    c->val = lo;
}

/* GUI-Objekte: bevorzugt den Send-Namen, sonst den Receive-Namen */
static const char *gui_name(char **tok, int n, int send_idx)
{
    if (n > send_idx && name_is_usable(tok[send_idx])) return tok[send_idx];
    if (n > send_idx + 1 && name_is_usable(tok[send_idx + 1])) return tok[send_idx + 1];
    return "";
}

/* Einen vollstaendigen Datensatz auswerten (wird dabei zerlegt). */
static void parse_record(char *rec, PdCtrlList *list)
{
    char *tok[PD_TOKENS_MAX];
    int   n = 0;

    for (char *t = strtok(rec, " \t\r\n"); t != NULL && n < PD_TOKENS_MAX;
         t = strtok(NULL, " \t\r\n")) {
        strip_token(t);
        tok[n++] = t;
    }

    if (n < 5 || strcmp(tok[0], "#X") != 0) return;

    if (strcmp(tok[1], "text") == 0) {
        if (strcmp(tok[4], "@ctrl") == 0 && n >= 8) {
            apply_ctrl_comment(list, tok[5], strtof(tok[6], NULL),
                               strtof(tok[7], NULL));
        }
        return;
    }

    if (strcmp(tok[1], "obj") != 0) return;
    const char *cls = tok[4];

    if (strcmp(cls, "r") == 0 || strcmp(cls, "receive") == 0) {
        if (n >= 6) add_ctrl(list, tok[5], CTRL_FLOAT, 0.0f, 1.0f);
    } else if (strcmp(cls, "vsl") == 0 || strcmp(cls, "hsl") == 0) {
        if (n >= 13) {
            add_ctrl(list, gui_name(tok, n, 11), CTRL_FLOAT,
                     strtof(tok[7], NULL), strtof(tok[8], NULL));
        }
    } else if (strcmp(cls, "tgl") == 0) {
        if (n >= 9) add_ctrl(list, gui_name(tok, n, 7), CTRL_TOGGLE, 0.0f, 1.0f);
    } else if (strcmp(cls, "nbx") == 0) {
        if (n >= 13) {
            add_ctrl(list, gui_name(tok, n, 11), CTRL_FLOAT,
                     strtof(tok[7], NULL), strtof(tok[8], NULL));
        }
    }
}

/* Endet die (bereits rechts getrimmte) Zeile mit einem unescapten ';'? */
static int ends_record(const char *s, size_t n)
{
    return n > 0 && s[n - 1] == ';' && (n < 2 || s[n - 2] != '\\');
}

/* ---- Oeffentliche API -------------------------------------------------- */

int pd_parse(const char *path, PdCtrlList *list)
{
    if (list == NULL) return -1;
    list->count = 0;
    if (path == NULL) return -1;

    FILE *f = fopen(path, "r");
    if (f == NULL) return -1;

    char   line[PD_LINE_MAX];
    char   rec[PD_RECORD_MAX];
    size_t rec_len = 0;

    rec[0] = '\0';
    while (fgets(line, sizeof(line), f) != NULL) {
        /* Zeilenende und Leerraum rechts entfernen */
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' '  || line[n - 1] == '\t')) {
            line[--n] = '\0';
        }

        /* An den Datensatz anhaengen (mit Leerzeichen als Trenner);
         * zu lange Datensaetze werden abgeschnitten. */
        if (rec_len + n + 2 < sizeof(rec)) {
            if (rec_len > 0) rec[rec_len++] = ' ';
            memcpy(rec + rec_len, line, n);
            rec_len += n;
            rec[rec_len] = '\0';
        }

        if (ends_record(line, n)) {
            parse_record(rec, list);
            rec_len = 0;
            rec[0]  = '\0';
        }
    }
    if (rec_len > 0) parse_record(rec, list);   /* letzter Datensatz ohne ';' */

    fclose(f);
    return list->count;
}

void pd_ctrl_find(PdCtrlList *list, const char *name, float *lo, float *hi)
{
    if (list == NULL || name == NULL) return;
    PdCtrl *c = find_ctrl(list, name);
    if (c == NULL) return;
    if (lo) *lo = c->lo;
    if (hi) *hi = c->hi;
}

/* soundfiler_shim.c - Dateisystem-Shim fuer libpd auf 3DS
 *
 * Devkit-libc leitet fopen("sdmc:/...") und fopen("romfs:/...")
 * automatisch auf ctrulib FSUSER um - kein manueller FSUSER-Code noetig.
 *
 * Dieser Shim:
 *   1. Haelt eine Liste von Suchpfaden
 *   2. Loest relative PD-Pfade -> absolute 3DS-Pfade auf
 *   3. Stellt soundfiler_shim_open() bereit (fuer libpd-Hook in pd_bridge)
 *   4. Erstellt fehlende Verzeichnisse automatisch (fsrv)
 */

#include "soundfiler_shim.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>   /* mkdir via ctrulib-Wrapper */

/* ---- Interne Zustandsvariablen ----------------------------------------- */

static char s_search_paths[SHIM_MAX_SEARCH_PATHS][SHIM_MAX_PATH_LEN];
static int  s_search_path_count = 0;

/* Standard-Schreibpfad fuer [writesf~] und [soundfiler]-Schreiboperationen */
#define DEFAULT_WRITE_PATH  "sdmc:/3ds/libpd_3ds/recordings/"

/* ---- Hilfsfunktion: Verzeichnis anlegen falls nicht vorhanden ----------- */
static void ensure_dir(const char *path)
{
    /* ctrulib-libc bietet mkdir() via devoptab */
    mkdir(path, 0777);   /* Fehler ignorieren (existiert moeglicherweise schon) */
}

/* ---- Oeffentliche API -------------------------------------------------- */

void soundfiler_shim_init(void)
{
    s_search_path_count = 0;

    /* Standard-Suchpfade in Prioritaetsreihenfolge registrieren.
     * Zuerst SD-Karte (grosse Bibliotheken), dann romfs (eingebettet).
     * soundfiler_shim_add_search_path() fuegt LIFO ein,
     * deshalb hier in umgekehrter Prioritaet eintragen: */

    /* Niedrigste Prioritaet: romfs (eingebettet, immer vorhanden) */
    soundfiler_shim_add_search_path("romfs:/patches/");
    soundfiler_shim_add_search_path("romfs:/samples/");

    /* Mittlere Prioritaet: SD-Karte Standard-Pfade */
    soundfiler_shim_add_search_path("sdmc:/3ds/libpd_3ds/patches/");
    soundfiler_shim_add_search_path("sdmc:/3ds/libpd_3ds/samples/");

    /* Hoechste Prioritaet: SD-Karte Wurzel-Pfad (fuer absolute Patch-Pfade) */
    soundfiler_shim_add_search_path("sdmc:/3ds/libpd_3ds/");

    /* Schreib-Verzeichnis auf SD-Karte anlegen */
    ensure_dir("sdmc:/3ds/");
    ensure_dir("sdmc:/3ds/libpd_3ds/");
    ensure_dir("sdmc:/3ds/libpd_3ds/patches/");
    ensure_dir("sdmc:/3ds/libpd_3ds/samples/");
    ensure_dir("sdmc:/3ds/libpd_3ds/recordings/");
}

bool soundfiler_shim_add_search_path(const char *path)
{
    if (!path || s_search_path_count >= SHIM_MAX_SEARCH_PATHS) return false;

    /* LIFO: neuer Pfad hat hoechste Prioritaet -> ans Ende schreiben,
     * resolve() durchlaeuft rueckwaerts */
    strncpy(s_search_paths[s_search_path_count], path, SHIM_MAX_PATH_LEN - 1);
    s_search_paths[s_search_path_count][SHIM_MAX_PATH_LEN - 1] = '\0';
    s_search_path_count++;
    return true;
}

bool soundfiler_shim_resolve(const char *pd_path, char *out_path)
{
    if (!pd_path || !out_path) return false;

    /* Wenn Pfad bereits einen bekannten Praefix hat, direkt pruefen */
    if (strncmp(pd_path, "sdmc:/", 6) == 0 ||
        strncmp(pd_path, "romfs:/", 7) == 0) {
        FILE *f = fopen(pd_path, "rb");
        if (f) {
            fclose(f);
            strncpy(out_path, pd_path, SHIM_MAX_PATH_LEN - 1);
            out_path[SHIM_MAX_PATH_LEN - 1] = '\0';
            return true;
        }
        return false;   /* Absoluter Pfad, aber Datei existiert nicht */
    }

    /* Relativer Pfad: alle Suchpfade rueckwaerts durchsuchen (LIFO) */
    for (int i = s_search_path_count - 1; i >= 0; i--) {
        /* Kandidaten-Pfad zusammenbauen */
        char candidate[SHIM_MAX_PATH_LEN];
        snprintf(candidate, sizeof(candidate), "%s%s",
                 s_search_paths[i], pd_path);

        FILE *f = fopen(candidate, "rb");
        if (f) {
            fclose(f);
            strncpy(out_path, candidate, SHIM_MAX_PATH_LEN - 1);
            out_path[SHIM_MAX_PATH_LEN - 1] = '\0';
            return true;
        }
    }

    /* Nicht gefunden: out_path unveraendert lassen */
    fprintf(stderr, "[soundfiler_shim] Nicht gefunden: %s\n", pd_path);
    return false;
}

FILE *soundfiler_shim_open(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;

    /* Schreibzugriff: direkt auf SD-Karte, kein Suchpfad */
    if (mode[0] == 'w' || mode[0] == 'a') {
        /* Wenn kein absoluter Pfad: Schreib-Verzeichnis voranstellen */
        if (strncmp(path, "sdmc:/", 6) != 0 &&
            strncmp(path, "romfs:/", 7) != 0) {
            char abs_path[SHIM_MAX_PATH_LEN];
            snprintf(abs_path, sizeof(abs_path), "%s%s",
                     DEFAULT_WRITE_PATH, path);
            return fopen(abs_path, mode);
        }
        return fopen(path, mode);
    }

    /* Lesezugriff: Suchpfade durchsuchen */
    char resolved[SHIM_MAX_PATH_LEN];
    if (soundfiler_shim_resolve(path, resolved)) {
        return fopen(resolved, mode);
    }

    /* Fallback: Pfad unveraendert uebergeben (fopen gibt NULL zurueck) */
    return fopen(path, mode);
}

void soundfiler_shim_get_write_path(char *out_path)
{
    if (!out_path) return;
    strncpy(out_path, DEFAULT_WRITE_PATH, SHIM_MAX_PATH_LEN - 1);
    out_path[SHIM_MAX_PATH_LEN - 1] = '\0';
}

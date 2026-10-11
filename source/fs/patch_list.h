#pragma once
/* patch_list.h – Patch-Verzeichnis scannen und durchblaettern
 *
 * Scannt zuerst sdmc:/3ds/libpd_3ds/patches/ (User-Patches haben Prioritaet),
 * dann romfs:/patches (eingebauter Fallback).
 * Unterstuetzt bis zu PATCH_LIST_MAX Eintraege.
 *
 * Verwendung:
 *   PatchList pl;
 *   patch_list_scan(&pl);                       // einmalig beim Start
 *   const PatchEntry *e = patch_list_current(&pl); // aktueller Patch
 *   patch_list_next(&pl);                       // START-Taste -> naechster
 */

#include <stdbool.h>

#define PATCH_PATH_MAX  256
#define PATCH_NAME_MAX  128
#define PATCH_LIST_MAX   32

typedef struct {
    char dir[PATCH_PATH_MAX];      /* vollstaendiger Verzeichnispfad   */
    char file[PATCH_NAME_MAX];     /* Dateiname inkl. .pd              */
    char display[PATCH_NAME_MAX];  /* Kurzname ohne .pd (fuer Anzeige) */
    bool from_sd;                  /* true wenn von sdmc:/, false romfs */
} PatchEntry;

typedef struct {
    PatchEntry entries[PATCH_LIST_MAX];
    int        count;    /* Anzahl gefundener Patches */
    int        current;  /* Index des aktiven Patches */
} PatchList;

/**
 * patch_list_scan – SD und romfs nach *.pd scannen.
 * Legt sdmc:/3ds/libpd_3ds/patches/ an falls nicht vorhanden.
 * @param pl  Zeiger auf PatchList; wird vollstaendig initialisiert.
 */
void patch_list_scan(PatchList *pl);

/** Aktuellen Eintrag zurueckgeben (NULL wenn Liste leer). */
const PatchEntry *patch_list_current(const PatchList *pl);

/** Zum naechsten Patch weiterschalten (zyklisch); gibt neuen Eintrag zurueck. */
const PatchEntry *patch_list_next(PatchList *pl);

/** Zum vorherigen Patch zurueckschalten (zyklisch). */
const PatchEntry *patch_list_prev(PatchList *pl);

/* patch_list.c – Patch-Verzeichnis scannen und durchblaettern */

#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdio.h>
#include <stdlib.h>

#include "patch_list.h"

#define SD_BASE_DIR   "sdmc:/3ds/libpd_3ds"
#define SD_PATCH_DIR  "sdmc:/3ds/libpd_3ds/patches"
#define ROMFS_DIR     "romfs:/patches"

/* true wenn name auf ".pd" endet (und mehr als nur ".pd" ist) */
static bool has_pd_ext(const char *name)
{
    size_t len = strlen(name);
    return len > 3 && strcmp(name + len - 3, ".pd") == 0;
}

static int cmp_entry(const void *a, const void *b)
{
    return strcasecmp(((const PatchEntry *)a)->file, ((const PatchEntry *)b)->file);
}

static void add_from_dir(PatchList *pl, const char *dir, bool from_sd)
{
    DIR *d = opendir(dir);
    if (!d) return;
    int first = pl->count;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && pl->count < PATCH_LIST_MAX) {
        const char *name = ent->d_name;
        /* versteckte Dateien ueberspringen: ".", ".." und die "._name.pd"-
         * Begleitdateien, die macOS beim Kopieren auf FAT-SD-Karten anlegt */
        if (name[0] == '.') continue;
        if (!has_pd_ext(name)) continue;

        /* Zu lange Namen ueberspringen (abgeschnitten waeren sie nicht ladbar) */
        size_t len = strlen(name);
        if (len >= PATCH_NAME_MAX) continue;

        PatchEntry *e = &pl->entries[pl->count++];
        snprintf(e->dir, sizeof(e->dir), "%s", dir);
        memcpy(e->file, name, len + 1);

        size_t n = len - 3;                       /* ohne ".pd" */
        memcpy(e->display, name, n);
        e->display[n] = '\0';

        e->from_sd = from_sd;
    }
    closedir(d);

    /* readdir liefert auf FAT die Kopier-Reihenfolge -> alphabetisch sortieren
     * (01_..., 02_... erscheinen so in der richtigen Reihenfolge) */
    qsort(&pl->entries[first], (size_t)(pl->count - first), sizeof(PatchEntry), cmp_entry);
}

void patch_list_scan(PatchList *pl)
{
    if (!pl) return;
    pl->count   = 0;
    pl->current = 0;

    /* Verzeichnisse anlegen; Fehler (z.B. existiert bereits) ignorieren */
    mkdir(SD_BASE_DIR, 0777);
    mkdir(SD_PATCH_DIR, 0777);

    add_from_dir(pl, SD_PATCH_DIR, true);   /* User-Patches zuerst */
    add_from_dir(pl, ROMFS_DIR, false);     /* eingebauter Fallback */
}

const PatchEntry *patch_list_current(const PatchList *pl)
{
    if (!pl || pl->count <= 0) return NULL;
    return &pl->entries[pl->current % pl->count];
}

const PatchEntry *patch_list_next(PatchList *pl)
{
    if (!pl || pl->count <= 0) return NULL;
    pl->current = (pl->current + 1) % pl->count;
    return &pl->entries[pl->current];
}

const PatchEntry *patch_list_prev(PatchList *pl)
{
    if (!pl || pl->count <= 0) return NULL;
    pl->current = (pl->current - 1 + pl->count) % pl->count;
    return &pl->entries[pl->current];
}

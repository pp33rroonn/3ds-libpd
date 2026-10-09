#pragma once
/* soundfiler_shim.h - Dateisystem-Shim fuer libpd auf 3DS
 *
 * Hintergrund
 * -----------
 * libpd nutzt intern Standard-C fopen()/fread()/fclose() fuer:
 *   [soundfiler]  - WAV/AIFF lesen und schreiben
 *   [readsf~]     - WAV streaming (lesen)
 *   [writesf~]    - WAV streaming (schreiben)
 *
 * Auf 3DS gibt es KEIN POSIX-Dateisystem. Devkit-libc bietet zwar
 * eine fopen()-Implementierung, aber sie leitet auf ctrulib FSUSER-
 * Calls um - allerdings nur fuer bestimmte Pfad-Praefixe:
 *
 *   sdmc:/  -> SD-Karte  (ctrulib FSUSER, automatisch)
 *   romfs:/ -> romfs     (ctrulib romfsMount, automatisch nach romfsInit())
 *
 * Das bedeutet: fopen("sdmc:/3ds/libpd_3ds/samples/kick.wav", "rb")
 * funktioniert direkt - KEIN manueller FSUSER-Aufruf noetig!
 *
 * Was dieser Shim tut
 * -------------------
 * 1. PD-Pfade normalisieren: relative Pfade -> absolute sdmc:/-Pfade
 *    (PD-Patches verwenden oft relative Pfade wie "samples/kick.wav")
 * 2. Suchpfad-Liste: mehrere Pfade werden der Reihe nach probiert
 * 3. Schreib-Pfad: [writesf~] schreibt auf SD-Karte
 * 4. Hook-Funktion fuer libpd_set_printhook (Pfad-Fehler loggen)
 *
 * Suchpfad-Reihenfolge (konfigurierbar via soundfiler_shim_set_search_paths):
 *   1. sdmc:/3ds/libpd_3ds/samples/
 *   2. sdmc:/3ds/libpd_3ds/patches/
 *   3. romfs:/samples/
 *   4. romfs:/patches/
 *
 * Verwendung in pd_bridge.c:
 *   soundfiler_shim_init();
 *   // Optional: eigene Pfade:
 *   soundfiler_shim_add_search_path("sdmc:/music/samples/");
 */

#include <stdio.h>
#include <stdbool.h>

/* Maximale Anzahl konfigurierbarer Suchpfade */
#define SHIM_MAX_SEARCH_PATHS  8

/* Maximale absolute Pfadlaenge (3DS: 512 Zeichen) */
#define SHIM_MAX_PATH_LEN     512

/**
 * soundfiler_shim_init - Shim initialisieren mit Standard-Suchpfaden.
 *
 * Muss nach romfsInit() und vor pd_bridge_init() aufgerufen werden.
 * Registriert intern den fopen-kompatiblen Oeffner fuer libpd.
 */
void soundfiler_shim_init(void);

/**
 * soundfiler_shim_add_search_path - Eigenen Suchpfad hinzufuegen.
 *
 * Pfad MUSS mit '/' enden, z.B. "sdmc:/meine_samples/"
 * Wird VOR den Standard-Pfaden durchsucht (LIFO-Reihenfolge).
 *
 * @return true wenn erfolgreich hinzugefuegt, false wenn Liste voll.
 */
bool soundfiler_shim_add_search_path(const char *path);

/**
 * soundfiler_shim_resolve - Relativen PD-Pfad zu absolutem 3DS-Pfad aufloesen.
 *
 * Durchsucht alle registrierten Pfade. Wenn die Datei gefunden wird,
 * wird der absolute Pfad in @out_path geschrieben.
 *
 * @param pd_path   Relativer oder absoluter Pfad aus dem PD-Patch
 * @param out_path  Ausgabe-Puffer (mindestens SHIM_MAX_PATH_LEN Zeichen)
 * @return true wenn Datei gefunden.
 */
bool soundfiler_shim_resolve(const char *pd_path, char *out_path);

/**
 * soundfiler_shim_open - fopen()-kompatibler Oeffner mit Pfad-Aufloesung.
 *
 * Wird intern von libpd als Datei-Oeffner registriert.
 * Kann auch direkt verwendet werden.
 *
 * @param path  Pfad (relativ oder mit sdmc:/romfs:/ Praefix)
 * @param mode  fopen-Modus ("rb", "wb", "r", etc.)
 * @return FILE* oder NULL bei Fehler.
 */
FILE *soundfiler_shim_open(const char *path, const char *mode);

/**
 * soundfiler_shim_get_write_path - Standard-Schreibpfad liefern.
 *
 * [writesf~] und [soundfiler]-Schreib-Operationen landen hier.
 * Standard: "sdmc:/3ds/libpd_3ds/recordings/"
 *
 * @param out_path  Puffer fuer den absoluten Verzeichnis-Pfad.
 */
void soundfiler_shim_get_write_path(char *out_path);

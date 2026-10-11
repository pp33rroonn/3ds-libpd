/* main.c - libpd 3DS Homebrew Einstiegspunkt
 *
 * Stereo-Kanal-Belegung
 * ---------------------
 *   Sync AN : Links = PD-Mono-Mix, Rechts = Sync-Puls
 *   Sync AUS: Links/Rechts = PD-Stereo
 *
 * Modi (app_state.h)
 * ------------------
 *   MODE_PATCH   : Tasten, Circle-Pad, Touch -> nur PD
 *   MODE_OPTIONS : BPM/Sync/ppqn-Steuerung (siehe button_input.h)
 *   Wechsel      : L+R gleichzeitig oder Touch auf OPT (unten rechts)
 *   START        : naechster Patch (in beiden Modi), HOME beendet
 *
 * PD-Patch kann BPM aendern via: [send pd-bpm 130]
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "audio/audio.h"
#include "sync/sync_clock.h"
#include "input/touch_input.h"
#include "input/button_input.h"
#include "pd_bridge/pd_bridge.h"
#include "fs/patch_list.h"
#include "app_state.h"

/* Muss mit sync_clock.h / Makefile uebereinstimmen */
#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

#define DEFAULT_BPM   120.0f
#define SD_PATCH_DIR  "sdmc:/3ds/libpd_3ds/patches"
#define ROMFS_PATCH_DIR "romfs:/patches"
#define PATCH_FILE    "main.pd"

/* Anzeige-Update nur alle N Frames (60fps -> alle 30 Frames = 2 Hz) */
#define DISPLAY_UPDATE_FRAMES 30

/* Globaler App-Modus (Deklaration in app_state.h) */
AppMode g_app_mode = MODE_PATCH;

/* Konsolen: oben Status, unten Touch-Flaeche + OPT-Button */
static PrintConsole s_top_console;
static PrintConsole s_bottom_console;

/* PD-Status fuer die Anzeige (wird von update_display staendig gezeigt) */
static bool s_pd_ok = false;
static PatchList s_patch_list;
static char      s_patch_display[64];  /* Anzeigename aktueller Patch */

/* Diagnose: PD-Ausgangspegel (Spitzenwert seit letzter Anzeige, 0..32767) */
static int       s_peak_l = 0, s_peak_r = 0;
static unsigned  s_log_drawn = 0;      /* zuletzt gezeichneter Log-Stand */

/* Log-Bereich unten: Zeile 3 = Ueberschrift, ab Zeile 4 die Meldungen */
#define LOG_ROW0 4

/* Eine Zeile auf dem oberen Bildschirm (50 Spalten) ausgeben und mit
 * Leerzeichen auffuellen, damit Reste alter Texte ueberschrieben werden.
 * 49 Zeichen + '\n' vermeidet automatischen Zeilenumbruch. */
static void top_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void top_line(const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%-49.49s\n", buf);
}

/* ---- Hilfsfunktion: Top-Screen-Status-Anzeige -------------------------- */
static void update_display(float bpm, bool sync_on, uint32_t ppqn, int tap_count)
{
    (void)tap_count;
    consoleSelect(&s_top_console);
    printf("\x1b[1;1H");   /* Cursor auf Zeile 1, Spalte 1 */

    if (g_app_mode == MODE_PATCH) {
        top_line("libpd 3DS                              [PATCH]");
        if (s_pd_ok) {
            top_line("Patch [%d/%d]: %s", s_patch_list.current + 1,
                     s_patch_list.count, s_patch_display);
        } else {
            top_line("Patch: FEHLT %s", s_patch_display);
        }
        top_line("BPM: %6.1f   SYNC: %s", bpm, sync_on ? "AN" : "AUS (Stereo)");
        top_line("PD-Pegel  L:%5d  R:%5d %s", s_peak_l, s_peak_r,
                 (s_peak_l | s_peak_r) ? "" : "(PD stumm)");
        top_line("------------------------------------------------");
        top_line("Tasten / Circle-Pad / Touch -> PD");
        top_line("START: naechster Patch");
        top_line("L+R oder OPT-Feld: Optionen");
        /* immer ausgeben (ggf. leer), damit keine Reste stehen bleiben */
        top_line("%s", s_pd_ok ? "" : "SD: sdmc:/3ds/libpd_3ds/patches/");
    } else {
        const char *dev;
        switch (ppqn) {
            case  1: dev = "Korg MS";     break;
            case  2: dev = "Volca/PO";    break;
            case 24: dev = "MIDI/DINSync"; break;
            default: dev = "?";           break;
        }
        top_line("libpd 3DS                            [OPTIONEN]");
        top_line("BPM: %6.1f  SYNC: %s  ppqn: %lu (%s)", bpm,
                 sync_on ? "AN " : "AUS", (unsigned long)ppqn, dev);
        top_line("------------------------------------------------");
        top_line("D-Pad L/R: BPM -1/+1");
        top_line("D-Pad U/D: BPM +10/-10");
        top_line("Circle-Pad: BPM stufenlos");
        top_line("A: Tap-Tempo    X: ppqn wechseln");
        top_line("SELECT: Sync ein/aus (aus = Stereo)");
        top_line("START: naechster Patch");
        top_line("L+R oder OPT-Feld: zurueck zum Patch");
    }
}

/* ---- Hilfsfunktion: PD-Log auf dem unteren Bildschirm (nur Patch-Modus) */
static void draw_log(void)
{
    if (g_app_mode != MODE_PATCH) return;
    consoleSelect(&s_bottom_console);
    printf("\x1b[%d;1H%-39.39s", LOG_ROW0 - 1, "PD-Meldungen:");
    for (int i = 0; i < PD_LOG_LINES; i++) {
        printf("\x1b[%d;1H%-39.39s", LOG_ROW0 + i, pd_bridge_log_line(i));
    }
    s_log_drawn = pd_bridge_log_serial();
    consoleSelect(&s_top_console);
}

/* ---- Hilfsfunktion: unterer Bildschirm mit OPT-Button ------------------ */
static void draw_bottom(void)
{
    consoleSelect(&s_bottom_console);
    consoleClear();

    printf("\x1b[1;1H");
    if (g_app_mode == MODE_PATCH) {
        printf("Touch-Flaeche -> PD (touch-x/y)\n");
    } else {
        printf("Optionen aktiv - Touch geht nicht an PD\n");
    }

    /* Button: Spalten 34..39, Zeilen 27..29 (grau = Patch, gelb = Optionen).
     * Letzte Zeile/Spalte bewusst frei, damit die Konsole nicht scrollt. */
    const char *col = (g_app_mode == MODE_OPTIONS) ? "\x1b[30;43m"   /* schwarz auf gelb */
                                                   : "\x1b[30;47m";  /* schwarz auf grau */
    printf("\x1b[27;34H%s      \x1b[0m", col);
    printf("\x1b[28;34H%s OPT  \x1b[0m", col);
    printf("\x1b[29;34H%s      \x1b[0m", col);

    consoleSelect(&s_top_console);
    draw_log();   /* zeichnet nur im Patch-Modus */
}

static bool touch_in_opt_button(const touchPosition *tp)
{
    return tp->px >= OPT_BTN_X0 && tp->px <= OPT_BTN_X1 &&
           tp->py >= OPT_BTN_Y0 && tp->py <= OPT_BTN_Y1;
}

/* ---- Einstiegspunkt ----------------------------------------------------- */
int main(void)
{
    /* --- 3DS-Services ---------------------------------------------------- */
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, &s_bottom_console);
    consoleInit(GFX_TOP, &s_top_console);   /* zuletzt init = aktiv */
    hidInit();
    romfsInit();

    consoleClear();
    printf("Initialisierung...\n");

    /* --- Sync-Clock ------------------------------------------------------ */
    SyncClock *clk = sync_clock_create(SAMPLERATE, DEFAULT_BPM);
    if (!clk) {
        printf("FEHLER: sync_clock_create\n");
        goto cleanup_gfx;
    }

    /* --- libpd / PD-Bridge ----------------------------------------------- */
    /* Patches scannen (SD zuerst, dann romfs) */
    patch_list_scan(&s_patch_list);

    /* Ersten Patch laden */
    const PatchEntry *first = patch_list_current(&s_patch_list);
    if (first) {
        snprintf(s_patch_display, sizeof(s_patch_display), "%s", first->display);
        if (!pd_bridge_init(first->dir, first->file, clk)) {
            printf("FEHLER: PD-Patch nicht geladen\n");
        } else {
            s_pd_ok = true;
        }
    } else {
        /* Absoluter Fallback: romfs */
        snprintf(s_patch_display, sizeof(s_patch_display), "main (romfs)");
        if (!pd_bridge_init(ROMFS_PATCH_DIR, PATCH_FILE, clk)) {
            printf("FEHLER: kein Patch gefunden\n");
        } else {
            s_pd_ok = true;
        }
    }

    /* --- Button-Eingabe -------------------------------------------------- */
    ButtonInputCtx *btn = button_input_create(clk);
    if (!btn) {
        printf("FEHLER: button_input_create\n");
        goto cleanup_pd;
    }

    /* --- NDSP Audio ------------------------------------------------------ */
    AudioCtx *audio = audio_init(clk);
    if (!audio) {
        printf("FEHLER: audio_init\n");
        goto cleanup_btn;
    }

    /* Startanzeige */
    consoleClear();
    update_display(DEFAULT_BPM, true, sync_clock_get_ppqn(clk), 0);
    draw_bottom();

    /* --- Main-Loop ------------------------------------------------------- */
    int  frame_count   = 0;
    bool touch_on_opt  = false;   /* aktueller Touch begann auf OPT-Button */

    while (aptMainLoop()) {

        /* hidScanInput() EINMAL pro Frame - touch_input_poll() liest nur */
        hidScanInput();

        AppMode mode_before = g_app_mode;

        /* START: naechster Patch laden (in beiden Modi) */
        if (hidKeysDown() & KEY_START) {
            const PatchEntry *e = patch_list_next(&s_patch_list);
            if (e) {
                snprintf(s_patch_display, sizeof(s_patch_display),
                         "%s", e->display);
                s_pd_ok = pd_bridge_reload(e->dir, e->file);
            }
        }

        /* Tasten (inkl. L+R-Moduswechsel) */
        button_input_poll(btn);

        /* OPT-Button: neuer Touch im Button-Bereich -> Modus wechseln.
         * Der ganze Touch (bis zum Loslassen) geht dann nicht an PD. */
        if (hidKeysDown() & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            if (touch_in_opt_button(&tp)) {
                touch_on_opt = true;
                button_input_toggle_mode(btn);
            }
        }
        if (!(hidKeysHeld() & KEY_TOUCH)) touch_on_opt = false;

        /* Touch -> PD nur im Patch-Modus */
        touch_input_poll(g_app_mode == MODE_PATCH && !touch_on_opt);

        /* Moduswechsel: beide Bildschirme sofort neu zeichnen */
        if (g_app_mode != mode_before) {
            consoleSelect(&s_top_console);
            consoleClear();
            draw_bottom();
            frame_count = DISPLAY_UPDATE_FRAMES;   /* Top sofort aktualisieren */
        }

        /* Sync-Mute-Status an Audio-Modul weitergeben */
        audio_set_sync_enabled(audio, button_input_sync_enabled(btn));

        /* Audio befuellen (PD + Sync; gibt sofort zurueck wenn nicht noetig) */
        audio_fill_buffer(audio, NULL);

        /* Neue PD-Meldungen -> Log unten neu zeichnen (nur Patch-Modus) */
        if (g_app_mode == MODE_PATCH && pd_bridge_log_serial() != s_log_drawn) {
            draw_log();
        }

        /* Anzeige-Update (seltener um CPU zu schonen) */
        if (++frame_count >= DISPLAY_UPDATE_FRAMES) {
            frame_count = 0;
            audio_get_pd_peak(audio, &s_peak_l, &s_peak_r);
            update_display(button_input_get_bpm(btn),
                           button_input_sync_enabled(btn),
                           button_input_get_ppqn(btn),
                           0 /* tap_count */);
        }

        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    /* --- Aufraumen ------------------------------------------------------- */
    audio_deinit(audio);

cleanup_btn:
    button_input_destroy(btn);

cleanup_pd:
    pd_bridge_deinit();
    sync_clock_destroy(clk);

cleanup_gfx:
    romfsExit();
    hidExit();
    gfxExit();

    return 0;
}

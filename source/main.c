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
 *   MODE_CTRL    : Slider-Panel fuer die Receiver des Patches (ctrl_panel.h)
 *   MODE_OPTIONS : BPM/Sync/ppqn-Steuerung (siehe button_input.h)
 *   Wechsel      : L+R = OPTIONS ein/aus (mit kurzer Ein-/Ausblendung),
 *                  R allein = PATCH <-> CTRL
 *   START        : naechster Patch (in allen Modi), HOME beendet
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
#include "fs/pd_parser.h"
#include "input/ctrl_panel.h"
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

/* Konsolen: oben Status, unten Touch-Flaeche / PD-Log / CTRL-Panel */
static PrintConsole s_top_console;
static PrintConsole s_bottom_console;

/* PD-Status fuer die Anzeige (wird von update_display staendig gezeigt) */
static bool s_pd_ok = false;
static PatchList s_patch_list;
static AudioCtx *s_audio = NULL;   /* fuer Aussetzer-Anzeige */

/* CPU-Ueberlast-Schutz: liegt die DSP-Last ueber OVERLOAD_PCT fuer
 * OVERLOAD_CHECKS Frames in Folge, wird DSP gestoppt. */
#define OVERLOAD_PCT     100
#define OVERLOAD_CHECKS  30
static int  s_overload_cnt = 0;
static bool s_dsp_stopped  = false;
static char      s_patch_display[64];  /* Anzeigename aktueller Patch */

/* Diagnose: PD-Ausgangspegel (Spitzenwert seit letzter Anzeige, 0..32767) */
static int       s_peak_l = 0, s_peak_r = 0;
static unsigned  s_log_drawn = 0;      /* zuletzt gezeichneter Log-Stand */

/* CTRL-Modus: aus dem aktuellen Patch gelesene Receiver + Panel-Zustand */
static PdCtrlList s_ctrl_list;
static CtrlPanel  s_ctrl_panel;

/* Ein-/Ausblende-Animation beim Wechsel zu/von OPTIONS: der Top-Screen
 * wird zeilenweise auf- bzw. abgebaut. */
#define OPTIONS_ANIM_FRAMES  8    /* Frames fuer den Fade (~133 ms bei 60 fps) */
#define ANIM_TOTAL_LINES     10   /* Inhaltszeilen, die animiert werden */
#define DISPLAY_ALL_LINES    999  /* max_lines-Wert fuer "alles zeigen" */
static int  s_anim_counter = 0;     /* >0 = Animation laeuft (Countdown) */
static bool s_anim_fade_in = true;  /* true = einblenden, false = ausblenden */

/* Zeilenbegrenzung fuer top_line() (von update_display gesetzt) */
static int s_top_max_lines = DISPLAY_ALL_LINES;
static int s_top_lines     = 0;

/* Log-Bereich unten: Zeile 3 = Ueberschrift, ab Zeile 4 die Meldungen */
#define LOG_ROW0 4

/* Eine Zeile auf dem oberen Bildschirm (50 Spalten) ausgeben und mit
 * Leerzeichen auffuellen, damit Reste alter Texte ueberschrieben werden.
 * 49 Zeichen + '\n' vermeidet automatischen Zeilenumbruch. */
static void top_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void top_line(const char *fmt, ...)
{
    char buf[64];
    /* Ueber dem Limit (Animation): Leerzeile statt Inhalt */
    if (s_top_lines++ >= s_top_max_lines) {
        printf("%-49.49s\n", "");
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%-49.49s\n", buf);
}

static void trigger_options_fade(bool fade_in)
{
    s_anim_counter = OPTIONS_ANIM_FRAMES;
    s_anim_fade_in = fade_in;
}

/* ---- Hilfsfunktion: Top-Screen-Status-Anzeige --------------------------
 * max_lines: Anzahl Zeilen mit Inhalt (DISPLAY_ALL_LINES = alle); der Rest
 * bis ANIM_TOTAL_LINES wird mit Leerzeilen ueberschrieben. */
static void update_display(float bpm, bool sync_on, uint32_t ppqn, int tap_count,
                           int max_lines)
{
    (void)tap_count;
    consoleSelect(&s_top_console);
    printf("\x1b[1;1H");   /* Cursor auf Zeile 1, Spalte 1 */
    s_top_max_lines = max_lines;
    s_top_lines     = 0;

    /* Beim Ausblenden ist g_app_mode schon der Zielmodus; abgebaut wird
     * aber noch der OPTIONEN-Bildschirm. */
    AppMode mode = (max_lines < DISPLAY_ALL_LINES && !s_anim_fade_in)
                 ? MODE_OPTIONS : g_app_mode;

    if (mode == MODE_PATCH) {
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
        top_line("Aussetzer: %u   DSP-Last: %3d%%", audio_get_underruns(s_audio),
                 audio_get_dsp_load(s_audio));
        top_line("------------------------------------------------");
        top_line("Tasten / Circle-Pad / Touch -> PD");
        top_line("START: naechster Patch");
        top_line("L+R: Optionen   R: CTRL-Panel");
        /* immer ausgeben (ggf. leer), damit keine Reste stehen bleiben */
        top_line("%s", !s_pd_ok ? "SD: sdmc:/3ds/libpd_3ds/patches/"
                      : s_dsp_stopped ? "CPU-UEBERLAST: DSP gestoppt (START=weiter)"
                      : "");
    } else if (mode == MODE_CTRL) {
        top_line("libpd 3DS                               [CTRL]");
        if (s_pd_ok) {
            top_line("Patch [%d/%d]: %s", s_patch_list.current + 1,
                     s_patch_list.count, s_patch_display);
        } else {
            top_line("Patch: FEHLT %s", s_patch_display);
        }
        top_line("Regler: %d   BPM: %6.1f", s_ctrl_list.count, bpm);
        top_line("Aussetzer: %u   DSP-Last: %3d%%", audio_get_underruns(s_audio),
                 audio_get_dsp_load(s_audio));
        top_line("------------------------------------------------");
        top_line("Touch: Slider ziehen / Toggle antippen");
        top_line("D-Pad U/D: Auswahl scrollen");
        top_line("A/B/X/Y/SELECT, Circle-Pad -> PD");
        top_line("START: naechster Patch");
        top_line("L+R: Optionen   R: CTRL zurueck");
        top_line("%s", s_dsp_stopped ? "CPU-UEBERLAST: DSP gestoppt (START=weiter)" : "");
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
        top_line("L+R: Optionen ausblenden");
    }

    /* Bei Begrenzung restliche Inhaltszeilen leeren */
    while (max_lines < DISPLAY_ALL_LINES && s_top_lines < ANIM_TOTAL_LINES)
        top_line("%s", "");
    s_top_max_lines = DISPLAY_ALL_LINES;
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

/* ---- Hilfsfunktion: unterer Bildschirm --------------------------------- */
static void draw_bottom(void)
{
    consoleSelect(&s_bottom_console);
    consoleClear();

    if (g_app_mode == MODE_CTRL) {
        /* Panel in Zeile 1..29, Zeile 30: Hinweis
         * (letzte Spalte frei, damit die Konsole nicht scrollt) */
        ctrl_panel_draw(&s_ctrl_panel, s_patch_display);
        printf("\x1b[30;1H%-39.39s", "D-Pad U/D: scrollen   R: zurueck");
        consoleSelect(&s_top_console);
        return;
    }

    printf("\x1b[1;1H");
    if (g_app_mode == MODE_PATCH) {
        printf("Touch-Flaeche -> PD (touch-x/y)\n");
    } else {
        printf("Optionen aktiv - Touch geht nicht an PD\n");
    }

    consoleSelect(&s_top_console);
    draw_log();   /* zeichnet nur im Patch-Modus */
}

/* Nur das CTRL-Panel neu zeichnen (nach Scroll / Touch-Aenderung) */
static void draw_ctrl_panel(void)
{
    consoleSelect(&s_bottom_console);
    ctrl_panel_draw(&s_ctrl_panel, s_patch_display);
    consoleSelect(&s_top_console);
}

/* Receiver des geladenen Patches fuer den CTRL-Modus einlesen */
static void load_ctrls(const char *dir, const char *file)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    int n = pd_parse(path, &s_ctrl_list);
    ctrl_panel_init(&s_ctrl_panel, &s_ctrl_list);

    char msg[64];
    if (n < 0) snprintf(msg, sizeof(msg), "CTRL: Patch nicht lesbar");
    else       snprintf(msg, sizeof(msg), "CTRL: %d Regler gefunden", n);
    pd_bridge_log(msg);
}

/* ---- Einstiegspunkt ----------------------------------------------------- */
int main(void)
{
    /* --- 3DS-Services ---------------------------------------------------- */
    /* New 3DS: 804 MHz + L2-Cache aktivieren (alter 3DS: wirkungslos) */
    osSetSpeedupEnable(true);
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
        load_ctrls(first->dir, first->file);
    } else {
        /* Absoluter Fallback: romfs */
        snprintf(s_patch_display, sizeof(s_patch_display), "main (romfs)");
        if (!pd_bridge_init(ROMFS_PATCH_DIR, PATCH_FILE, clk)) {
            printf("FEHLER: kein Patch gefunden\n");
        } else {
            s_pd_ok = true;
        }
        load_ctrls(ROMFS_PATCH_DIR, PATCH_FILE);
    }

    /* --- Button-Eingabe -------------------------------------------------- */
    ButtonInputCtx *btn = button_input_create(clk);
    if (!btn) {
        printf("FEHLER: button_input_create\n");
        goto cleanup_pd;
    }

    /* --- NDSP Audio ------------------------------------------------------ */
    AudioCtx *audio = audio_init(clk);
    s_audio = audio;
    if (!audio) {
        printf("FEHLER: audio_init\n");
        goto cleanup_btn;
    }

    /* Startanzeige */
    consoleClear();
    update_display(DEFAULT_BPM, true, sync_clock_get_ppqn(clk), 0, DISPLAY_ALL_LINES);
    draw_bottom();

    /* --- Main-Loop ------------------------------------------------------- */
    int  frame_count   = 0;

    while (aptMainLoop()) {

        /* hidScanInput() EINMAL pro Frame - touch_input_poll() liest nur */
        hidScanInput();

        AppMode mode_before = g_app_mode;

        /* START: naechster Patch laden (in allen Modi) */
        if (hidKeysDown() & KEY_START) {
            const PatchEntry *e = patch_list_next(&s_patch_list);
            if (e) {
                snprintf(s_patch_display, sizeof(s_patch_display),
                         "%s", e->display);
                s_pd_ok = pd_bridge_reload(e->dir, e->file);
                s_dsp_stopped  = false;
                s_overload_cnt = 0;
                audio_reset_dsp_load(audio);
                load_ctrls(e->dir, e->file);
                if (g_app_mode == MODE_CTRL) draw_ctrl_panel();
            }
        }

        /* Tasten (inkl. Moduswechsel: L+R = OPTIONS, R = CTRL) */
        button_input_poll(btn);

        /* Touch -> PD nur im Patch-Modus */
        touch_input_poll(g_app_mode == MODE_PATCH);

        /* CTRL-Modus: Scroll (D-Pad) und Slider-Touch, nur bei Aenderung zeichnen.
         * Ein Moduswechsel in diesem Frame zeichnet ohnehin alles neu. */
        if (g_app_mode == MODE_CTRL && mode_before == MODE_CTRL) {
            bool redraw = false;
            int  delta  = button_input_ctrl_scroll_delta();
            if (delta != 0) {
                ctrl_panel_scroll(&s_ctrl_panel, delta);
                redraw = true;
            }
            if (ctrl_panel_poll_touch(&s_ctrl_panel)) redraw = true;
            if (redraw) draw_ctrl_panel();
        } else {
            (void)button_input_ctrl_scroll_delta();   /* Reste verwerfen */
        }

        /* Moduswechsel: beide Bildschirme sofort neu zeichnen.
         * Wechsel zu/von OPTIONS zusaetzlich mit Ein-/Ausblendung oben;
         * PATCH <-> CTRL wechselt ohne Animation. */
        if (g_app_mode != mode_before) {
            bool to_opts   = (g_app_mode  == MODE_OPTIONS);
            bool from_opts = (mode_before == MODE_OPTIONS);
            if (to_opts || from_opts) trigger_options_fade(to_opts);
            else                      s_anim_counter = 0;
            s_ctrl_panel.active = -1;   /* kein "geerbter" Slider-Griff */
            consoleSelect(&s_top_console);
            consoleClear();
            draw_bottom();
            frame_count = DISPLAY_UPDATE_FRAMES;   /* Top sofort aktualisieren */
        }

        /* Sync-Mute-Status an Audio-Modul weitergeben */
        audio_set_sync_enabled(audio, button_input_sync_enabled(btn));

        /* Audio befuellen (PD + Sync; gibt sofort zurueck wenn nicht noetig) */
        audio_fill_buffer(audio, NULL);

        /* CPU-Ueberlast: DSP stoppen statt die App einfrieren zu lassen.
         * Bei Ueberlast dauert jeder Frame laenger als 1/60 s, daher pro
         * Frame pruefen (30 Frames in Folge = ca. 1-3 s). */
        if (!s_dsp_stopped && audio_get_dsp_load(audio) > OVERLOAD_PCT) {
            if (++s_overload_cnt >= OVERLOAD_CHECKS) {
                pd_bridge_set_dsp(false);
                s_dsp_stopped = true;
                pd_bridge_log("CPU-Ueberlast: DSP gestoppt");
                frame_count = DISPLAY_UPDATE_FRAMES;   /* Anzeige sofort */
            }
        } else {
            s_overload_cnt = 0;
        }

        /* Neue PD-Meldungen -> Log unten neu zeichnen (nur Patch-Modus) */
        if (g_app_mode == MODE_PATCH && pd_bridge_log_serial() != s_log_drawn) {
            draw_log();
        }

        /* Anzeige-Update: waehrend der Animation jeden Frame, sonst
         * seltener (alle DISPLAY_UPDATE_FRAMES) um CPU zu schonen. */
        if (s_anim_counter > 0) {
            s_anim_counter--;
            int progress   = OPTIONS_ANIM_FRAMES - s_anim_counter;   /* 1..N */
            int show_lines = s_anim_fade_in
                ? (progress * ANIM_TOTAL_LINES / OPTIONS_ANIM_FRAMES)
                : ((OPTIONS_ANIM_FRAMES - progress + 1) * ANIM_TOTAL_LINES
                   / OPTIONS_ANIM_FRAMES);
            if (show_lines < 0) show_lines = 0;
            update_display(button_input_get_bpm(btn),
                           button_input_sync_enabled(btn),
                           button_input_get_ppqn(btn),
                           0 /* tap_count */, show_lines);
            if (s_anim_counter == 0) {
                /* Animation fertig: Zielmodus im naechsten Frame komplett */
                consoleSelect(&s_top_console);
                consoleClear();
                frame_count = DISPLAY_UPDATE_FRAMES;
            }
        } else if (++frame_count >= DISPLAY_UPDATE_FRAMES) {
            frame_count = 0;
            audio_get_pd_peak(audio, &s_peak_l, &s_peak_r);
            update_display(button_input_get_bpm(btn),
                           button_input_sync_enabled(btn),
                           button_input_get_ppqn(btn),
                           0 /* tap_count */, DISPLAY_ALL_LINES);
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

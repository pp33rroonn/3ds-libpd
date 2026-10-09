/* main.c - libpd 3DS Homebrew Einstiegspunkt
 *
 * Stereo-Kanal-Belegung
 * ---------------------
 *   Links  : Pure Data Audio-Ausgang (Mono-PD-Mix)
 *   Rechts : 2ppqn Sync-Puls (sync_clock, intern im Audio-Modul)
 *
 * BPM-Steuerung
 * -------------
 *   D-Pad Links/Rechts  : BPM -1 / +1
 *   D-Pad Hoch/Runter   : BPM +10 / -10
 *   Schulter L/R        : BPM -10 / +10
 *   A-Taste             : Tap-Tempo
 *   Circle-Pad Y        : kontinuierlich
 *   SELECT              : Sync rechter Kanal ein/aus
 *   START               : Beenden
 *
 * PD-Patch kann BPM aendern via: [send pd-bpm 130]
 *
 * Top-Screen-Anzeige (konsole):
 *   Zeile 0: BPM (aktuell), SYNC-Status
 *   Zeile 1: Tap-Tempo-Hinweis
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

#include "audio/audio.h"
#include "sync/sync_clock.h"
#include "input/touch_input.h"
#include "input/button_input.h"
#include "pd_bridge/pd_bridge.h"

/* Muss mit sync_clock.h / Makefile uebereinstimmen */
#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

#define DEFAULT_BPM   120.0f
#define PATCH_DIR     "romfs:/patches"
#define PATCH_FILE    "main.pd"

/* Anzeige-Update nur alle N Frames (60fps -> alle 30 Frames = 2 Hz) */
#define DISPLAY_UPDATE_FRAMES 30

/* PD-Status fuer die Anzeige (wird von update_display staendig gezeigt) */
static bool s_pd_ok = false;

/* ---- Hilfsfunktion: Top-Screen-Status-Anzeige -------------------------- */
static void update_display(float bpm, bool sync_on, uint32_t ppqn, int tap_count)
{
    /* Gerät-Label abhängig vom ppqn-Modus */
    const char *ppqn_label;
    switch (ppqn) {
        case  1: ppqn_label = " 1ppqn (Korg MS)     "; break;
        case  2: ppqn_label = " 2ppqn (Volca/PO)    "; break;
        case 24: ppqn_label = "24ppqn (MIDI/DINSync)"; break;
        default: ppqn_label = "??ppqn               "; break;
    }

    /* Cursor auf Zeile 0 setzen (ANSI-Escape) */
    printf("\x1b[1;1H");
    printf("libpd 3DS                         \n");
    printf("BPM: %6.1f  SYNC: %s            \n",
           bpm, sync_on ? " ON (R-CH)" : "OFF       ");
    printf("L=PD Audio   R=%s\n", ppqn_label);
    printf("----------------------------------\n");
    printf("D-Pad L/R: BPM -1/+1              \n");
    printf("D-Pad U/D: BPM +10/-10            \n");
    printf("Schulter L/R: BPM -10/+10         \n");
    printf("A: Tap-Tempo (Taps: %d/3+)         \n", tap_count);
    printf("X: ppqn wechseln (1/2/24)         \n");
    printf("SELECT: Sync ein/aus              \n");
    printf("START: Beenden                    \n");
    printf("----------------------------------\n");
    printf("Touch: sendet touch-x/y an PD     \n");
    printf("PD: [send pd-bpm <float>]         \n");
    printf("----------------------------------\n");
    printf("Patch: %s\n", s_pd_ok ? "OK  romfs:/patches/main.pd     "
                                   : "FEHLT romfs:/patches/main.pd  ");
}

/* ---- Einstiegspunkt ----------------------------------------------------- */
int main(void)
{
    /* --- 3DS-Services ---------------------------------------------------- */
    gfxInitDefault();
    PrintConsole top_console;
    consoleInit(GFX_TOP, &top_console);
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
    if (!pd_bridge_init(PATCH_DIR, PATCH_FILE, clk)) {
        printf("FEHLER: PD-Patch nicht gefunden:\n  %s/%s\n",
               PATCH_DIR, PATCH_FILE);
        printf("Patch in romfs/patches/ legen und neu bauen.\n");
        /* Kein Abbruch - Audio laeuft trotzdem (Stille von PD-Seite) */
    } else {
        s_pd_ok = true;
        printf("PD OK: %s/%s\n", PATCH_DIR, PATCH_FILE);
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

    /* --- Main-Loop ------------------------------------------------------- */
    int frame_count = 0;

    while (aptMainLoop()) {

        /* hidScanInput() EINMAL pro Frame - touch_input_poll() liest nur */
        hidScanInput();

        /* Beenden */
        if (hidKeysDown() & KEY_START) break;

        /* Eingaben verarbeiten */
        button_input_poll(btn);   /* BPM via D-Pad, Circle-Pad, Tap */
        touch_input_poll();       /* Touch -> PD Messages            */

        /* Sync-Mute-Status an Audio-Modul weitergeben */
        audio_set_sync_enabled(audio, button_input_sync_enabled(btn));

        /* Audio befuellen (PD + Sync; gibt sofort zurueck wenn nicht noetig) */
        audio_fill_buffer(audio, NULL);

        /* Anzeige-Update (seltener um CPU zu schonen) */
        if (++frame_count >= DISPLAY_UPDATE_FRAMES) {
            frame_count = 0;
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

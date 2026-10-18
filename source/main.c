/* main.c - libpd 3DS homebrew entry point
 *
 * Stereo channel layout
 * ---------------------
 *   Sync on : left = PD mono mix, right = sync pulse
 *   Sync off: left/right = PD stereo
 *
 * Screens
 * -------
 *   Top    : pixel UI (RGB565), redrawn completely every frame (double
 *            buffered). Either the OPTIONS overlay (START) or the active
 *            page: Mixer / Patch 1..5 / PD Log (ZL / ZR), see ui/.
 *   Bottom : text console (touch area) or the CTRL slider panel (R).
 *
 * Patch selection: OPTIONS -> Patch 1 (slot 0 is the running patch).
 * The PD patch can change the BPM via: [send pd-bpm 130]
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

#include "audio/audio.h"
#include "sync/sync_clock.h"
#include "input/touch_input.h"
#include "input/button_input.h"
#include "input/motion_input.h"
#include "input/cstick_input.h"
#include "pd_bridge/pd_bridge.h"
#include "fs/patch_list.h"
#include "fs/pd_parser.h"
#include "input/ctrl_panel.h"
#include "app_state.h"
#include "ui/options_menu.h"
#include "ui/top_screen.h"

/* Must match sync_clock.h / Makefile */
#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

#define DEFAULT_BPM   120.0f
#define ROMFS_PATCH_DIR "romfs:/patches"
#define PATCH_FILE    "main.pd"

/* Peak meter refresh every N frames (60 fps -> 2 Hz) */
#define PEAK_UPDATE_FRAMES 30

/* Global app mode (declared in app_state.h) */
AppMode g_app_mode = MODE_PATCH;

/* Bottom console: touch area / CTRL panel */
static PrintConsole s_bottom_console;

static bool      s_pd_ok = false;
static PatchList s_patch_list;
static char      s_patch_display[64];   /* name of the running patch */

/* CPU overload guard: if the DSP load stays above OVERLOAD_PCT for
 * OVERLOAD_CHECKS frames in a row, DSP is stopped. */
#define OVERLOAD_PCT     100
#define OVERLOAD_CHECKS  30
static int  s_overload_cnt = 0;
static bool s_dsp_stopped  = false;

/* CTRL mode: receivers read from the current patch + panel state */
static PdCtrlList s_ctrl_list;
static CtrlPanel  s_ctrl_panel;

/* ---- bottom screen -------------------------------------------------------- */
static void draw_bottom(void)
{
    consoleClear();

    if (g_app_mode == MODE_CTRL) {
        /* panel in rows 1..29, row 30: hint
         * (last column left free so the console does not scroll) */
        ctrl_panel_draw(&s_ctrl_panel, s_patch_display);
        printf("\x1b[30;1H%-39.39s", "D-pad U/D: scroll   R: back");
        ctrl_panel_draw_faders(&s_ctrl_panel);   /* CTRL branch only */
        return;
    }

    printf("\x1b[1;1H");
    if (g_options_open) {
        printf("OPTIONS open - touch not sent to PD\n");
    } else {
        printf("Touch area -> PD (touch-x/y)\n");
    }
    printf("\nPD log: top screen, ZL/ZR to select\n");
    printf("START: options   R: CTRL panel\n");
}

/* Redraw only the CTRL panel (after scroll / touch changes) */
static void draw_ctrl_panel(void)
{
    ctrl_panel_draw(&s_ctrl_panel, s_patch_display);
    /* pixel faders after the text so console writes do not overwrite them */
    ctrl_panel_draw_faders(&s_ctrl_panel);
}

/* Read the receivers of the loaded patch for CTRL mode */
static void load_ctrls(const char *dir, const char *file)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    int n = pd_parse(path, &s_ctrl_list);
    ctrl_panel_init(&s_ctrl_panel, &s_ctrl_list);

    char msg[64];
    if (n < 0) snprintf(msg, sizeof(msg), "CTRL: patch not readable");
    else       snprintf(msg, sizeof(msg), "CTRL: %d controls found", n);
    pd_bridge_log(msg);
}

/* Switch the running patch (OPTIONS -> Patch 1) */
static void load_patch(int idx, AudioCtx *audio)
{
    if (idx < 0 || idx >= s_patch_list.count) return;
    s_patch_list.current = idx;
    const PatchEntry *e = patch_list_current(&s_patch_list);
    if (!e) return;

    snprintf(s_patch_display, sizeof(s_patch_display), "%s", e->display);
    s_pd_ok = pd_bridge_reload(e->dir, e->file);
    motion_input_resend();   /* new patch: send the current orientation */
    cstick_input_resend();
    s_dsp_stopped  = false;
    s_overload_cnt = 0;
    audio_reset_dsp_load(audio);
    load_ctrls(e->dir, e->file);
    if (g_app_mode == MODE_CTRL) draw_bottom();
}

static void update_status(void)
{
    if (!s_pd_ok)
        top_screen_set_status("Patch missing: sdmc:/3ds/libpd_3ds/patches/");
    else if (s_dsp_stopped)
        top_screen_set_status("CPU overload: DSP stopped (select a patch)");
    else
        top_screen_set_status("");
}

/* ---- entry point ---------------------------------------------------------- */
int main(void)
{
    /* --- 3DS services ------------------------------------------------------ */
    /* New 3DS: 804 MHz + L2 cache (old 3DS: no effect) */
    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);   /* pixel UI on top */
    consoleInit(GFX_BOTTOM, &s_bottom_console);
    hidInit();
    motion_input_init();   /* accelerometer + gyro on */
    cstick_input_init();   /* C-stick (New 3DS); old 3DS: stays off */
    romfsInit();

    consoleClear();
    printf("Initializing...\n");

    /* --- sync clock -------------------------------------------------------- */
    SyncClock *clk = sync_clock_create(SAMPLERATE, DEFAULT_BPM);
    if (!clk) {
        printf("ERROR: sync_clock_create\n");
        goto cleanup_gfx;
    }

    /* --- libpd / PD bridge ------------------------------------------------- */
    /* scan patches (SD first, then romfs) */
    patch_list_scan(&s_patch_list);

    const PatchEntry *first = patch_list_current(&s_patch_list);
    if (first) {
        snprintf(s_patch_display, sizeof(s_patch_display), "%s", first->display);
        if (!pd_bridge_init(first->dir, first->file, clk)) {
            printf("ERROR: PD patch not loaded\n");
        } else {
            s_pd_ok = true;
        }
        load_ctrls(first->dir, first->file);
    } else {
        /* last resort: romfs */
        snprintf(s_patch_display, sizeof(s_patch_display), "main (romfs)");
        if (!pd_bridge_init(ROMFS_PATCH_DIR, PATCH_FILE, clk)) {
            printf("ERROR: no patch found\n");
        } else {
            s_pd_ok = true;
        }
        load_ctrls(ROMFS_PATCH_DIR, PATCH_FILE);
    }

    /* --- UI state ------------------------------------------------------------ */
    options_menu_set_patch_list(&s_patch_list);
    g_options_open = false;
    g_app_page     = PAGE_MIXER;
    options_menu_init(&g_menu);
    g_menu.bpm = sync_clock_get_bpm(clk);
    sync_clock_set_ppqn(clk, options_menu_ppqn(&g_menu));

    /* --- button input ------------------------------------------------------ */
    ButtonInputCtx *btn = button_input_create(clk);
    if (!btn) {
        printf("ERROR: button_input_create\n");
        goto cleanup_pd;
    }

    /* --- NDSP audio -------------------------------------------------------- */
    AudioCtx *audio = audio_init(clk);
    if (!audio) {
        printf("ERROR: audio_init\n");
        goto cleanup_btn;
    }

    draw_bottom();

    /* --- main loop --------------------------------------------------------- */
    int frame_count = 0;

    while (aptMainLoop()) {

        /* hidScanInput() ONCE per frame - the input modules only read */
        hidScanInput();

        AppMode mode_before    = g_app_mode;
        bool    options_before = g_options_open;

        /* buttons (START = OPTIONS, ZL/ZR = pages, R = CTRL) */
        button_input_poll(btn);

        /* patch slot 0 changed in OPTIONS (applied once the value settled) */
        int req = options_menu_take_patch_request(&g_menu);
        if (req >= 0 && (req != s_patch_list.current || s_dsp_stopped || !s_pd_ok))
            load_patch(req, audio);

        bool to_pd = !g_options_open;

        /* touch -> PD only in PATCH mode */
        touch_input_poll(to_pd && g_app_mode == MODE_PATCH);

        /* tilt/rotation -> PD (accel-*, gyro-*) */
        motion_input_poll(to_pd);

        /* C-stick -> PD (cstick-x, cstick-y) */
        cstick_input_poll(to_pd);

        /* CTRL mode: scroll (D-pad) and slider touch, redraw only on change.
         * A mode change in this frame redraws everything anyway. */
        if (to_pd && g_app_mode == MODE_CTRL && mode_before == MODE_CTRL) {
            bool redraw = false;
            int  delta  = button_input_ctrl_scroll_delta();
            if (delta != 0) {
                ctrl_panel_scroll(&s_ctrl_panel, delta);
                redraw = true;
            }
            if (ctrl_panel_poll_touch(&s_ctrl_panel)) redraw = true;
            if (redraw) draw_ctrl_panel();
        } else {
            (void)button_input_ctrl_scroll_delta();   /* drop leftovers */
        }

        /* mode or OPTIONS change: redraw the bottom screen */
        if (g_app_mode != mode_before) {
            s_ctrl_panel.active = -1;   /* no inherited slider grab */
            draw_bottom();
        } else if (g_options_open != options_before &&
                   g_app_mode == MODE_PATCH) {
            draw_bottom();
        }

        /* OPTIONS -> clock / audio; otherwise follow the clock ([send pd-bpm]) */
        if (g_menu.bpm_changed) {
            sync_clock_set_bpm(clk, g_menu.bpm);
            g_menu.bpm_changed = false;
        } else {
            g_menu.bpm = sync_clock_get_bpm(clk);
        }
        if (g_menu.sync_changed) {
            sync_clock_set_ppqn(clk, options_menu_ppqn(&g_menu));
            g_menu.sync_changed = false;
        }
        audio_set_sync_enabled(audio, options_menu_sync_on(&g_menu));

        /* fill audio (PD + sync; returns at once if not needed) */
        audio_fill_buffer(audio, NULL);

        /* CPU overload: stop DSP instead of freezing the app. Under overload
         * every frame takes longer than 1/60 s, so check every frame
         * (30 frames in a row = about 1-3 s). */
        if (!s_dsp_stopped && audio_get_dsp_load(audio) > OVERLOAD_PCT) {
            if (++s_overload_cnt >= OVERLOAD_CHECKS) {
                pd_bridge_set_dsp(false);
                s_dsp_stopped = true;
                pd_bridge_log("CPU overload: DSP stopped");
            }
        } else {
            s_overload_cnt = 0;
        }

        /* peak meter (Mixer page) */
        if (++frame_count >= PEAK_UPDATE_FRAMES) {
            int pl = 0, pr = 0;
            frame_count = 0;
            audio_get_pd_peak(audio, &pl, &pr);
            top_screen_set_peak(pl, pr);
        }
        update_status();

        /* top screen: draw ONLY the active view, the whole screen, every
         * frame (the back buffer is stale after each swap) */
        u16 *top_fb = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        float    bpm      = sync_clock_get_bpm(clk);
        int      dsp_pct  = audio_get_dsp_load(audio);
        unsigned glitches = audio_get_underruns(audio);
        if (g_options_open)
            options_menu_draw(top_fb, &g_menu, bpm, dsp_pct, glitches);
        else
            top_screen_draw(top_fb, &g_menu, bpm, dsp_pct, glitches);

        gfxFlushBuffers();
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    /* --- cleanup ----------------------------------------------------------- */
    audio_deinit(audio);

cleanup_btn:
    button_input_destroy(btn);

cleanup_pd:
    pd_bridge_deinit();
    sync_clock_destroy(clk);

cleanup_gfx:
    romfsExit();
    motion_input_exit();
    cstick_input_exit();   /* irrstExit only if irrstInit succeeded */
    hidExit();
    gfxExit();

    return 0;
}

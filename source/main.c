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
 *            page: Waveform (start) / Patch 1..5 (ZL / ZR) / PD Log
 *            (ZL+ZR combo), see ui/.
 *   Bottom : RGB565. BOT_MIXER (start, SELECT): pixel mixer (5 slot
 *            faders); touch goes ONLY to the faders, never to PD; redrawn
 *            every frame. BOT_CTRL (after ZL / ZR navigation, SELECT): pixel
 *            CTRL panel (font5x8 text + widgets, EYE theme colours) of the
 *            active patch slot, single buffered, touch also goes to PD
 *            (touch-x/y); redrawn on change only (incl. theme change).
 *
 * Patch selection: OPTIONS -> Patch 1..5 (slots 0..4, "Empty" or any
 * patch, all run simultaneously). All slots start empty.
 *
 * Input routing: only the patch of the active page (ZL / ZR) receives the
 * inputs (touch, buttons, pad, accel, gyro, cstick, CTRL panel). On the
 * Waveform page, the PD Log and in OPTIONS no patch gets input; all
 * patches keep playing. Patch files use plain names ([r touch-x]), the
 * slot copy renames them per slot (pd_bridge/pd_rewrite.h).
 * Global clock (sync/sync_clock.h): one 24-ppqn master clock feeds
 * clock-step / clock-tick / clock-bpm / clock-ms to ALL patches, so every
 * slot runs in lockstep. OPTIONS tempo is authoritative; a loaded patch can
 * request a tempo via [send pd-bpm 130] (ignored while the patch loads).
 */

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "camera/camera.h"
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
#include "ui/cam_view.h"
#include "ui/canvas_view.h"
#include "ui/scope_view.h"
#include "ui/mixer_screen.h"
#include "ui/draw.h"
#include "ui/font5x8.h"
#include "ui/theme.h"

/* Must match sync_clock.h / Makefile */
#ifndef SAMPLERATE
#  define SAMPLERATE 44100
#endif

#define DEFAULT_BPM   120.0f

/* Top-screen peak numbers refresh every N frames (60 fps -> 2 Hz) */
#define PEAK_UPDATE_FRAMES 30
/* Bottom mixer peak meter: falls by this much per frame */
#define PEAK_DECAY 1200

/* Global app mode (declared in app_state.h) */
AppMode g_app_mode = BOT_MIXER;

static bool      s_pd_ok = false;       /* libpd initialised */
static PatchList s_patch_list;

/* CPU overload guard: if the DSP load stays above OVERLOAD_PCT for
 * OVERLOAD_CHECKS frames in a row, DSP is stopped. */
#define OVERLOAD_PCT     100
#define OVERLOAD_CHECKS  30
static int  s_overload_cnt = 0;
static bool s_dsp_stopped  = false;

/* CTRL mode: receivers read from the current patch + panel state */
static PdCtrlList s_ctrl_empty;   /* shown when the slot has no list */
static CtrlPanel  s_ctrl_panel;
/* Patch slot whose CTRL screen is shown (-1 = none -> "kein Patch geladen") */
static int        s_ctrl_slot = -1;
/* Slot whose CTRL list the panel shows (-1 = must be re-bound) */
static int        s_ctrl_list_slot = -1;
static char       s_ctrl_display[64];   /* patch name shown on the panel */

/* Slot shown on the CTRL bottom screen: the active top-screen slot if it
 * is loaded, else -1 (Waveform, PD Log, empty / unloaded slot). */
static int ctrl_slot(void)
{
    int slot = top_screen_get_active_slot();
    if (slot < 0 || !pd_slot_is_loaded(slot)) return -1;
    return slot;
}

/* Output peaks: s_peak_* = mixer meter (read every frame, with decay),
 * s_peak_hold_* = max since the last top-screen update */
static int s_peak_l = 0, s_peak_r = 0;   /* kept for diagnostics */
static int s_peak_hold_l = 0, s_peak_hold_r = 0;
/* scope copy for the Waveform page (static: 8 KB, not on the stack) */
static int16_t s_scope_l[AUDIO_SCOPE_FRAMES], s_scope_r[AUDIO_SCOPE_FRAMES];

/* ---- bottom screen -------------------------------------------------------- */
static void ensure_ctrls(int slot);

/* CAM image is shown in the top-screen PatchView in modes 2 ("Bild") and 3
 * ("Bild+Matrix") for the camera patch; modes 0 ("Aus") and 1 ("Fader") show no
 * image. The bottom screen no longer renders the camera (faders use the full
 * area again). */
static inline bool cam_img_active(void) { return g_menu.cam_mode >= 2; }

/* CTRL hint line: row 30 of the 8-px grid would start at y = 232 and clip
 * the 9-px glyph cell, so it sits one pixel higher (y 231..239). */
#define BOT_HINT_Y   231
/* "kein Patch geladen": row 15 of the 8-px grid */
#define BOT_EMPTY_Y  112

/* BOT_CTRL only: full pixel redraw of the bottom screen (EYE theme).
 * The BOT_MIXER bottom screen is the pixel mixer, which main() redraws
 * completely every frame. */
static void draw_bottom(void)
{
    if (g_app_mode != BOT_CTRL) return;
    if (gfxGetScreenFormat(GFX_BOTTOM) != GSP_RGB565_OES) return;
    u16 *fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!fb) return;

    draw_clear_bot(fb, TH.bg);
    s_ctrl_slot = ctrl_slot();
    if (s_ctrl_slot >= 0) ensure_ctrls(s_ctrl_slot);
    if (s_ctrl_slot < 0) {
        draw_string_bot(fb, 2 * FONT_ADV_X, BOT_EMPTY_Y, "kein Patch geladen",
                        theme_fg_at(BOT_EMPTY_Y + 4), TH.bg);
        draw_string_bot(fb, 0, BOT_HINT_Y, "ZL/ZR: patch   SELECT: mixer",
                        TH.dim, TH.bg);
        return;
    }
    /* panel in rows 1..29 (y 0..231), hint below */
    ctrl_panel_draw(&s_ctrl_panel, s_ctrl_display);
    draw_string_bot(fb, 0, BOT_HINT_Y, "D-pad U/D: scroll   SELECT: mixer",
                    TH.dim, TH.bg);
    ctrl_panel_draw_faders(&s_ctrl_panel);   /* CTRL branch only */
}

/* Bottom screen buffering per mode (both RGB565):
 *   BOT_MIXER: pixel mixer, double buffered, full redraw every frame
 *   BOT_CTRL : pixel CTRL panel, single buffered, redrawn on change only */
static void set_bottom_mode(AppMode mode)
{
    if (mode == BOT_MIXER) {
        gfxSetDoubleBuffering(GFX_BOTTOM, true);
    } else {
        gfxSetDoubleBuffering(GFX_BOTTOM, false);
        gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    }
}

/* Redraw only the CTRL panel (after scroll / touch changes) */
static void draw_ctrl_panel(void)
{
    ctrl_panel_draw(&s_ctrl_panel, s_ctrl_display);
    ctrl_panel_draw_faders(&s_ctrl_panel);   /* widgets after the text */
}

/* Bind the panel to the CTRL list of `slot` (0..4). The list itself lives
 * in pd_bridge (read on load, values kept, updated by the patch). */
static void ensure_ctrls(int slot)
{
    if (slot == s_ctrl_list_slot || slot < 0 || slot >= MENU_PATCH_SLOTS) return;
    PdCtrlList *l = pd_slot_ctrls(slot);
    int idx = g_menu.patch_idx[slot];
    if (l && idx >= 0 && idx < s_patch_list.count)
        snprintf(s_ctrl_display, sizeof(s_ctrl_display), "%s",
                 s_patch_list.entries[idx].display);
    else
        s_ctrl_display[0] = '\0';
    if (!l) {   /* empty slot: empty panel (avoids re-binding every frame) */
        memset(&s_ctrl_empty, 0, sizeof(s_ctrl_empty));
        l = &s_ctrl_empty;
    }
    ctrl_panel_init(&s_ctrl_panel, l);
    s_ctrl_list_slot = slot;
    (void)pd_slot_ctrls_take_dirty(slot);   /* full redraw follows */
}

/* OPTIONS -> Patch 1..5: load (patch_idx >= 0) or empty (-1) slot 0..4 */
static void apply_slot_request(int slot, int patch_idx, AudioCtx *audio)
{
    if (slot < 0 || slot >= PD_SLOT_COUNT) return;
    if (slot == s_ctrl_list_slot) s_ctrl_list_slot = -1;   /* re-read */
    if (patch_idx < 0 || patch_idx >= s_patch_list.count) {
        pd_slot_unload(slot);
        char msg[32];
        snprintf(msg, sizeof(msg), "slot %d: empty", slot);
        pd_bridge_log(msg);
        return;
    }
    const PatchEntry *e = &s_patch_list.entries[patch_idx];
    if (!pd_slot_load(slot, e->dir, e->file)) return;

    /* a new patch is a fresh start: DSP back on after an overload stop */
    s_dsp_stopped  = false;
    s_overload_cnt = 0;
    audio_reset_dsp_load(audio);
    pd_bridge_set_dsp(true);
    scope_view_reset();     /* drop envelope history / peak holds */
    if (slot == pd_bridge_get_input_slot()) {
        motion_input_resend();   /* send the current orientation */
        cstick_input_resend();
    }
}

static void update_status(void)
{
    if (!s_pd_ok)
        top_screen_set_status("libpd init failed");
    else if (s_patch_list.count == 0)
        top_screen_set_status("No patches: sdmc:/3ds/libpd_3ds/patches/");
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
    /* bottom = RGB565 pixel UI (mixer double buffered at start) */
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    hidInit();
    motion_input_init();   /* accelerometer + gyro on */
    cstick_input_init();   /* C-stick (New 3DS); old 3DS: stays off */
    camera_init();         /* control source; stays off until enabled */
    romfsInit();

    /* --- sync clock -------------------------------------------------------- */
    SyncClock *clk = sync_clock_create(SAMPLERATE, DEFAULT_BPM);
    if (!clk) {
        pd_bridge_log("ERROR: sync_clock_create");
        goto cleanup_gfx;
    }

    /* --- libpd / PD bridge ------------------------------------------------- */
    /* scan patches (SD first, then romfs) */
    patch_list_scan(&s_patch_list);

    /* libpd only: all patch slots start empty (OPTIONS -> Patch 1..5) */
    s_pd_ok = pd_bridge_init(clk);
    if (!s_pd_ok) pd_bridge_log("ERROR: libpd init");
    pd_bridge_set_input_slot(-1);

    /* --- UI state ------------------------------------------------------------ */
    options_menu_set_patch_list(&s_patch_list);
    g_options_open = false;
    g_app_mode     = BOT_MIXER;    /* start: Waveform on top, Mixer below */
    g_app_page     = PAGE_WAVEFORM;
    options_menu_init(&g_menu);   /* all slots Empty, sync Off */
    g_menu.bpm = sync_clock_get_bpm(clk);
    sync_clock_set_ppqn(clk, options_menu_ppqn(&g_menu));
    sync_clock_set_swing(clk, g_menu.swing_pct);
    sync_clock_set_rate_q4(clk, g_menu.rate_q4);

    /* --- button input ------------------------------------------------------ */
    ButtonInputCtx *btn = button_input_create(clk);
    if (!btn) {
        pd_bridge_log("ERROR: button_input_create");
        goto cleanup_pd;
    }

    /* --- NDSP audio -------------------------------------------------------- */
    AudioCtx *audio = audio_init(clk);
    if (!audio) {
        pd_bridge_log("ERROR: audio_init");
        goto cleanup_btn;
    }

    set_bottom_mode(g_app_mode);
    draw_bottom();

    /* --- main loop --------------------------------------------------------- */
    int frame_count = 0;
    int last_theme  = g_theme_idx;   /* CTRL screen: redraw on theme change */

    while (aptMainLoop()) {

        /* hidScanInput() ONCE per frame - the input modules only read */
        hidScanInput();

        AppMode mode_before    = g_app_mode;

        /* buttons (START = OPTIONS, ZL/ZR = pages, ZL+ZR = log,
         * SELECT = mixer <-> CTRL) */
        button_input_poll(btn);

        /* Waveform has no CTRL panel: the bottom screen there is always the
         * mixer. Clamp right away (also covers top_screen_draw() falling
         * back to Waveform when the shown slot was unloaded). */
        if (g_app_page == PAGE_WAVEFORM && g_app_mode == BOT_CTRL)
            g_app_mode = BOT_MIXER;

        /* patch slots changed in OPTIONS (applied once the value settled) */
        options_menu_tick(&g_menu);
        int slot_patch, slot;
        while ((slot = options_menu_take_slot_request(&g_menu, &slot_patch)) >= 0)
            apply_slot_request(slot, slot_patch, audio);

        bool to_pd = !g_options_open;

        /* input routing: only the patch of the active page gets input.
         * Clamped right here so every input module below uses this frame's
         * slot (none on Waveform / PD Log / OPTIONS / empty slot). */
        {
            int in_slot = to_pd ? ctrl_slot() : -1;
            int old     = pd_bridge_get_input_slot();
            if (in_slot != old) {
                touch_input_switch_slot(old);   /* touch-off to the old slot */
                pd_bridge_set_input_slot(in_slot);
                motion_input_resend();          /* new slot: current state */
                cstick_input_resend();
            }
        }

        /* touch routing:
         *   BOT_MIXER: touch goes ONLY to the mixer faders, never to PD
         *   BOT_CTRL : touch goes to PD (touch-x/y) and to the CTRL panel */
        if (to_pd && g_app_mode == BOT_MIXER) {
            touchPosition tp = {0};
            bool touching = (hidKeysHeld() & KEY_TOUCH) != 0;
            if (touching) hidTouchRead(&tp);
            (void)mixer_screen_handle_touch(tp.px, tp.py, touching);
        } else {
            mixer_screen_handle_touch(0, 0, false);   /* drop any grab */
        }
        touch_input_poll(to_pd && g_app_mode == BOT_CTRL);

        /* tilt/rotation -> PD (accel-*, gyro-*) */
        motion_input_poll(to_pd);

        /* C-stick -> PD (cstick-x, cstick-y) */
        cstick_input_poll(to_pd);

        /* CTRL mode: scroll (D-pad) and slider touch, redraw only on change.
         * A mode change in this frame redraws everything anyway. */
        if (to_pd && g_app_mode == BOT_CTRL && mode_before == BOT_CTRL &&
            s_ctrl_slot >= 0) {
            bool redraw = false;
            int  delta  = button_input_ctrl_scroll_delta();
            if (delta != 0) {
                ctrl_panel_scroll(&s_ctrl_panel, delta);
                redraw = true;
            }
            if (ctrl_panel_poll_touch(&s_ctrl_panel)) redraw = true;
            /* values sent by the patch itself ([s name], loadbang, ...) */
            if (pd_slot_ctrls_take_dirty(s_ctrl_slot)) redraw = true;
            if (redraw) draw_ctrl_panel();
        } else {
            (void)button_input_ctrl_scroll_delta();   /* drop leftovers */
            /* panel not shown: the flags stay pending; values are kept in
             * the slot lists and drawn by the next full / panel redraw */
        }

        /* mode change: redraw the CTRL bottom screen (the mixer is redrawn
         * every frame below) */
        if (g_app_mode != mode_before) {
            s_ctrl_panel.active = -1;   /* no inherited slider grab */
            set_bottom_mode(g_app_mode);
            draw_bottom();
        } else if (g_app_mode == BOT_CTRL &&
                   (ctrl_slot() != s_ctrl_slot ||
                    (s_ctrl_slot >= 0 && s_ctrl_list_slot != s_ctrl_slot))) {
            /* other page / slot (un)loaded / patch changed while CTRL is shown */
            s_ctrl_panel.active = -1;
            draw_bottom();
        } else if (g_app_mode == BOT_CTRL && g_theme_idx != last_theme) {
            draw_bottom();   /* OPTIONS -> Eye changed while CTRL is shown */
        }
        last_theme = g_theme_idx;

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
        if (g_menu.clock_changed) {   /* Swing / Rate */
            sync_clock_set_swing(clk, g_menu.swing_pct);
            sync_clock_set_rate_q4(clk, g_menu.rate_q4);
            g_menu.clock_changed = false;
        }
        if (g_menu.clock_reset) {     /* RESET: every patch back to beat 1 */
            sync_clock_reset(clk);
            g_menu.clock_reset = false;
        }
        /* OPTIONS -> Faders: CTRL panel layout (full redraw if shown) */
        if (g_menu.fader_layout_changed) {
            g_menu.fader_layout_changed = false;
            ctrl_panel_set_layout(&s_ctrl_panel, g_menu.fader_layout);
            if (g_app_mode == BOT_CTRL) draw_bottom();
        }
        audio_set_sync_enabled(audio, options_menu_sync_on(&g_menu));

        /* OPTIONS -> Mikrofon: adc~ 1 + globales mic-level ein/aus */
        if (g_menu.mic_changed) {
            g_menu.mic_changed = false;
            audio_set_mic_enabled(audio, g_menu.mic_on != 0);
        }
        /* Header-Indikator jeden Frame am tatsaechlichen HW-Zustand ausrichten */
        top_screen_set_mic(audio_mic_active(audio));

        /* OPTIONS -> Kamera (4 Modi: Aus / Fader / Bild / Bild+Matrix).
         * Aus      : Hardware aus, kein cam-* und kein Bild.
         * Fader    : Hardware an, nur cam-bright / cam-r/g/b an PD.
         * Bild     : zusaetzlich GB-Kamera-Bild unten rendern.
         * Bild+Matrix: zusaetzlich 16x16 Helligkeit in PD-Array "cam-matrix". */
        if (g_menu.cam_changed) {
            g_menu.cam_changed = false;
            camera_set_enabled(g_menu.cam_mode != 0);
        }
        /* periodischer Frame-Capture + cam-* an PD (drosselt sich selbst) */
        camera_update();
        top_screen_set_cam_mode(camera_active() ? g_menu.cam_mode : 0);
        /* TEMPORARY on-screen capture diagnostics (remove later) */
        top_screen_set_cam_debug(camera_active(), camera_frame_serial(),
                                 camera_last_bright());

        /* Camera image is drawn in the top-screen PatchView (not on the bottom
         * screen anymore). Hand top_screen the raw frame only when the camera is
         * active and an image mode is selected; it decides per patch whether to
         * draw it. In "Bild+Matrix" the 16x16 brightness still goes to PD. */
        if (camera_active() && cam_img_active()) {
            const u16 *frame = camera_frame();
            top_screen_set_cam_frame(frame);
            if (g_menu.cam_mode == 3) cam_view_matrix(frame);
        } else {
            top_screen_set_cam_frame(NULL);
        }

        /* Feed the active top-screen patch's .pd path to the canvas renderer
         * (cached: only re-reads the SD file when the patch actually changes). */
        {
            int cslot = top_screen_get_active_slot();
            int cidx  = (cslot >= 0) ? g_menu.patch_idx[cslot] : -1;
            if (cidx >= 0 && cidx < s_patch_list.count) {
                char cpath[PATCH_PATH_MAX + PATCH_NAME_MAX + 2];
                snprintf(cpath, sizeof(cpath), "%s/%s",
                         s_patch_list.entries[cidx].dir,
                         s_patch_list.entries[cidx].file);
                canvas_view_set_patch(cpath);
            } else {
                canvas_view_set_patch(NULL);
            }
        }

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

        /* peaks: read every frame (reading resets them in audio.c) */
        {
            int pl = 0, pr = 0;
            audio_get_pd_peak(audio, &pl, &pr);
            s_peak_l = pl > s_peak_l - PEAK_DECAY ? pl : s_peak_l - PEAK_DECAY;
            s_peak_r = pr > s_peak_r - PEAK_DECAY ? pr : s_peak_r - PEAK_DECAY;
            if (s_peak_l < 0) s_peak_l = 0;
            if (s_peak_r < 0) s_peak_r = 0;
            if (pl > s_peak_hold_l) s_peak_hold_l = pl;
            if (pr > s_peak_hold_r) s_peak_hold_r = pr;
        }
        if (++frame_count >= PEAK_UPDATE_FRAMES) {
            frame_count = 0;
            top_screen_set_peak(s_peak_hold_l, s_peak_hold_r);
            s_peak_hold_l = s_peak_hold_r = 0;
        }
        update_status();

        /* top screen: draw ONLY the active view, the whole screen, every
         * frame (the back buffer is stale after each swap) */
        u16 *top_fb = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        float    bpm      = sync_clock_get_bpm(clk);
        int      dsp_pct  = audio_get_dsp_load(audio);
        unsigned glitches = audio_get_underruns(audio);
        if (g_options_open) {
            options_menu_draw(top_fb, &g_menu, bpm, dsp_pct, glitches);
        } else {
            /* audio view: copy the scope ring only while it is visible */
            int sn = 0;
            if (g_app_page == PAGE_WAVEFORM)
                sn = audio_get_scope(audio, s_scope_l, s_scope_r, AUDIO_SCOPE_FRAMES);
            top_screen_set_scope(s_scope_l, s_scope_r, sn);
            top_screen_draw(top_fb, &g_menu, bpm, dsp_pct, glitches);
        }

        /* bottom screen: pixel mixer ONLY in BOT_MIXER (BOT_CTRL keeps
         * its single-buffered pixel panel untouched) */
        if (g_app_mode == BOT_MIXER) {
            u16 *bot_fb = (u16 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
            mixer_screen_draw(bot_fb, dsp_pct);
        }

        /* swap first, then wait: the swap takes effect at the next VBlank, so
         * after the wait the new back buffer is no longer on screen. (Waiting
         * first and swapping after made the next frame draw into the buffer
         * that was still being displayed -> tearing / flicker.) */
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
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
    camera_deinit();       /* power off camera, free frame buffer */
    motion_input_exit();
    cstick_input_exit();   /* irrstExit only if irrstInit succeeded */
    hidExit();
    gfxExit();

    return 0;
}

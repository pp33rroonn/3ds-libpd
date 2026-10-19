/* button_input.c - button / Circle-Pad routing
 *
 * Global keys (every mode)
 * ------------------------
 *   START  : open / close the OPTIONS overlay (top screen)
 *   ZL/ZR  : evaluated on RELEASE so the ZL+ZR combo can be detected:
 *            - single ZL / ZR: previous / next page of the cycle
 *              (Waveform, Patch 1..5 with empty slots skipped) and the
 *              bottom screen switches to BOT_CTRL. If the PD Log is open,
 *              a single ZL / ZR only closes it (back to the previous page,
 *              no navigation).
 *            - combo (one pressed while the other is held): opens the
 *              PD Log immediately (or closes it if already open); the
 *              following releases are ignored until both are up.
 *            Not sent to PD.
 *   SELECT : app only (not sent to PD): toggles BOT_MIXER <-> BOT_CTRL
 *            while a patch page is shown (with the PD Log open: the page
 *            below it). No-op on the Waveform page.
 *   R      : bang btn-r to PD (both modes); inside OPTIONS R is +10 BPM
 *
 * OPTIONS open
 * ------------
 *   D-pad, L/R and Y go to options_menu_input(); nothing is sent to PD.
 *   BPM, tap tempo and sync mode live in the OPTIONS menu (ui/options_menu.c).
 *
 * BOT_MIXER (see app_state.h)
 *   [receive btn-a|btn-b|btn-x|btn-y]              -> bang
 *   [receive btn-up|btn-down|btn-left|btn-right]   -> bang
 *   [receive btn-l] / [receive btn-r]              -> bang
 *   [receive pad-x] / [receive pad-y]              -> float -1..+1
 *   (accel-x/y/z and gyro-x/y/z: see motion_input.h)
 * BOT_CTRL
 *   D-pad U/D scrolls the slider panel (fetch via
 *   button_input_ctrl_scroll_delta()); A/B/X/Y, L, R and Circle-Pad
 *   still go to PD.
 */

#include "button_input.h"
#include "../app_state.h"

#include <3ds.h>
#include <z_libpd.h>   /* libpd_bang(), libpd_float() */
#include <stdlib.h>

/* Circle-Pad normalisation factor */
#define CPPAD_MAX  156.0f

/* Auto-repeat thresholds (frames at 60 fps) */
#define REPEAT_DELAY   20   /* first repeat after 20 frames */
#define REPEAT_RATE    4    /* then every 4 frames          */

/* Normalise a raw Circle-Pad value to -1..+1; 0 inside the dead zone. */
static inline float cppad_normalized(s16 raw)
{
    if (raw > -CPPAD_DEADZONE && raw < CPPAD_DEADZONE) return 0.0f;
    float v = (float)raw / CPPAD_MAX;
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

struct ButtonInputCtx {
    SyncClock *clk;
    float      bpm;

    /* D-pad auto-repeat in CTRL mode */
    int        repeat_frames;

    /* Last Circle-Pad values sent to PD (to send 0 when the stick returns) */
    float      last_pad_x;
    float      last_pad_y;
};

ButtonInputCtx *button_input_create(SyncClock *clk)
{
    ButtonInputCtx *ctx = calloc(1, sizeof(ButtonInputCtx));
    if (!ctx) return NULL;

    ctx->clk = clk;
    ctx->bpm = clk ? sync_clock_get_bpm(clk) : 120.0f;
    return ctx;
}

/* Circle-Pad -> PD floats (pad-x, pad-y); only on changes >= 0.01 so the
 * PD scheduler is not flooded. */
static void send_pad_to_pd(ButtonInputCtx *ctx, float nx, float ny)
{
    float dx = nx - ctx->last_pad_x;
    float dy = ny - ctx->last_pad_y;
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;

    if (dx >= 0.01f) {
        libpd_float("pad-x", nx);
        ctx->last_pad_x = nx;
    }
    if (dy >= 0.01f) {
        libpd_float("pad-y", ny);
        ctx->last_pad_y = ny;
    }
}

/* Scroll steps for the CTRL panel (fetched by main.c every frame) */
static int s_ctrl_scroll_delta = 0;

/* ZL / ZR combo state: set when a combo fired (or when the pending
 * releases must be ignored, e.g. after OPTIONS); cleared once both
 * buttons are released. */
static bool    s_combo_used  = false;
/* Page shown before the PD Log was opened (restored when it closes) */
static AppPage s_log_prev_page = PAGE_WAVEFORM;

static void toggle_options(ButtonInputCtx *ctx)
{
    g_options_open = !g_options_open;
    if (g_options_open) {
        /* nothing goes to PD while OPTIONS is open: centre the stick */
        send_pad_to_pd(ctx, 0.0f, 0.0f);
        g_menu.repeat_frames = 0;
        g_menu.tap_count     = 0;
    }
    s_ctrl_scroll_delta = 0;
    ctx->repeat_frames  = 0;
    /* ZL / ZR releases that belong to presses before the toggle are
     * ignored (cleared once both buttons are up) */
    s_combo_used = true;
}

/* Bottom-screen mode change (keeps the scroll state clean) */
static void set_mode(ButtonInputCtx *ctx, AppMode mode)
{
    if (g_app_mode == mode) return;
    g_app_mode          = mode;
    s_ctrl_scroll_delta = 0;
    ctx->repeat_frames  = 0;
}

/* ---- ZL / ZR: page navigation and PD Log ---------------------------------- */
static void open_close_log(void)
{
    if (g_app_page == PAGE_PD_LOG) {
        g_app_page = s_log_prev_page;   /* top_screen_draw re-validates it */
    } else {
        s_log_prev_page = g_app_page;
        g_app_page      = PAGE_PD_LOG;
    }
}

static void handle_zl_zr(ButtonInputCtx *ctx, u32 down, u32 held, u32 up)
{
    const u32 both = KEY_ZL | KEY_ZR;

    /* combo: one pressed while the other is held (or both in one frame) */
    if ((down & both) && (held & both) == both) {
        if (!s_combo_used) open_close_log();
        s_combo_used = true;
        return;
    }

    /* single press: act on release, if no combo is in progress */
    if ((up & both) && !(held & both)) {
        if (!s_combo_used) {
            if (g_app_page == PAGE_PD_LOG) {
                open_close_log();   /* close only, no navigation */
            } else {
                g_app_page = (up & KEY_ZL) ? top_screen_prev_page(&g_menu)
                                           : top_screen_next_page(&g_menu);
                set_mode(ctx, BOT_CTRL);   /* navigation always shows CTRL */
            }
        }
    }

    /* both released: the next press starts fresh */
    if (!(held & both)) s_combo_used = false;
}

/* SELECT: BOT_MIXER <-> BOT_CTRL on a patch page; no-op on Waveform */
static void handle_select(ButtonInputCtx *ctx)
{
    AppPage page = (g_app_page == PAGE_PD_LOG) ? s_log_prev_page : g_app_page;
    if (page < PAGE_PATCH_0 || page > PAGE_PATCH_4) return;
    set_mode(ctx, g_app_mode == BOT_CTRL ? BOT_MIXER : BOT_CTRL);
}

int button_input_ctrl_scroll_delta(void)
{
    int d = s_ctrl_scroll_delta;
    s_ctrl_scroll_delta = 0;
    return d;
}

/* L / R -> PD (both modes). ZL/ZR are used for page navigation. */
static void send_shoulder_to_pd(u32 down)
{
    if (down & KEY_L) libpd_bang("btn-l");
    if (down & KEY_R) libpd_bang("btn-r");
}

/* ---- BOT_MIXER: all buttons to PD ----------------------------------------- */
static void poll_mixer_mode(ButtonInputCtx *ctx, u32 down,
                            const circlePosition *cp)
{
    if (down & KEY_A)      libpd_bang("btn-a");
    if (down & KEY_B)      libpd_bang("btn-b");
    if (down & KEY_X)      libpd_bang("btn-x");
    if (down & KEY_Y)      libpd_bang("btn-y");
    if (down & KEY_DUP)    libpd_bang("btn-up");
    if (down & KEY_DDOWN)  libpd_bang("btn-down");
    if (down & KEY_DLEFT)  libpd_bang("btn-left");
    if (down & KEY_DRIGHT) libpd_bang("btn-right");
    send_shoulder_to_pd(down);

    send_pad_to_pd(ctx, cppad_normalized(cp->dx), cppad_normalized(cp->dy));
}

/* ---- BOT_CTRL: D-pad U/D scrolls the panel, rest as BOT_MIXER ------------ */
static void poll_ctrl_mode(ButtonInputCtx *ctx, u32 down, u32 held,
                           const circlePosition *cp)
{
    int delta = 0;
    if (down & KEY_DUP)   delta -= 1;
    if (down & KEY_DDOWN) delta += 1;

    /* auto-repeat while held */
    u32 repeat_keys = KEY_DUP | KEY_DDOWN;
    if ((held & repeat_keys) && !(down & repeat_keys)) {
        ctx->repeat_frames++;
        if (ctx->repeat_frames >= REPEAT_DELAY &&
            (ctx->repeat_frames - REPEAT_DELAY) % REPEAT_RATE == 0) {
            if (held & KEY_DUP)   delta -= 1;
            if (held & KEY_DDOWN) delta += 1;
        }
    } else {
        ctx->repeat_frames = 0;
    }
    s_ctrl_scroll_delta += delta;

    if (down & KEY_A)      libpd_bang("btn-a");
    if (down & KEY_B)      libpd_bang("btn-b");
    if (down & KEY_X)      libpd_bang("btn-x");
    if (down & KEY_Y)      libpd_bang("btn-y");
    send_shoulder_to_pd(down);

    send_pad_to_pd(ctx, cppad_normalized(cp->dx), cppad_normalized(cp->dy));
}

void button_input_poll(ButtonInputCtx *ctx)
{
    if (!ctx) return;

    /* BPM may also come from the patch ([send pd-bpm]) -> follow the clock */
    if (ctx->clk) ctx->bpm = sync_clock_get_bpm(ctx->clk);

    /* hidScanInput() is called ONCE per frame in main.c. */
    u32 down = hidKeysDown();
    u32 held = hidKeysHeld();
    u32 up   = hidKeysUp();
    circlePosition cp;
    hidCircleRead(&cp);

    if (down & KEY_START) {
        toggle_options(ctx);
        return;
    }

    if (g_options_open) {
        options_menu_input(&g_menu, held, down);
        return;
    }

    /* ZL / ZR: page cycle (on release) and ZL+ZR combo -> PD Log */
    handle_zl_zr(ctx, down, held, up);

    /* SELECT belongs to the app (bottom screen), never sent to PD */
    if (down & KEY_SELECT) handle_select(ctx);

    switch (g_app_mode) {
        case BOT_CTRL:  poll_ctrl_mode(ctx, down, held, &cp); break;
        case BOT_MIXER:
        default:        poll_mixer_mode(ctx, down, &cp);      break;
    }
}

float button_input_get_bpm(const ButtonInputCtx *ctx)
{
    return ctx ? ctx->bpm : 120.0f;
}

void button_input_destroy(ButtonInputCtx *ctx)
{
    free(ctx);
}

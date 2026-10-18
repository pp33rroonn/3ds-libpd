/* button_input.c - button / Circle-Pad routing
 *
 * Global keys (every mode)
 * ------------------------
 *   START : open / close the OPTIONS overlay (top screen)
 *   ZL/ZR : previous / next top-screen page (Mixer, Patch 1..5, PD Log);
 *           not sent to PD any more
 *   R     : PATCH <-> CTRL (bottom screen); inside OPTIONS R is +10 BPM
 *
 * OPTIONS open
 * ------------
 *   D-pad, L/R and Y go to options_menu_input(); nothing is sent to PD.
 *   BPM, tap tempo and sync mode live in the OPTIONS menu (ui/options_menu.c).
 *
 * MODE_PATCH (see app_state.h)
 *   [receive btn-a|btn-b|btn-x|btn-y]              -> bang
 *   [receive btn-up|btn-down|btn-left|btn-right]   -> bang
 *   [receive btn-select]                           -> bang
 *   [receive btn-l]                                -> bang
 *   [receive pad-x] / [receive pad-y]              -> float -1..+1
 *   (accel-x/y/z and gyro-x/y/z: see motion_input.h)
 * MODE_CTRL
 *   D-pad U/D scrolls the slider panel (fetch via
 *   button_input_ctrl_scroll_delta()); A/B/X/Y/SELECT, L and Circle-Pad
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
}

void button_input_toggle_ctrl(ButtonInputCtx *ctx)
{
    if (g_options_open) return;   /* R belongs to the OPTIONS menu */
    g_app_mode = (g_app_mode == MODE_CTRL) ? MODE_PATCH : MODE_CTRL;

    s_ctrl_scroll_delta = 0;
    if (ctx) ctx->repeat_frames = 0;
}

int button_input_ctrl_scroll_delta(void)
{
    int d = s_ctrl_scroll_delta;
    s_ctrl_scroll_delta = 0;
    return d;
}

/* L -> PD (PATCH and CTRL). ZL/ZR are used for page navigation. */
static void send_shoulder_to_pd(u32 down)
{
    if (down & KEY_L) libpd_bang("btn-l");
}

/* ---- MODE_PATCH: all buttons to PD ---------------------------------------- */
static void poll_patch_mode(ButtonInputCtx *ctx, u32 down,
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
    if (down & KEY_SELECT) libpd_bang("btn-select");
    send_shoulder_to_pd(down);

    send_pad_to_pd(ctx, cppad_normalized(cp->dx), cppad_normalized(cp->dy));
}

/* ---- MODE_CTRL: D-pad U/D scrolls the panel, rest as PATCH --------------- */
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
    if (down & KEY_SELECT) libpd_bang("btn-select");
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

    /* ZL / ZR: top-screen page navigation (PD Log always reachable) */
    if (down & KEY_ZL) g_app_page = top_screen_prev_page(&g_menu);
    if (down & KEY_ZR) g_app_page = top_screen_next_page(&g_menu);

    if (down & KEY_R) {
        button_input_toggle_ctrl(ctx);
        return;
    }

    switch (g_app_mode) {
        case MODE_CTRL:  poll_ctrl_mode(ctx, down, held, &cp); break;
        case MODE_PATCH:
        default:         poll_patch_mode(ctx, down, &cp);      break;
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

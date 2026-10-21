/* camera.c - built-in camera as a CONTROL SOURCE for Pure Data.
 *
 * See camera.h. The outer camera (SELECT_OUT1) is captured at QQVGA
 * (160x120) in RGB565. Capture is driven entirely from the main thread via
 * camera_update(): a small state machine starts a non-blocking receive at
 * most every CAM_THROTTLE_FRAMES frames (~10 Hz at 60 fps), then polls for
 * completion. When a frame is ready the mean colour is computed on a
 * subsampled grid and sent to PD with libpd_float(), just like "mic-level".
 *
 * CAMU API verified against /opt/devkitpro/libctru/include/3ds/services/cam.h.
 */

#include "camera.h"

#include <3ds.h>
#include <z_libpd.h>   /* libpd_float (GLOBAL control messages) */

#include <stdlib.h>
#include <string.h>
#include <malloc.h>    /* memalign (camera DMA buffer, 0x1000-aligned) */
#include <stdio.h>

/* Low resolution: QQVGA 160x120, RGB565 (2 bytes/pixel). */
#define CAM_W            160
#define CAM_H            120
#define CAM_BYTES        (CAM_W * CAM_H * 2)

/* Capture rate throttle: start a new frame roughly every 6 main-loop frames
 * (about 10 Hz at 60 fps). CPU-friendly; far below the audio rate. */
#define CAM_THROTTLE_FRAMES 6

/* Pixel subsampling for the mean: every 4th pixel in x and y (40x30 = 1200
 * samples) is plenty for an average and keeps the per-frame cost tiny. */
#define CAM_STEP 4

static bool   s_enabled   = false;   /* caller wants the camera on          */
static bool   s_active    = false;   /* hardware is powered + capturing     */
static bool   s_receiving = false;   /* a transfer is in flight             */
static u16   *s_buf       = NULL;    /* DMA target: in-flight QQVGA frame   */
static u16   *s_display   = NULL;    /* last COMPLETE frame shown in the UI */
static u32    s_transfer  = 0;       /* transfer unit from CAMU_GetMaxBytes */
static Handle s_event     = 0;       /* transfer-end event                  */
static int    s_frame     = 0;       /* throttle counter                    */
static unsigned s_serial  = 0;       /* ++ on every newly received frame    */
static float  s_bright    = 0.0f;    /* last computed brightness (0..1)     */
static Handle s_err_event = 0;       /* buffer error interrupt event (PORT_CAM1) */
static unsigned s_err_count  = 0;    /* buffer error interrupts (diagnostics)   */
static unsigned s_err_streak = 0;    /* consecutive errors since last good frame */
static u32    s_source    = SELECT_OUT1; /* active camera: SELECT_OUT1 (back) / SELECT_IN1 (front) */
static unsigned s_starve  = 0;       /* updates with a receive armed but no event firing */
static int    s_soft_recover = 0;    /* 1 after a light starvation recovery (escalate next) */

/* After this many consecutive buffer errors without a completed frame, do a
 * full capture restart as a last resort (the normal recovery is a cheap
 * ClearBuffer + re-arm). ~1 s of continuous failure at 60 fps. */
#define CAM_ERR_RESYNC_STREAK 60

/* Event-starvation watchdog: if a receive is armed but NEITHER the receive nor
 * the buffer-error event fires for this many consecutive updates, the transfer
 * is wedged. This is the observed "freeze with err=0" case: the receive event
 * silently stops firing and no error interrupt ever comes, so without this
 * watchdog the poll loop would wait forever and never re-arm. ~0.75 s at 60 fps,
 * far above the ~4-update frame time at FRAME_RATE_15, so a healthy capture
 * never trips it. */
#define CAM_STARVE_LIMIT 45

/* ---- hardware bring-up / teardown ----------------------------------------- */

static bool camera_power_on(void)
{
    if (R_FAILED(camInit())) {
        fprintf(stderr, "[camera] camInit failed\n");
        return false;
    }

    /* Always exactly one camera (never stereo / PORT_BOTH): s_source is either
     * SELECT_OUT1 (back, default) or SELECT_IN1 (front), both on PORT_CAM1. */
    Result rc = 0;
    rc |= CAMU_SetSize(s_source, SIZE_QQVGA, CONTEXT_A);
    rc |= CAMU_SetOutputFormat(s_source, OUTPUT_RGB_565, CONTEXT_A);
    rc |= CAMU_SetFrameRate(s_source, FRAME_RATE_15);
    rc |= CAMU_SetNoiseFilter(s_source, true);
    rc |= CAMU_SetAutoExposure(s_source, true);
    rc |= CAMU_SetAutoWhiteBalance(s_source, true);
    rc |= CAMU_SetTrimming(PORT_CAM1, false);
    if (R_FAILED(rc)) {
        fprintf(stderr, "[camera] configuration failed\n");
        camExit();
        return false;
    }

    /* Transfer unit for the DMA (bytes per chunk). Use the LINES API so the
     * chunk is a whole number of image rows and tiles ALL CAM_H lines exactly:
     * CAMU_SetReceiving's transferUnit is an s16, so a byte count >32767 wraps
     * negative -> partial DMA -> the bottom rows are never written (the dark
     * block at the bottom of the frame). Pick a line count that (a) stays well
     * inside s16, and (b) divides CAM_H evenly so no leftover rows are dropped. */
    s16 max_lines = 0;
    if (R_FAILED(CAMU_GetMaxLines(&max_lines, CAM_W, CAM_H)) || max_lines <= 0)
        max_lines = CAM_H;
    const int line_bytes = CAM_W * 2;                 /* 320 bytes per row */
    int cap_lines = 32000 / line_bytes;               /* keep chunk inside s16 */
    int lines = max_lines < cap_lines ? max_lines : cap_lines;
    if (lines > CAM_H) lines = CAM_H;
    if (lines < 1)     lines = 1;
    while (lines > 1 && (CAM_H % lines) != 0) lines--; /* exact tiling of CAM_H */
    s_transfer = (u32)lines * (u32)line_bytes;        /* bytes per transfer chunk */
    if (R_FAILED(CAMU_SetTransferLines(PORT_CAM1, (s16)lines, CAM_W, CAM_H))) {
        fprintf(stderr, "[camera] transfer setup failed\n");
        camExit();
        return false;
    }

    if (R_FAILED(CAMU_Activate(s_source))) {
        fprintf(stderr, "[camera] activate failed\n");
        camExit();
        return false;
    }

    CAMU_GetBufferErrorInterruptEvent(&s_err_event, PORT_CAM1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);

    s_receiving    = false;
    s_err_streak   = 0;
    s_starve       = 0;
    s_soft_recover = 0;
    s_frame        = 0;
    return true;
}

static void camera_power_off(void)
{
    if (s_event) {
        svcCloseHandle(s_event);
        s_event = 0;
    }
    if (s_err_event) {
        svcCloseHandle(s_err_event);
        s_err_event = 0;
    }
    s_receiving    = false;
    s_err_streak   = 0;
    s_starve       = 0;
    s_soft_recover = 0;

    if (s_active) {
        CAMU_StopCapture(PORT_CAM1);
        CAMU_Activate(SELECT_NONE);
        camExit();
    }
    s_active = false;
}

/* ---- public API ----------------------------------------------------------- */

void camera_init(void)
{
    if (!s_buf) {
        /* DMA target: 0x1000-aligned, like the mic buffer in audio.c */
        s_buf = (u16 *)memalign(0x1000, CAM_BYTES);
        if (!s_buf)
            fprintf(stderr, "[camera] memalign failed\n");
    }
    if (!s_display) {
        /* Last COMPLETE frame handed to the UI. Separate from the DMA target so
         * the UI never sees a half-transferred / overflowed frame. Start black. */
        s_display = (u16 *)memalign(0x1000, CAM_BYTES);
        if (s_display)
            memset(s_display, 0, CAM_BYTES);
        else
            fprintf(stderr, "[camera] memalign (display) failed\n");
    }
    s_enabled    = false;
    s_active     = false;
    s_receiving  = false;
    s_event      = 0;
    s_err_event  = 0;
    s_err_count    = 0;
    s_err_streak   = 0;
    s_starve       = 0;
    s_soft_recover = 0;
    s_frame        = 0;
}

void camera_set_enabled(bool on)
{
    if (on == s_enabled) return;
    s_enabled = on;

    if (on) {
        if (!s_buf) return;               /* no buffer -> stay off */
        s_active = camera_power_on();     /* failure leaves us inactive */
    } else {
        camera_power_off();
    }
}

bool camera_active(void)
{
    return s_active;
}

/* Select the back (SELECT_OUT1, default) or front (SELECT_IN1) camera. Always
 * exactly one camera on PORT_CAM1 - never stereo. If the camera is currently
 * powered, cycle it off/on so the new source takes effect immediately. */
void camera_set_source(bool front)
{
    u32 want = front ? SELECT_IN1 : SELECT_OUT1;
    if (want == s_source) return;
    s_source = want;
    if (s_active) {
        camera_power_off();
        s_active = camera_power_on();
    }
}

/* Compute the mean colour over a subsampled grid and send to PD. */
static void camera_send_means(void)
{
    unsigned long sr = 0, sg = 0, sb = 0;
    unsigned long count = 0;

    for (int y = 0; y < CAM_H; y += CAM_STEP) {
        const u16 *row = s_buf + (size_t)y * CAM_W;
        for (int x = 0; x < CAM_W; x += CAM_STEP) {
            u16 px = row[x];               /* little-endian RGB565 */
            sr += (px >> 11) & 0x1F;       /* 5 bits red   */
            sg += (px >> 5)  & 0x3F;       /* 6 bits green */
            sb +=  px        & 0x1F;       /* 5 bits blue  */
            count++;
        }
    }
    if (count == 0) return;

    float r = (float)sr / ((float)count * 31.0f);
    float g = (float)sg / ((float)count * 63.0f);
    float b = (float)sb / ((float)count * 31.0f);

    /* perceptual luma for overall brightness (Rec. 601) */
    float bright = 0.299f * r + 0.587f * g + 0.114f * b;
    s_bright = bright;

    libpd_float("cam-bright", bright);
    libpd_float("cam-r", r);
    libpd_float("cam-g", g);
    libpd_float("cam-b", b);
}

/* Drive capture from the main thread without ever blocking. Capture runs
 * continuously (CAMU_StartCapture was issued once in camera_power_on); here we
 * only (a) keep exactly one receive armed, (b) publish a frame to the UI ONLY
 * when a transfer completes in full, and (c) recover cheaply from a buffer
 * overflow with CAMU_ClearBuffer + re-arm instead of tearing the whole capture
 * pipeline down and restarting it (which, polled at 60 fps, aborted every
 * in-flight frame and made the error counter explode while no frame ever
 * completed). Priority order matters: a completed receive is handled BEFORE a
 * pending error so a good frame is never discarded in favour of an overflow
 * that happened just afterwards.
 *
 * Mirrors the devkitPro camera/video example, adapted to a non-blocking poll
 * (that example blocks for up to a second per frame, which would stall audio
 * and input here). */
void camera_update(void)
{
    if (!s_active || !s_buf) return;

    /* (a) Keep one receive queued at all times. */
    if (!s_receiving) {
        if (s_event) { svcCloseHandle(s_event); s_event = 0; }
        if (R_SUCCEEDED(CAMU_SetReceiving(&s_event, s_buf, PORT_CAM1,
                                          CAM_BYTES, (s16)s_transfer))) {
            s_receiving = true;
            s_starve    = 0;   /* fresh receive armed: restart the watchdog */
        }
        return;   /* give the transfer a frame-time before polling it */
    }

    /* (b) Receive complete? (checked first, 0 ns = non-blocking poll). */
    if (s_event && R_SUCCEEDED(svcWaitSynchronization(s_event, 0))) {
        svcCloseHandle(s_event); s_event = 0;
        s_receiving    = false;
        s_err_streak   = 0;
        s_starve       = 0;   /* good frame: watchdog + recovery escalation reset */
        s_soft_recover = 0;

        /* The frame was written to s_buf by DMA; invalidate the CPU data cache
         * so we read fresh pixels, then publish the COMPLETE frame to the UI. */
        GSPGPU_InvalidateDataCache(s_buf, CAM_BYTES);
        if (s_display) memcpy(s_display, s_buf, CAM_BYTES);
        s_serial++;

        /* Swallow any error that fired alongside this good frame so it does not
         * trigger a needless recovery on the next update. */
        if (s_err_event) svcWaitSynchronization(s_err_event, 0);

        /* throttle only the mean computation / PD messages (~10 Hz at 60 fps) */
        if (++s_frame >= CAM_THROTTLE_FRAMES) {
            s_frame = 0;
            camera_send_means();
        }

        /* IMMEDIATE RE-ARM: queue the next receive right here instead of waiting
         * for branch (a) on the following update. This closes the one-update gap
         * where no transfer is armed - the window in which the receive event
         * could silently stop firing and leave capture starved. */
        if (R_SUCCEEDED(CAMU_SetReceiving(&s_event, s_buf, PORT_CAM1,
                                          CAM_BYTES, (s16)s_transfer))) {
            s_receiving = true;
            s_starve    = 0;
        }
        return;
    }

    /* (c) Buffer error / overflow: minimal recovery. Drop the in-flight
     * transfer, clear the camera FIFO and re-arm on the next update. Do NOT
     * stop/restart capture per error - only after a long streak as a resync. */
    if (s_err_event && R_SUCCEEDED(svcWaitSynchronization(s_err_event, 0))) {
        s_err_count++;
        s_err_streak++;
        s_starve = 0;   /* an event DID fire: this is not starvation */
        if (s_event) { svcCloseHandle(s_event); s_event = 0; }
        s_receiving = false;
        CAMU_ClearBuffer(PORT_CAM1);
        if (s_err_streak >= CAM_ERR_RESYNC_STREAK) {
            /* Pipeline looks wedged: hard resync once, then keep going. */
            CAMU_StopCapture(PORT_CAM1);
            CAMU_ClearBuffer(PORT_CAM1);
            CAMU_StartCapture(PORT_CAM1);
            s_err_streak = 0;
        }
        return;
    }

    /* (d) EVENT-STARVATION WATCHDOG. A receive is armed but NEITHER the receive
     * nor the error event fired this update. This is the observed freeze with
     * err=0: the receive event silently stops, no error interrupt ever comes,
     * and (a)/(b)/(c) above can never run again - so the capture would hang on
     * the last frame forever. Error-driven recovery cannot catch this (there is
     * no error). Once the silence lasts CAM_STARVE_LIMIT updates, force a
     * recovery regardless of any error:
     *   - first time: drop the stale receive + ClearBuffer, then re-arm (light);
     *   - if starvation recurs after that: one hard Stop/Clear/Start resync.
     * The watchdog resets on every successful receive and on this recovery, so a
     * healthy capture never trips it. Non-blocking throughout (no waits). */
    if (++s_starve >= CAM_STARVE_LIMIT) {
        if (s_event) { svcCloseHandle(s_event); s_event = 0; }
        s_receiving = false;
        s_starve    = 0;
        if (!s_soft_recover) {
            CAMU_ClearBuffer(PORT_CAM1);
            s_soft_recover = 1;
        } else {
            CAMU_StopCapture(PORT_CAM1);
            CAMU_ClearBuffer(PORT_CAM1);
            CAMU_StartCapture(PORT_CAM1);
            s_soft_recover = 0;
        }
        return;   /* branch (a) re-arms on the next update */
    }

    /* Nothing ready yet - hold the last complete frame. */
}

const u16 *camera_frame(void)
{
    return s_display;   /* always the last COMPLETE frame (never a partial one) */
}

unsigned camera_frame_serial(void)
{
    return s_serial;
}

float camera_last_bright(void)
{
    return s_bright;
}

/* TEMPORARY diagnostics: number of buffer error interrupts seen. */
unsigned camera_err_count(void)
{
    return s_err_count;
}

void camera_deinit(void)
{
    camera_power_off();
    if (s_buf) {
        free(s_buf);
        s_buf = NULL;
    }
    if (s_display) {
        free(s_display);
        s_display = NULL;
    }
    s_enabled = false;
}

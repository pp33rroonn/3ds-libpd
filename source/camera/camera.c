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
static u16   *s_buf       = NULL;    /* QQVGA RGB565 frame buffer           */
static u32    s_transfer  = 0;       /* transfer unit from CAMU_GetMaxBytes */
static Handle s_event     = 0;       /* transfer-end event                  */
static int    s_frame     = 0;       /* throttle counter                    */
static unsigned s_serial  = 0;       /* ++ on every newly received frame    */
static float  s_bright    = 0.0f;    /* last computed brightness (0..1)     */
static Handle s_err_event = 0;       /* buffer error interrupt event (PORT_CAM1) */
static bool   s_capture_interrupted = false;  /* restart capture next update */
static unsigned s_err_count = 0;     /* buffer error interrupts (diagnostics)   */

/* ---- hardware bring-up / teardown ----------------------------------------- */

static bool camera_power_on(void)
{
    if (R_FAILED(camInit())) {
        fprintf(stderr, "[camera] camInit failed\n");
        return false;
    }

    Result rc = 0;
    rc |= CAMU_SetSize(SELECT_OUT1, SIZE_QQVGA, CONTEXT_A);
    rc |= CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
    rc |= CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_15);
    rc |= CAMU_SetNoiseFilter(SELECT_OUT1, true);
    rc |= CAMU_SetAutoExposure(SELECT_OUT1, true);
    rc |= CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
    rc |= CAMU_SetTrimming(PORT_CAM1, false);
    if (R_FAILED(rc)) {
        fprintf(stderr, "[camera] configuration failed\n");
        camExit();
        return false;
    }

    /* transfer unit (per-chunk byte count) for the chosen geometry */
    if (R_FAILED(CAMU_GetMaxBytes(&s_transfer, CAM_W, CAM_H)) ||
        R_FAILED(CAMU_SetTransferBytes(PORT_CAM1, s_transfer, CAM_W, CAM_H))) {
        fprintf(stderr, "[camera] transfer setup failed\n");
        camExit();
        return false;
    }

    if (R_FAILED(CAMU_Activate(SELECT_OUT1))) {
        fprintf(stderr, "[camera] activate failed\n");
        camExit();
        return false;
    }

    CAMU_GetBufferErrorInterruptEvent(&s_err_event, PORT_CAM1);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);

    s_receiving = false;
    s_capture_interrupted = false;
    s_frame     = 0;
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
    s_receiving = false;
    s_capture_interrupted = false;

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
    s_enabled   = false;
    s_active    = false;
    s_receiving = false;
    s_event     = 0;
    s_err_event = 0;
    s_capture_interrupted = false;
    s_err_count = 0;
    s_frame     = 0;
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

void camera_update(void)
{
    if (!s_active || !s_buf) return;

    /* Always keep one receive queued (the camera delivers 15 fps; we must not
     * throttle the receive side or the camera's internal queue overflows ->
     * buffer error interrupt). Only camera_send_means() is throttled below.
     * Mirrors the devkitPro camera/video example. */
    if (!s_receiving) {
        if (s_event) { svcCloseHandle(s_event); s_event = 0; }
        if (R_SUCCEEDED(CAMU_SetReceiving(&s_event, s_buf, PORT_CAM1,
                                          CAM_BYTES, (s16)s_transfer)))
            s_receiving = true;
        if (s_capture_interrupted) {
            CAMU_StartCapture(PORT_CAM1);   /* resume after a buffer error */
            s_capture_interrupted = false;
        }
        return;
    }

    /* Wait (non-blocking) on {buffer error, receive done}. Timeout = normal. */
    Handle events[2] = { s_err_event, s_event };
    s32 index = -1;
    if (R_FAILED(svcWaitSynchronizationN(&index, events, 2, false, 0)))
        return;   /* nothing ready yet */

    if (index == 0) {   /* buffer error / overflow: drop the frame, re-arm */
        if (s_event) { svcCloseHandle(s_event); s_event = 0; }
        s_receiving = false;
        s_capture_interrupted = true;
        s_err_count++;
        return;
    }

    /* index == 1: receive complete */
    if (s_event) { svcCloseHandle(s_event); s_event = 0; }
    s_receiving = false;
    s_serial++;                 /* new frame available for camera_frame() */

    /* The frame was written to s_buf by GPU DMA; invalidate the CPU data
     * cache for the buffer so we read the fresh pixels, not stale cache. */
    GSPGPU_InvalidateDataCache(s_buf, CAM_BYTES);

    /* throttle only the mean computation / PD messages (~10 Hz at 60 fps) */
    if (++s_frame >= CAM_THROTTLE_FRAMES) {
        s_frame = 0;
        camera_send_means();
    }
}
const u16 *camera_frame(void)
{
    return s_buf;
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
    s_enabled = false;
}

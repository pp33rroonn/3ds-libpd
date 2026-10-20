#ifndef CAMERA_H
#define CAMERA_H
/* camera.h - built-in camera as a CONTROL SOURCE (no image / no video shown)
 *
 * The outer camera is captured periodically at a low resolution (QQVGA
 * 160x120, RGB565) and a low rate (throttled in the main loop, ~10 Hz),
 * NOT per audio buffer. From each frame the mean colour is computed on a
 * subsampled grid and sent to Pure Data as GLOBAL control messages, exactly
 * like the microphone's "mic-level":
 *
 *   cam-bright : overall brightness 0..1
 *   cam-r      : red mean           0..1
 *   cam-g      : green mean         0..1
 *   cam-b      : blue mean          0..1
 *
 * Mirrors the microphone integration (audio_set_mic_enabled / audio_mic_active).
 */

#include <3ds.h>        /* u16 */
#include <stdbool.h>

/* Raw frame geometry (QQVGA, RGB565). Exposed so the bottom-screen camera
 * view can size its render without pulling in camera.c internals. */
#define CAM_FRAME_W 160
#define CAM_FRAME_H 120

/* Allocate the frame buffer and reset state. Does NOT power the camera on
 * (that happens on the first camera_set_enabled(true)). Call once at start. */
void camera_init(void);

/* Power the camera hardware on/off. Safe to call repeatedly with the same
 * value. When turned off, capture stops and no cam-* messages are sent. */
void camera_set_enabled(bool on);

/* True while the camera hardware is powered and capturing. */
bool camera_active(void);

/* Call once per frame from the main loop. Throttles the capture itself and,
 * when a frame has arrived, computes the means and sends the cam-* messages
 * to PD. Returns at once when the camera is not active. */
void camera_update(void);

/* Pointer to the most recent raw QQVGA RGB565 frame (CAM_FRAME_W x
 * CAM_FRAME_H), or NULL if the buffer was never allocated. The contents are
 * only meaningful while the camera is active; read it right after
 * camera_update(). Do NOT write through this pointer. */
const u16 *camera_frame(void);

/* Monotonic counter incremented once per newly received frame. A consumer can
 * store the last value it rendered and re-render only when it changes, so the
 * 160x120 dither runs at most at the capture rate (~10 Hz), never per frame. */
unsigned camera_frame_serial(void);

/* Stop capture, power the camera off and free the buffer. Call at shutdown. */
void camera_deinit(void);

#endif /* CAMERA_H */

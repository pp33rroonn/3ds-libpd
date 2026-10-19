#pragma once
/* mixer_screen.h - pixel mixer on the bottom screen (MODE_PATCH only)
 *
 * Bottom screen: 320x240, RGB565, same memory layout as the top screen:
 *   fb[x * 240 + (239 - y)] = color
 * The bottom framebuffer must be in GSP_RGB565_OES format.
 *
 * Layout:
 *   y   0..14  header  "MIXER"            "DSP:XX%"
 *   y  15..214 5 strips of 40 px (slot i at y = 15 + i * 40)
 *              top 18 px: "N: name", bottom 20 px: fader + vol %
 *   y 215..239 footer (help)
 */

#include <3ds.h>
#include <stdbool.h>

#define MIXER_W 320
#define MIXER_H 240

/* Full redraw. active_slot = slot shown on the top screen (-1 = none),
 * peak_l / peak_r = master output peaks 0..32767. */
void mixer_screen_draw(u16 *fb, int active_slot, int dsp_pct,
                       int peak_l, int peak_r);

/* Touch handling (call every frame in MODE_PATCH). x/y = touch position,
 * touching = KEY_TOUCH held. A touch that starts on the fader of a loaded
 * slot grabs it until release and sets its volume.
 * Returns true if the touch was consumed (do not forward it to PD). */
bool mixer_screen_handle_touch(int x, int y, bool touching);

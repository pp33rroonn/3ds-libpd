#pragma once
/* scope_view.h - audio visualisation for the top screen (Waveform page)
 *
 * Views, selected in OPTIONS -> SpecAn (OptionsMenu.specan_mode):
 *   0 Waveform : triggered oscilloscope of the mono mix (L+R)/2
 *   1 Envelope : scrolling peak history, L upwards / R downwards
 *   2 Stereo   : goniometer (mid vertical, side horizontal) + correlation
 *   3 Level    : L/R RMS bars in dB (-60..0) with peak-hold marker
 *   4..11      : text-mode views, see scope_ascii.h (ASCII Wave, ASCII Rain,
 *                ASCII Scope, Braille Scope, Phosphor, Stereo Dots,
 *                Stereo Glow, Stereo Field)
 *
 * Input is the newest block of PD output frames (oldest first), e.g. from
 * audio_get_scope(). All drawing is clipped to the given rectangle.
 */

#include <3ds.h>
#include <stdint.h>

typedef enum {
    SCOPE_WAVEFORM = 0,
    SCOPE_ENVELOPE,
    SCOPE_STEREO,
    SCOPE_LEVEL,
    SCOPE_ASCII_WAVE,
    SCOPE_ASCII_RAIN,
    SCOPE_ASCII_SCOPE,
    SCOPE_ASCII_BRAILLE,
    SCOPE_ASCII_PHOSPHOR,
    SCOPE_ASCII_STEREO_DOTS,
    SCOPE_ASCII_STEREO_GLOW,
    SCOPE_ASCII_STEREO_FIELD,
    SCOPE_MODE_COUNT
} ScopeMode;

/* Frames that make up one video frame (44100 / 60). Envelope and Level
 * analyse only this newest part of the block. */
#define SCOPE_FRAME_SAMPLES 735

/* Draw the view into the rectangle (x, y, w, h) of the top framebuffer.
 * n = number of frames in l / r. Call once per video frame and ONLY while
 * the view is visible (Envelope / Level keep history across calls). */
void scope_view_draw(u16 *fb, int x, int y, int w, int h, int mode,
                     const int16_t *l, const int16_t *r, int n);

/* Forget Envelope history and Level peak-hold (e.g. after a patch change). */
void scope_view_reset(void);

#pragma once
/* top_screen.h - page dispatcher for the pixel top screen
 *
 * Pages: Mixer, Patch 1..5 (only slots that hold a patch), PD Log.
 * ZL / ZR step backwards / forwards (wrapping, empty slots skipped,
 * PD Log always reachable). The OPTIONS overlay replaces the page while
 * g_options_open is set (see options_menu.h).
 */

#include <3ds.h>
#include "options_menu.h"

typedef enum {
    PAGE_MIXER = 0,
    PAGE_PATCH_0,
    PAGE_PATCH_1,
    PAGE_PATCH_2,
    PAGE_PATCH_3,
    PAGE_PATCH_4,
    PAGE_PD_LOG,
    PAGE_COUNT
} AppPage;

extern AppPage g_app_page;

/* Draw the active page (whole screen). */
void top_screen_draw(u16 *fb, const OptionsMenu *m, float bpm,
                     int dsp_pct, unsigned glitches);

AppPage top_screen_next_page(const OptionsMenu *m);
AppPage top_screen_prev_page(const OptionsMenu *m);

/* Optional status/warning line on the Mixer page ("" or NULL = none). */
void top_screen_set_status(const char *msg);

/* PD output peak levels (0..32767) for the Mixer page. */
void top_screen_set_peak(int l, int r);

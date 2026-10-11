/* touch_input.c – Touchscreen → libpd message bridge */

#include "touch_input.h"
#include <3ds.h>
#include <z_libpd.h>   /* libpd_float(), libpd_bang() */

static bool  s_was_touched = false;
static int   s_last_x      = -1;
static int   s_last_y      = -1;

void touch_input_poll(bool forward)
{
    /* hidScanInput() wird EINMAL pro Frame in main.c aufgerufen.
     * Hier nur die bereits aktualisierten Zustaende lesen. */
    u32 held    = hidKeysHeld();
    touchPosition tp;

    bool touching = forward && (held & KEY_TOUCH) != 0;

    if (touching) {
        hidTouchRead(&tp);
        int x = (int)tp.px;
        int y = (int)tp.py;

        if (!s_was_touched) {
            libpd_bang("touch-on");
        }
        if (x != s_last_x) {
            libpd_float("touch-x", (float)x);
            s_last_x = x;
        }
        if (y != s_last_y) {
            libpd_float("touch-y", (float)y);
            s_last_y = y;
        }
        s_was_touched = true;
    } else if (s_was_touched) {
        libpd_bang("touch-off");
        s_was_touched = false;
        s_last_x = s_last_y = -1;
    }
}

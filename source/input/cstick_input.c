/* cstick_input.c - C-Stick (New 3DS) -> libpd
 *
 * libctru: hidInit() ruft auf dem New 3DS selbst schon irrstInit() auf
 * (hidShouldUseIrrst). Unser eigener irrstInit()-Aufruf erhoeht nur den
 * Referenzzaehler; er macht die Abhaengigkeit explizit und meldet uns,
 * ob ir:rst verfuegbar ist. hidScanInput() ruft irrstScanInput() auf,
 * irrstCstickRead() liest danach nur die zwischengespeicherte Position.
 *
 * Rohwerte: circlePosition s16 dx/dy, laut 3dbrew (IRRST Shared Memory)
 * Bereich -0x9C..0x9C (= +-156), Mitte 0. dx rechts positiv, dy oben positiv.
 */

#include "cstick_input.h"

#include <3ds.h>
#include "pd_bridge.h"   /* pd_input_float/bang: routed to the active slot */

#ifndef CSTICK_MAX
#  define CSTICK_MAX       156.0f  /* 0x9C laut 3dbrew; Werte darueber werden geclampt */
#endif
#ifndef CSTICK_DEADZONE
#  define CSTICK_DEADZONE  16      /* Rohwert, ca. 0.10 */
#endif
#define CSTICK_THRESHOLD   0.01f

static bool  s_on        = false;
static bool  s_sent_once = false;
static float s_last_x    = 0.0f;
static float s_last_y    = 0.0f;

static inline float cstick_normalized(s16 raw)
{
    if (raw > -CSTICK_DEADZONE && raw < CSTICK_DEADZONE) return 0.0f;
    float v = (float)raw / CSTICK_MAX;
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;
    return v;
}

static void send_if_changed(const char *name, float v, float *last)
{
    float d = v - *last;
    if (d < 0.0f) d = -d;
    if (!s_sent_once || d >= CSTICK_THRESHOLD) {
        pd_input_float(name, v);
        *last = v;
    }
}

void cstick_input_init(void)
{
    s_on = R_SUCCEEDED(irrstInit());
    s_sent_once = false;
    s_last_x = s_last_y = 0.0f;
}

void cstick_input_poll(bool forward)
{
    if (!forward || !s_on) return;

    circlePosition p;
    irrstCstickRead(&p);
    send_if_changed("cstick-x", cstick_normalized(p.dx), &s_last_x);
    send_if_changed("cstick-y", cstick_normalized(p.dy), &s_last_y);
    s_sent_once = true;
}

void cstick_input_resend(void)
{
    s_sent_once = false;
}

void cstick_input_exit(void)
{
    if (s_on) irrstExit();
    s_on = false;
}

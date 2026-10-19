/* motion_input.c - Beschleunigungssensor + Gyroskop -> libpd
 *
 * Rohwerte (libctru hid.h):
 *   hidAccelRead(accelVector*)  s16 x/y/z, keine offizielle Einheit.
 *     ACCEL_1G = Rohwert fuer ca. 1 g (Schaetzwert, per Makefile
 *     -DACCEL_1G=... anpassbar, wenn 1.0 bei flach liegendem Geraet nicht passt).
 *   hidGyroRead(angularRate*)   s16 x(Roll)/y(Pitch)/z(Yaw).
 *     Grad/s = roh / Koeffizient (HIDUSER_GetGyroscopeRawToDpsCoefficient).
 *
 * Gesendet wird nur bei Aenderung >= MOTION_THRESHOLD (PD-Scheduler nicht fluten).
 */

#include "motion_input.h"

#include <3ds.h>
#include "pd_bridge.h"   /* pd_input_float/bang: routed to the active slot */

#ifndef ACCEL_1G
#  define ACCEL_1G        512.0f   /* Schaetzwert, siehe oben */
#endif
#ifndef GYRO_FULL_DPS
#  define GYRO_FULL_DPS   360.0f   /* 1.0 = 360 Grad/s */
#endif
#define GYRO_DEFAULT_COEFF 14.375f /* Fallback, falls Abfrage fehlschlaegt */
#define MOTION_THRESHOLD   0.02f
#define GYRO_DEADZONE      0.02f   /* Rauschen in Ruhe unterdruecken */

static bool  s_accel_on  = false;
static bool  s_gyro_on   = false;
static float s_gyro_coef = GYRO_DEFAULT_COEFF;

/* Letzte gesendete Werte: [0..2] accel x/y/z, [3..5] gyro x/y/z */
static float s_last[6];
static bool  s_sent_once = false;

static const char *const NAMES[6] = {
    "accel-x", "accel-y", "accel-z", "gyro-x", "gyro-y", "gyro-z"
};

static inline float clamp1(float v)
{
    if (v >  1.0f) return  1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

static void send_if_changed(int i, float v)
{
    float d = v - s_last[i];
    if (d < 0.0f) d = -d;
    if (!s_sent_once || d >= MOTION_THRESHOLD) {
        pd_input_float(NAMES[i], v);
        s_last[i] = v;
    }
}

void motion_input_init(void)
{
    s_accel_on = R_SUCCEEDED(HIDUSER_EnableAccelerometer());
    s_gyro_on  = R_SUCCEEDED(HIDUSER_EnableGyroscope());

    float c = 0.0f;
    if (s_gyro_on && R_SUCCEEDED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&c))
        && c > 0.0f) {
        s_gyro_coef = c;
    }
    s_sent_once = false;
}

void motion_input_poll(bool forward)
{
    if (!forward) return;

    if (s_accel_on) {
        accelVector a;
        hidAccelRead(&a);
        /* x invertiert: rechts neigen = positiv, wie pad-x (auf Hardware geprueft) */
        send_if_changed(0, clamp1(-(float)a.x / ACCEL_1G));
        send_if_changed(1, clamp1((float)a.y / ACCEL_1G));
        send_if_changed(2, clamp1((float)a.z / ACCEL_1G));
    }

    if (s_gyro_on) {
        angularRate g;
        hidGyroRead(&g);
        float scale = 1.0f / (s_gyro_coef * GYRO_FULL_DPS);
        float v[3] = { (float)g.x * scale, (float)g.y * scale, (float)g.z * scale };
        for (int i = 0; i < 3; i++) {
            float x = clamp1(v[i]);
            if (x > -GYRO_DEADZONE && x < GYRO_DEADZONE) x = 0.0f;
            send_if_changed(3 + i, x);
        }
    }

    s_sent_once = true;
}

void motion_input_resend(void)
{
    s_sent_once = false;
}

void motion_input_exit(void)
{
    if (s_gyro_on)  HIDUSER_DisableGyroscope();
    if (s_accel_on) HIDUSER_DisableAccelerometer();
    s_accel_on = s_gyro_on = false;
}

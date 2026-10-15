/* motion_input.h - Beschleunigungssensor + Gyroskop -> libpd
 *
 * PD-Receiver (nur in MODE_PATCH und MODE_CTRL, nur bei Aenderung):
 *   [receive accel-x] [receive accel-y] [receive accel-z]  float ca. -1..+1  (flach: y ca. -1)
 *       (1.0 = ca. 1 g, Neigung/Lage des Geraets; Kalibrierung ACCEL_1G)
 *   [receive gyro-x]  [receive gyro-y]  [receive gyro-z]   float -1..+1
 *       (Drehgeschwindigkeit; 1.0 = GYRO_FULL_DPS Grad/s; x=Roll, y=Pitch, z=Yaw)
 */
#ifndef MOTION_INPUT_H
#define MOTION_INPUT_H

#include <stdbool.h>

/* Sensoren einschalten (nach hidInit). Fehler werden ignoriert. */
void motion_input_init(void);

/* Pro Frame nach hidScanInput() aufrufen. forward=false -> nichts senden. */
void motion_input_poll(bool forward);

/* Beim naechsten poll alle Werte senden (z.B. nach Patchwechsel). */
void motion_input_resend(void);

/* Sensoren ausschalten (vor hidExit). */
void motion_input_exit(void);

#endif /* MOTION_INPUT_H */

/* cstick_input.h - C-Stick (New 3DS, Ministick rechts) -> libpd
 *
 * PD-Receiver (nur in BOT_MIXER und BOT_CTRL, nur bei Aenderung >= 0.01):
 *   [receive cstick-x]  float -1..+1  (rechts = positiv)
 *   [receive cstick-y]  float -1..+1  (oben   = positiv)
 *   In der Totzone (CSTICK_DEADZONE) wird 0 gesendet.
 *
 * Old 3DS / 2DS ohne C-Stick: irrstInit() schlaegt fehl oder liefert nur
 * Nullen -> es wird hoechstens einmal 0 gesendet, kein Absturz.
 */
#ifndef CSTICK_INPUT_H
#define CSTICK_INPUT_H

#include <stdbool.h>

/* ir:rst anmelden (nach hidInit). Fehler -> C-Stick bleibt aus. */
void cstick_input_init(void);

/* Pro Frame nach hidScanInput() aufrufen. forward=false -> nichts senden. */
void cstick_input_poll(bool forward);

/* Beim naechsten poll beide Werte senden (z.B. nach Patchwechsel). */
void cstick_input_resend(void);

/* ir:rst abmelden (vor hidExit), nur wenn init erfolgreich war. */
void cstick_input_exit(void);

#endif /* CSTICK_INPUT_H */

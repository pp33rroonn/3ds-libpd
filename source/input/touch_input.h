#pragma once
/* touch_input.h – Touchscreen → Pure Data message bridge
 *
 * Reads 3DS touchscreen state and sends PD messages:
 *   [receive touch-x]   → float 0..319
 *   [receive touch-y]   → float 0..239
 *   [receive touch-on]  → bang on press
 *   [receive touch-off] → bang on release
 */

#include <stdbool.h>

/** Touchscreen lesen und bei Aenderungen libpd-Messages senden.
 *  @param forward  false = nichts an PD senden (CTRL- oder Options-Modus). Ein laufender Touch wird dann mit
 *                  touch-off beendet. */
void touch_input_poll(bool forward);

/** The input slot changed: end a running touch on old_slot (touch-off) and
 *  reset, so the new slot gets a fresh touch-on / touch-x / touch-y. */
void touch_input_switch_slot(int old_slot);

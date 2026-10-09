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

/** Poll touchscreen and send libpd messages for any state change. */
void touch_input_poll(void);

#pragma once
/* font5x8.h - classic 5x7 bitmap font for the pixel UI
 *
 * 95 printable ASCII glyphs (0x20 space .. 0x7E tilde).
 * Column format: 5 bytes per glyph, one byte per column, bit0 = top row.
 * Character cell: FONT_ADV_X x FONT_ADV_Y pixels (glyph + 1 px gap).
 */

#include <stdint.h>

#define FONT5X8_FIRST  0x20
#define FONT5X8_LAST   0x7E
#define FONT5X8_COUNT  95

#define FONT_W      5   /* glyph width in pixels   */
#define FONT_H      7   /* glyph height in pixels  */
#define FONT_ADV_X  6   /* horizontal advance      */
#define FONT_ADV_Y  9   /* line height             */

extern const uint8_t FONT5X8[FONT5X8_COUNT][5];

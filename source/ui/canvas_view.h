#pragma once
/* canvas_view.h - lightweight Pure-Data patch (.pd) renderer for the top screen
 *
 * Parses a .pd file into a small set of objects (obj / msg / atom / comment and
 * the common GUI objects: tgl, hsl, vsl, nbx, hradio, vradio, cnv, bng) plus
 * the connections between them, and draws a schematic of the patch scaled to
 * fit a target rectangle (fit-to-box, aspect preserved, centered).
 *
 * The parse result is cached by file path: canvas_view_set_patch() only re-reads
 * the SD file when the path actually changes, so it is cheap to call every frame.
 * All drawing uses the column-major, y-inverted TOP-screen framebuffer (draw.h).
 */

#include <3ds.h>   /* u16 */

/* Parse and cache the patch at `path` (full sdmc:/ or romfs:/ path, e.g.
 * "sdmc:/3ds/libpd_3ds/patches/16_cam.pd"). No-op if the same path is already
 * cached. Pass NULL to clear the cache. Missing / unreadable files clear the
 * cache (nothing is drawn), never crash. */
void canvas_view_set_patch(const char *path);

/* Draw the cached patch schematic into the rectangle (x, y, w, h) on the TOP
 * framebuffer. Object boxes / labels use `fg`, connection lines use `line_col`.
 * Draws nothing if no patch is cached or it holds no objects. Clips to the
 * target rectangle and the screen. */
void canvas_view_draw(u16 *fb, int x, int y, int w, int h, u16 fg, u16 line_col);

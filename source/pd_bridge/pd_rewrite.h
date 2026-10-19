#pragma once
/* pd_rewrite.h - per-slot receiver renaming for patch copies (pure C, no libpd)
 *
 * Every patch slot gets its own namespace for the input receivers, so only
 * the patch of the active page reacts to buttons / touch / motion / CTRL.
 * Patch files keep plain names ([r touch-x], [r btn-a], [r cutoff]); when
 * the slot copy is written, matching names are prefixed with "s<N>-"
 * (N = slot + 1), e.g. [r touch-x] in slot 0 -> [r s1-touch-x].
 *
 * Renamed (exact whole-token match only):
 *   - hardware inputs: any name starting with btn- pad- touch- accel-
 *     gyro- cstick-
 *   - the extra names passed in (the CTRL receivers found by pd_parse)
 * Where:
 *   - "#X obj" records (from the class token on: [r]/[s]/[receive]/[send],
 *     IEM GUI send/receive fields, ...), sends and receives alike, so the
 *     patch-internal s/r wiring keeps working
 *   - "#X floatatom" / "#X symbolatom" / "#X listbox" (label/receive/send)
 *   - "#X msg": only the target right after a "\;" (message-box sends)
 *   - "#X array NAME" (so [tabread NAME] etc. stay consistent)
 * Never touched: "#X text" comments, names with '$', pd-*, slot-*, empty.
 *
 * Abstractions used by the patch get the same treatment: pd_bridge.c copies
 * them (recursively) into the slot's own folder, where Pd finds them before
 * the originals (an abstraction is looked up in the calling patch's folder
 * first). See pd_rewrite_obj_class().
 */

#include <stddef.h>
#include <stdio.h>

#define PD_REWRITE_NAME_MAX 64

/** true if `tok` (length len) is a hardware input name (btn-, touch-, ...). */
int pd_rewrite_is_hw_name(const char *tok, size_t len);

/** Build the routed receiver name "s<slot+1>-<name>" into out. */
void pd_rewrite_slot_name(char *out, size_t out_len, int slot, const char *name);

/**
 * Write one record [rec, end) (without its terminating ';') to out with the
 * routed names prefixed for `slot`. extra = names to rename in addition to
 * the hardware names (may be NULL), each entry `stride` bytes apart
 * (so a PdCtrl array can be passed directly via &ctrls[0].name).
 * @return number of renamed tokens.
 */
int pd_rewrite_record(FILE *out, const char *rec, const char *end, int slot,
                      const char *extra, size_t stride, int n_extra);

/**
 * If [rec, end) is "#X obj x y CLASS ..." and CLASS could be an abstraction
 * file name (relative path like "voice" or "lib/voice": no '$', '\\', ':',
 * "..", leading '/', not "pd"), copy CLASS to out and return 1, else 0.
 * Used to give every slot its own renamed copy of the patch's abstractions.
 */
int pd_rewrite_obj_class(const char *rec, const char *end, char *out, size_t out_len);

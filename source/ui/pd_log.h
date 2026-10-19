#pragma once
/* pd_log.h - ring buffer of the latest PD print messages (top screen log)
 *
 * Fed from the libpd print hook (pd_bridge.c), shown on the PD Log page.
 */

#ifndef PD_LOG_LINES
#  define PD_LOG_LINES 20   /* stored lines */
#endif
#define PD_LOG_MAX 64       /* bytes per line incl. terminator */

/* Store one line (newlines stripped, trimmed to PD_LOG_MAX - 1 chars). */
void pd_log_push(const char *msg);

/* Line i, 0 = newest; NULL if there is no such line. */
const char *pd_log_get(int i);

/* Number of stored lines (0..PD_LOG_LINES). */
int pd_log_count(void);

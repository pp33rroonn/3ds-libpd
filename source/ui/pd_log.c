/* pd_log.c - ring buffer of the latest PD print messages (see pd_log.h) */

#include "pd_log.h"

static char s_lines[PD_LOG_LINES][PD_LOG_MAX];
static int  s_head  = 0;   /* next slot to write */
static int  s_count = 0;

void pd_log_push(const char *msg)
{
    if (!msg) return;
    char *dst = s_lines[s_head];
    int n = 0;
    for (; *msg && n < PD_LOG_MAX - 1; msg++) {
        unsigned char c = (unsigned char)*msg;
        if (c == '\n' || c == '\r') continue;
        dst[n++] = (c < 32 || c > 126) ? '?' : (char)c;
    }
    dst[n] = '\0';

    s_head = (s_head + 1) % PD_LOG_LINES;
    if (s_count < PD_LOG_LINES) s_count++;
}

const char *pd_log_get(int i)
{
    if (i < 0 || i >= s_count) return 0;
    int idx = (s_head - 1 - i + 2 * PD_LOG_LINES) % PD_LOG_LINES;
    return s_lines[idx];
}

int pd_log_count(void)
{
    return s_count;
}

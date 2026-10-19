/* pd_rewrite.c - per-slot receiver renaming (see pd_rewrite.h) */

#include "pd_rewrite.h"

#include <string.h>

static const char *const HW_PREFIXES[] = {
    "btn-", "pad-", "touch-", "accel-", "gyro-", "cstick-",
};

static int is_ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

static int tok_eq(const char *tok, size_t len, const char *s)
{
    return strlen(s) == len && memcmp(tok, s, len) == 0;
}

int pd_rewrite_is_hw_name(const char *tok, size_t len)
{
    for (size_t i = 0; i < sizeof(HW_PREFIXES) / sizeof(HW_PREFIXES[0]); i++) {
        size_t pl = strlen(HW_PREFIXES[i]);
        if (len > pl && memcmp(tok, HW_PREFIXES[i], pl) == 0) return 1;
    }
    return 0;
}

void pd_rewrite_slot_name(char *out, size_t out_len, int slot, const char *name)
{
    snprintf(out, out_len, "s%d-%s", slot + 1, name);
}

/* true if the token must get the slot prefix */
static int is_target(const char *tok, size_t len,
                     const char *extra, size_t stride, int n_extra)
{
    if (len == 0 || len >= PD_REWRITE_NAME_MAX) return 0;
    if (memchr(tok, '$', len)) return 0;               /* $0-x, \$1 ... */
    /* global names (sent by the app to all slots) are never prefixed */
    if ((len > 6 && !strncmp(tok, "clock-", 6)) ||
        (len > 5 && !strncmp(tok, "slot-", 5)) ||
        (len > 3 && !strncmp(tok, "pd-", 3)))
        return 0;
    if (pd_rewrite_is_hw_name(tok, len)) return 1;
    for (int i = 0; extra && i < n_extra; i++) {
        const char *n = extra + (size_t)i * stride;
        if (n[0] && tok_eq(tok, len, n)) return 1;
    }
    return 0;
}

typedef enum { REC_OTHER, REC_OBJ, REC_ATOM, REC_MSG, REC_ARRAY } RecKind;

int pd_rewrite_record(FILE *out, const char *rec, const char *end, int slot,
                      const char *extra, size_t stride, int n_extra)
{
    RecKind kind = REC_OTHER;
    int idx = 0, renamed = 0, after_semi = 0;
    const char *copied = rec;   /* bytes before this are written already */
    const char *p = rec;

    while (p < end) {
        while (p < end && is_ws(*p)) p++;
        if (p >= end) break;
        const char *t = p;
        while (p < end && !is_ws(*p)) p++;
        size_t len = (size_t)(p - t);

        if (idx == 0 && !tok_eq(t, len, "#X")) break;      /* #N, #A, ... */
        if (idx == 1) {
            if (tok_eq(t, len, "obj"))                      kind = REC_OBJ;
            else if (tok_eq(t, len, "floatatom") ||
                     tok_eq(t, len, "symbolatom") ||
                     tok_eq(t, len, "listbox"))             kind = REC_ATOM;
            else if (tok_eq(t, len, "msg"))                 kind = REC_MSG;
            else if (tok_eq(t, len, "array"))               kind = REC_ARRAY;
            else break;                                     /* text, connect, ... */
        }

        int rename = 0;
        if (kind == REC_ARRAY) {   /* "#X array NAME ...": keeps [tabread NAME] valid */
            rename = idx == 2 && is_target(t, len, extra, stride, n_extra);
        } else if (idx >= 4) {   /* after "#X <kind> x y" */
            if (kind == REC_MSG) {
                rename = after_semi && is_target(t, len, extra, stride, n_extra);
                after_semi = tok_eq(t, len, "\\;");
            } else {
                rename = is_target(t, len, extra, stride, n_extra);
            }
        }
        if (rename) {
            fwrite(copied, 1, (size_t)(t - copied), out);
            fprintf(out, "s%d-", slot + 1);
            fwrite(t, 1, len, out);
            copied = p;
            renamed++;
        }
        idx++;
    }
    fwrite(copied, 1, (size_t)(end - copied), out);
    return renamed;
}

int pd_rewrite_obj_class(const char *rec, const char *end, char *out, size_t out_len)
{
    const char *p = rec;
    for (int idx = 0; p < end; idx++) {
        while (p < end && is_ws(*p)) p++;
        if (p >= end) return 0;
        const char *t = p;
        while (p < end && !is_ws(*p)) p++;
        size_t len = (size_t)(p - t);

        if (idx == 0 && !tok_eq(t, len, "#X"))  return 0;
        if (idx == 1 && !tok_eq(t, len, "obj")) return 0;
        if (idx < 4) continue;

        /* "#X obj 10 10 voice, f 12": the width suffix sticks to the token */
        if (len > 0 && t[len - 1] == ',') len--;
        if (len == 0 || len >= out_len || tok_eq(t, len, "pd")) return 0;
        if (t[0] == '/' || t[0] == '.' || t[len - 1] == '/') return 0;
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)t[i];
            if (c <= 32 || c > 126 || c == '$' || c == '\\' || c == ':' || c == ',' || c == ';')
                return 0;
            if (c == '.' && i + 1 < len && t[i + 1] == '.') return 0;
        }
        memcpy(out, t, len);
        out[len] = '\0';
        return 1;
    }
    return 0;
}

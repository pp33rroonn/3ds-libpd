/* canvas_view.c - schematic Pure-Data patch renderer (see canvas_view.h) */

#include "canvas_view.h"

#include "draw.h"        /* draw_pixel / draw_rect / draw_string / TOP_W/H */
#include "font5x8.h"     /* FONT_ADV_X / FONT_ADV_Y / FONT_W */
#include "theme.h"       /* TH.bg */

#include <stdio.h>
#include <stdlib.h>      /* atoi */
#include <string.h>

/* Limits chosen for 3DS memory / per-frame cost. */
#define CV_MAX_OBJ    128
#define CV_MAX_CONN   256
#define CV_TEXT_MAX   28
#define CV_FILE_MAX   65536   /* biggest .pd we bother to parse (64 KB) */

/* Object kinds. */
enum {
    CVK_OBJ = 0,   /* generic object box with text   */
    CVK_MSG,       /* message box                    */
    CVK_ATOM,      /* floatatom (number box)         */
    CVK_SYMATOM,   /* symbolatom                     */
    CVK_COMMENT,   /* comment text (no frame)        */
    CVK_TGL,       /* toggle                         */
    CVK_HSL,       /* horizontal slider              */
    CVK_VSL,       /* vertical slider                */
    CVK_NBX,       /* number2 box                    */
    CVK_HRADIO,    /* horizontal radio               */
    CVK_VRADIO,    /* vertical radio                 */
    CVK_CNV,       /* canvas / colour rectangle      */
    CVK_BNG        /* bang                           */
};

typedef struct {
    short x, y;              /* Pd canvas coordinates          */
    short w, h;              /* Pd-unit size (GUI objs); 0 = derive from text */
    unsigned char kind;
    short count;             /* radio cell count / toggle on-state */
    char text[CV_TEXT_MAX];  /* label for obj/msg/atom/comment */
} CvObj;

typedef struct {
    short src, dst;
    unsigned char outlet, inlet;
} CvConn;

static CvObj  s_obj[CV_MAX_OBJ];
static CvConn s_conn[CV_MAX_CONN];
static int    s_nobj  = 0;
static int    s_nconn = 0;
static int    s_cw = 0, s_ch = 0;     /* declared canvas size (Pd units) */
static char   s_path[512] = "";       /* currently cached file path */
static bool   s_valid = false;

/* ---- parsing -------------------------------------------------------------- */

/* Estimate a Pd-unit width for a text object from its label length. Pd's own
 * boxes are roughly 7 px per character at the default font; a floor keeps tiny
 * boxes visible. */
static int text_width_pd(const char *s)
{
    int n = (int)strlen(s);
    int w = n * 7 + 8;
    if (w < 18) w = 18;
    return w;
}

/* Join tokens[from..n-1] into dst (space separated), truncated to CV_TEXT_MAX. */
static void join_text(char *dst, char **tok, int from, int n)
{
    dst[0] = '\0';
    int len = 0;
    for (int i = from; i < n && len < CV_TEXT_MAX - 1; i++) {
        if (i > from && len < CV_TEXT_MAX - 1) dst[len++] = ' ';
        const char *t = tok[i];
        while (*t && len < CV_TEXT_MAX - 1) dst[len++] = *t++;
    }
    dst[len] = '\0';
}

/* Tokenise a record in-place (whitespace separated). Returns token count. */
static int tokenize(char *rec, char **tok, int max)
{
    int n = 0;
    char *p = rec;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        tok[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static void add_obj(const CvObj *o)
{
    if (s_nobj < CV_MAX_OBJ) s_obj[s_nobj] = *o;
    s_nobj++;   /* keep counting even past the cap so connect indices stay sane */
}

/* Parse one '#X' / '#N' record (already '\0'-terminated, no trailing ';'). */
static void parse_record(char *rec)
{
    char *tok[48];
    int n = tokenize(rec, tok, 48);
    if (n < 2) return;

    if (strcmp(tok[0], "#N") == 0 && strcmp(tok[1], "canvas") == 0) {
        /* Only the first (main) canvas sets the fit dimensions. */
        if (s_cw == 0 && n >= 6) { s_cw = atoi(tok[4]); s_ch = atoi(tok[5]); }
        return;
    }
    if (strcmp(tok[0], "#X") != 0) return;

    if (strcmp(tok[1], "connect") == 0 && n >= 6) {
        if (s_nconn < CV_MAX_CONN) {
            CvConn c;
            c.src    = (short)atoi(tok[2]);
            c.outlet = (unsigned char)atoi(tok[3]);
            c.dst    = (short)atoi(tok[4]);
            c.inlet  = (unsigned char)atoi(tok[5]);
            s_conn[s_nconn++] = c;
        }
        return;
    }

    /* object-like records carry X Y at tok[2]/tok[3] */
    if (n < 4) return;
    CvObj o;
    memset(&o, 0, sizeof(o));
    o.x = (short)atoi(tok[2]);
    o.y = (short)atoi(tok[3]);

    if (strcmp(tok[1], "obj") == 0) {
        const char *k = (n >= 5) ? tok[4] : "";
        if (strcmp(k, "tgl") == 0) {
            o.kind = CVK_TGL; o.w = o.h = (short)(n >= 6 ? atoi(tok[5]) : 15);
        } else if (strcmp(k, "hsl") == 0) {
            o.kind = CVK_HSL;
            o.w = (short)(n >= 6 ? atoi(tok[5]) : 128);
            o.h = (short)(n >= 7 ? atoi(tok[6]) : 15);
        } else if (strcmp(k, "vsl") == 0) {
            o.kind = CVK_VSL;
            o.w = (short)(n >= 6 ? atoi(tok[5]) : 15);
            o.h = (short)(n >= 7 ? atoi(tok[6]) : 128);
        } else if (strcmp(k, "hradio") == 0) {
            o.kind = CVK_HRADIO;
            o.h = (short)(n >= 6 ? atoi(tok[5]) : 15);
            o.count = (short)(n >= 9 ? atoi(tok[8]) : 8);
            if (o.count < 1) o.count = 1;
            o.w = (short)(o.h * o.count);
        } else if (strcmp(k, "vradio") == 0) {
            o.kind = CVK_VRADIO;
            o.w = (short)(n >= 6 ? atoi(tok[5]) : 15);
            o.count = (short)(n >= 9 ? atoi(tok[8]) : 8);
            if (o.count < 1) o.count = 1;
            o.h = (short)(o.w * o.count);
        } else if (strcmp(k, "nbx") == 0) {
            o.kind = CVK_NBX;
            int digits = (n >= 6 ? atoi(tok[5]) : 5);
            o.w = (short)(digits * 9 + 6);
            o.h = (short)(n >= 7 ? atoi(tok[6]) : 14);
        } else if (strcmp(k, "cnv") == 0) {
            o.kind = CVK_CNV;
            o.w = (short)(n >= 7 ? atoi(tok[6]) : 100);
            o.h = (short)(n >= 8 ? atoi(tok[7]) : 60);
        } else if (strcmp(k, "bng") == 0) {
            o.kind = CVK_BNG; o.w = o.h = (short)(n >= 6 ? atoi(tok[5]) : 15);
        } else {
            o.kind = CVK_OBJ;
            join_text(o.text, tok, 4, n);
            o.w = (short)text_width_pd(o.text);
            o.h = 18;
        }
    } else if (strcmp(tok[1], "msg") == 0) {
        o.kind = CVK_MSG;
        join_text(o.text, tok, 4, n);
        o.w = (short)text_width_pd(o.text);
        o.h = 18;
    } else if (strcmp(tok[1], "floatatom") == 0) {
        o.kind = CVK_ATOM;
        int digits = (n >= 5 ? atoi(tok[4]) : 5);
        if (digits < 3) digits = 5;
        o.w = (short)(digits * 7 + 6);
        o.h = 16;
    } else if (strcmp(tok[1], "symbolatom") == 0) {
        o.kind = CVK_SYMATOM;
        int digits = (n >= 5 ? atoi(tok[4]) : 6);
        if (digits < 3) digits = 6;
        o.w = (short)(digits * 7 + 6);
        o.h = 16;
    } else if (strcmp(tok[1], "text") == 0) {
        o.kind = CVK_COMMENT;
        join_text(o.text, tok, 4, n);
        o.w = (short)text_width_pd(o.text);
        o.h = 14;
    } else {
        /* #X coords / restore / array / ... : not a gobj, keep index space
         * correct by NOT adding it. */
        return;
    }
    add_obj(&o);
}

/* Split the whole file buffer into records terminated by an unescaped ';'. */
static void parse_buffer(char *buf, int len)
{
    int start = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == ';' && (i == 0 || buf[i - 1] != '\\')) {
            buf[i] = '\0';
            parse_record(&buf[start]);
            start = i + 1;
            if (s_nobj >= CV_MAX_OBJ && s_nconn >= CV_MAX_CONN) break;
        }
    }
}

void canvas_view_set_patch(const char *path)
{
    if (!path) {
        s_path[0] = '\0';
        s_valid = false;
        s_nobj = s_nconn = 0;
        return;
    }
    if (s_valid && strcmp(path, s_path) == 0) return;   /* already cached */

    snprintf(s_path, sizeof(s_path), "%s", path);
    s_nobj = s_nconn = 0;
    s_cw = s_ch = 0;
    s_valid = false;

    FILE *f = fopen(path, "rb");
    if (!f) return;

    static char buf[CV_FILE_MAX];
    size_t rd = fread(buf, 1, CV_FILE_MAX - 1, f);
    fclose(f);
    if (rd == 0) return;
    buf[rd] = '\0';

    parse_buffer(buf, (int)rd);

    if (s_cw <= 0) s_cw = 700;     /* sane defaults if canvas line was odd */
    if (s_ch <= 0) s_ch = 500;
    if (s_nobj > CV_MAX_OBJ) s_nobj = CV_MAX_OBJ;
    s_valid = (s_nobj > 0);
}

/* ---- drawing -------------------------------------------------------------- */

/* fit-to-box transform: Pd (px,py) -> screen (sx,sy); size scale in `scl`. */
typedef struct { float scl; int ox, oy; } CvFit;

static CvFit make_fit(int x, int y, int w, int h)
{
    CvFit t;
    float sx = (float)w / (float)s_cw;
    float sy = (float)h / (float)s_ch;
    t.scl = sx < sy ? sx : sy;
    if (t.scl <= 0.0f) t.scl = 0.01f;
    t.ox = x + (int)((w - s_cw * t.scl) / 2.0f);
    t.oy = y + (int)((h - s_ch * t.scl) / 2.0f);
    return t;
}

static inline int map_x(const CvFit *t, int px) { return t->ox + (int)(px * t->scl); }
static inline int map_y(const CvFit *t, int py) { return t->oy + (int)(py * t->scl); }

/* Clipped line via Bresenham (draw_pixel already clips to the screen). */
static void line(u16 *fb, int x0, int y0, int x1, int y1, u16 col)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        draw_pixel(fb, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Rectangle outline clipped to a bounding box (cx..cx+cw, cy..cy+ch). */
static void box(u16 *fb, int x, int y, int w, int h, u16 col)
{
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    for (int i = 0; i <= w; i++) { draw_pixel(fb, x + i, y, col); draw_pixel(fb, x + i, y + h, col); }
    for (int i = 0; i <= h; i++) { draw_pixel(fb, x, y + i, col); draw_pixel(fb, x + w, y + i, col); }
}

/* Draw text clipped to a max pixel width (truncate characters that overflow). */
static void text_clip(u16 *fb, int x, int y, const char *s, int maxw, u16 fg)
{
    char tmp[CV_TEXT_MAX];
    int maxc = maxw / FONT_ADV_X;
    if (maxc <= 0) return;
    if (maxc > CV_TEXT_MAX - 1) maxc = CV_TEXT_MAX - 1;
    int i = 0;
    for (; s[i] && i < maxc; i++) tmp[i] = s[i];
    tmp[i] = '\0';
    draw_string(fb, x, y, tmp, fg, TH.bg);
}

static void draw_object(u16 *fb, const CvFit *t, const CvObj *o, u16 fg)
{
    int x = map_x(t, o->x);
    int y = map_y(t, o->y);
    int w = (int)(o->w * t->scl);
    int h = (int)(o->h * t->scl);
    if (w < 3) w = 3;
    if (h < 3) h = 3;

    switch (o->kind) {
        case CVK_COMMENT:
            text_clip(fb, x, y, o->text, w + 60, fg);
            break;
        case CVK_TGL:
            box(fb, x, y, w, h, fg);
            if (o->count) {   /* default-on toggle: cross */
                line(fb, x, y, x + w, y + h, fg);
                line(fb, x + w, y, x, y + h, fg);
            }
            break;
        case CVK_BNG:
            box(fb, x, y, w, h, fg);
            /* inscribed circle (cheap: a few arc points) */
            {
                int cx = x + w / 2, cy = y + h / 2, r = (w < h ? w : h) / 2;
                for (int a = 0; a < 360; a += 20) {
                    float rad = (float)a * 3.14159f / 180.0f;
                    draw_pixel(fb, cx + (int)(r * __builtin_cosf(rad)),
                                   cy + (int)(r * __builtin_sinf(rad)), fg);
                }
            }
            break;
        case CVK_HSL:
            box(fb, x, y, w, h, fg);
            line(fb, x + w / 4, y, x + w / 4, y + h, fg);   /* handle */
            break;
        case CVK_VSL:
            box(fb, x, y, w, h, fg);
            line(fb, x, y + h - h / 4, x + w, y + h - h / 4, fg);
            break;
        case CVK_HRADIO:
            box(fb, x, y, w, h, fg);
            for (int i = 1; i < o->count; i++)
                line(fb, x + (w * i) / o->count, y, x + (w * i) / o->count, y + h, fg);
            break;
        case CVK_VRADIO:
            box(fb, x, y, w, h, fg);
            for (int i = 1; i < o->count; i++)
                line(fb, x, y + (h * i) / o->count, x + w, y + (h * i) / o->count, fg);
            break;
        case CVK_CNV:
            box(fb, x, y, w, h, fg);
            break;
        case CVK_ATOM:
        case CVK_SYMATOM:
        case CVK_NBX:
            box(fb, x, y, w, h, fg);
            /* flag the number box with a clipped top-right corner */
            line(fb, x + w - h / 2, y, x + w, y + h / 2, fg);
            break;
        case CVK_MSG:
            box(fb, x, y, w, h, fg);
            /* message flag on the right edge */
            line(fb, x + w, y, x + w + 3, y + 3, fg);
            line(fb, x + w, y + h, x + w + 3, y + h - 3, fg);
            if (h >= FONT_ADV_Y) text_clip(fb, x + 2, y + (h - FONT_H) / 2, o->text, w - 3, fg);
            break;
        case CVK_OBJ:
        default:
            box(fb, x, y, w, h, fg);
            if (h >= FONT_ADV_Y) text_clip(fb, x + 2, y + (h - FONT_H) / 2, o->text, w - 3, fg);
            break;
    }
}

/* Outlet / inlet anchor points. Without knowing the real I/O count we spread a
 * few fixed-width nubs across the box edge, which matches Pd's layout closely
 * enough for a schematic. */
static int io_x(const CvFit *t, const CvObj *o, int idx)
{
    int x = map_x(t, o->x);
    int w = (int)(o->w * t->scl);
    if (w < 3) w = 3;
    int nub = x + 2 + idx * 8;
    int max = x + w - 2;
    if (nub > max) nub = max;
    if (nub < x) nub = x;
    return nub;
}

void canvas_view_draw(u16 *fb, int x, int y, int w, int h, u16 fg, u16 line_col)
{
    if (!fb || !s_valid || s_nobj <= 0 || w <= 4 || h <= 4) return;

    CvFit t = make_fit(x, y, w, h);
    int nobj = s_nobj < CV_MAX_OBJ ? s_nobj : CV_MAX_OBJ;

    /* connections first (under the boxes) */
    for (int i = 0; i < s_nconn; i++) {
        int s = s_conn[i].src, d = s_conn[i].dst;
        if (s < 0 || s >= nobj || d < 0 || d >= nobj) continue;
        const CvObj *so = &s_obj[s];
        const CvObj *dobj = &s_obj[d];
        int sx = io_x(&t, so, s_conn[i].outlet);
        int sy = map_y(&t, so->y) + (int)(so->h * t.scl);   /* bottom edge */
        int dx = io_x(&t, dobj, s_conn[i].inlet);
        int dy = map_y(&t, dobj->y);                        /* top edge */
        line(fb, sx, sy, dx, dy, line_col);
    }

    /* object boxes on top */
    for (int i = 0; i < nobj; i++)
        draw_object(fb, &t, &s_obj[i], fg);
}

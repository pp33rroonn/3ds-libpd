/*
 * pd_sys_stubs.c — Symbole, die in der 3DS-libpd.a fehlen, weil
 * s_main.c / s_inter.c / s_path.c beim Bauen der Bibliothek gestubbt wurden.
 * Typen/Initialwerte/Signaturen entsprechen dem Original (pure-data f009fd8,
 * Deklarationen aus m_pd.h / s_stuff.h).
 *
 * Alles ist WEAK: Falls libpd.a ein Symbol doch definiert, gewinnt dessen
 * (starke) Definition ohne "multiple definition"-Fehler.
 */
#include <string.h>
#include <sys/time.h>
#include "m_pd.h"

#define WEAK __attribute__((weak))

/* Typen aus s_stuff.h (dort nicht öffentlich installiert) */
typedef void (*t_fdpollfn)(void *ptr, int fd);
typedef void (*t_socketnotifier)(void *x, int n);
typedef void (*t_socketreceivefn)(void *x, t_binbuf *b);
struct _socketreceiver;

/* ---------------- Variablen aus s_main.c ---------------- */
WEAK int sys_debuglevel;
WEAK int sys_verbose;
WEAK int sys_noloadbang;
WEAK int sys_hipriority = -1;
WEAK t_symbol *sys_libdir;          /* libpd_init() setzt gensym("") */
WEAK int sys_nmidiout = -1;
WEAK int sys_nmidiin = -1;
WEAK int sys_externalschedlib;
WEAK char sys_font[100] = "DejaVu Sans Mono";
WEAK char sys_fontweight[10] = "bold";
WEAK int sys_defaultfont = 12;
WEAK char sys_devicename[MAXPDSTRING] = "Pure Data";
WEAK int sys_guisetportnumber;
WEAK int sys_nosleep;
WEAK int sys_defeatrt;
WEAK t_symbol *sys_flags;
WEAK const char *sys_guicmd;
WEAK int sys_oldtclversion;
WEAK int sys_batch;
WEAK const char *pd_extraflags;
WEAK int sys_noautopatch;

/* aus s_path.c */
WEAK int sys_usestdpath = 1;        /* libpd_init() setzt es auf 0 */

/* ---------------- Font-Metriken aus s_main.c ---------------- */
typedef struct { int pointsize, width, height; } stub_fontinfo;
static const stub_fontinfo stub_fonts[2][6] = {
    {{8, 5, 11}, {10, 6, 13}, {12, 7, 16}, {16, 10, 19}, {24, 14, 29}, {36, 22, 44}},
    {{16, 10, 22}, {20, 12, 26}, {24, 14, 32}, {32, 20, 38}, {48, 28, 58}, {72, 44, 88}}
};

static int stub_findfont(int fontsize)
{
    int i;
    for (i = 0; i < 5; i++)
        if (fontsize < stub_fonts[0][i + 1].pointsize) return i;
    return 5;
}

static int stub_zoom(int z) { return z < 1 ? 1 : (z > 2 ? 2 : z); }

WEAK int sys_nearestfontsize(int fontsize)
{
    return stub_fonts[0][stub_findfont(fontsize)].pointsize;
}

WEAK int sys_hostfontsize(int fontsize, int zoom)
{
    return stub_fonts[stub_zoom(zoom) - 1][stub_findfont(fontsize)].pointsize;
}

WEAK int sys_zoomfontwidth(int fontsize, int zoom, int worstcase)
{
    int z = stub_zoom(zoom), ret;
    if (worstcase) ret = z * stub_fonts[0][stub_findfont(fontsize)].width;
    else ret = stub_fonts[z - 1][stub_findfont(fontsize)].width;
    return ret < 1 ? 1 : ret;
}

WEAK int sys_zoomfontheight(int fontsize, int zoom, int worstcase)
{
    int z = stub_zoom(zoom), ret;
    if (worstcase) ret = z * stub_fonts[0][stub_findfont(fontsize)].height;
    else ret = stub_fonts[z - 1][stub_findfont(fontsize)].height;
    return ret < 1 ? 1 : ret;
}

WEAK int sys_fontwidth(int fontsize)  { return sys_zoomfontwidth(fontsize, 1, 0); }
WEAK int sys_fontheight(int fontsize) { return sys_zoomfontheight(fontsize, 1, 0); }

/* ---------------- GUI (gibt es auf dem 3DS nicht) ---------------- */
WEAK int sys_havegui(void) { return 0; }
WEAK int sys_havetkproc(void) { return 0; }
WEAK void sys_vgui(const char *fmt, ...) { (void)fmt; }
WEAK void sys_gui(const char *s) { (void)s; }
WEAK void sys_pretendguibytes(int n) { (void)n; }
WEAK void sys_queuegui(void *client, t_glist *glist, t_guicallbackfn f)
{
    (void)client; (void)glist; (void)f;
}
WEAK void sys_unqueuegui(void *client) { (void)client; }
WEAK int sys_pollgui(void) { return 0; }
WEAK void sys_gui_preferences(void) {}
WEAK int sys_startgui(const char *libdir) { (void)libdir; return 0; }
WEAK void sys_stopgui(void) {}
WEAK void sys_doneglobinit(void) {}
WEAK void glob_ping(t_pd *dummy) { (void)dummy; }
WEAK void glob_watchdog(void *dummy) { (void)dummy; }
WEAK void glob_vis(void *dummy, t_symbol *s) { (void)dummy; (void)s; }
WEAK void glob_exit(void *dummy, t_floatarg status) { (void)dummy; (void)status; }
WEAK void glob_quit(void *dummy, t_floatarg status) { (void)dummy; (void)status; }
WEAK t_symbol *sys_decodedialog(t_symbol *s) { return s; }

/* ---------------- System / Scheduler (s_inter.c) ---------------- */
WEAK double sys_getrealtime(void)
{
    static struct timeval then;
    struct timeval now;
    gettimeofday(&now, 0);
    if (then.tv_sec == 0 && then.tv_usec == 0) then = now;
    return (now.tv_sec - then.tv_sec) + (1. / 1000000.) * (now.tv_usec - then.tv_usec);
}
WEAK void sys_microsleep(void) {}
WEAK void sys_setalarm(int microsec) { (void)microsec; }
WEAK void sys_setsignalhandlers(void) {}
WEAK void sys_set_priority(int higher) { (void)higher; }
WEAK void sys_setrealtime(const char *libdir) { (void)libdir; }
WEAK void sys_bail(int exitcode) { (void)exitcode; }
WEAK void sys_init_fdpoll(void) {}

/* single-threaded: Locks sind No-ops (wie im Original ohne PDINSTANCE/threads) */
WEAK void pd_globallock(void) {}
WEAK void pd_globalunlock(void) {}
WEAK void sys_lock(void) {}
WEAK void sys_unlock(void) {}
WEAK int sys_trylock(void) { return 1; }
WEAK void pd_queue_mess(struct _pdinstance *instance, t_pd *obj, void *data, t_messfn fn)
{
    (void)instance;
    if (fn) fn(obj, data);   /* sofort ausführen statt einreihen */
}
WEAK void pd_queue_cancel(t_pd *obj) { (void)obj; }
WEAK void messqueue_dispatch(void) {}

/* ---------------- Netzwerk (kein Netz auf dem 3DS-Port) ---------------- */
WEAK unsigned char *sys_getrecvbuf(unsigned int *size)
{
    static unsigned char buf[4];
    if (size) *size = 0;
    return buf;
}
WEAK void sys_sockerror(const char *s) { (void)s; }
WEAK void sys_closesocket(int fd) { (void)fd; }
WEAK void sys_addpollfn(int fd, t_fdpollfn fn, void *ptr) { (void)fd; (void)fn; (void)ptr; }
WEAK void sys_rmpollfn(int fd) { (void)fd; }
WEAK struct _socketreceiver *socketreceiver_new(void *owner,
    t_socketnotifier notifier, t_socketreceivefn socketreceivefn, int udp)
{
    (void)owner; (void)notifier; (void)socketreceivefn; (void)udp;
    return 0;
}
WEAK void socketreceiver_read(struct _socketreceiver *x, int fd) { (void)x; (void)fd; }

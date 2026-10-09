/*
 * pd_sys_stubs.c — Definitionen, die in der 3DS-libpd.a fehlen, weil
 * s_main.c / s_inter.c / s_path.c beim Bauen der Bibliothek gestubbt wurden.
 * Typen/Initialwerte entsprechen dem Original (pure-data f009fd8).
 */
#include "m_pd.h"

/* aus s_main.c */
int sys_debuglevel;
int sys_verbose;
int sys_noloadbang;
int sys_hipriority = -1;
t_symbol *sys_libdir;          /* wird von libpd_init() per gensym("") gesetzt */
int sys_nmidiout = -1;
int sys_nmidiin = -1;
int sys_externalschedlib;

/* aus s_path.c */
int sys_usestdpath = 1;        /* libpd_init() setzt es auf 0 */

/* aus s_inter.c — kein Socket-/fd-Polling auf dem 3DS, single-threaded */
void sys_init_fdpoll(void);
void pd_globallock(void);
void pd_globalunlock(void);

void sys_init_fdpoll(void) {}
void pd_globallock(void) {}
void pd_globalunlock(void) {}

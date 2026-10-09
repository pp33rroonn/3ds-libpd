/*
 * pd_path_stubs.c — fehlende Symbole aus s_path.c, s_main.c (Dialoge),
 * x_net.c sowie POSIX-Funktionen, die newlib/devkitARM nicht implementiert
 * (geteuid, glob, globfree, nftw).
 *
 * Pfad-Funktionen sind aus pure-data f009fd8 src/s_path.c portiert.
 * Alles WEAK: eine echte Definition in libpd.a hat Vorrang.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "m_pd.h"

#if defined(__has_include)
# if __has_include("s_stuff.h")
#  include "s_stuff.h"
#  define HAVE_S_STUFF 1
# endif
#endif

#ifndef HAVE_S_STUFF
/* identisch zur Definition in s_stuff.h */
typedef struct _namelist
{
    struct _namelist *nl_next;
    char *nl_string;
} t_namelist;
#endif

#ifndef DEFDACBLKSIZE
# define DEFDACBLKSIZE 64
#endif

#define WEAK __attribute__((weak))

/* ---------------- s_path.c ---------------- */
WEAK void sys_bashfilename(const char *from, char *to)
{
    char c;
    while ((c = *from++)) *to++ = c;
    *to = 0;
}

WEAK void sys_unbashfilename(const char *from, char *to)
{
    char c;
    while ((c = *from++)) *to++ = c;
    *to = 0;
}

/* kein $HOME auf dem 3DS: "~" wird zu "sdmc:" */
WEAK void sys_expandpath(const char *from, char *to, int bufsize)
{
    if (bufsize < 1) return;
    if ((strlen(from) == 1 && from[0] == '~') || strncmp(from, "~/", 2) == 0)
    {
        const char *home = getenv("HOME");
        if (!home) home = "sdmc:";
        strncpy(to, home, bufsize);
        to[bufsize - 1] = 0;
        strncat(to, from + 1, bufsize - strlen(to) - 1);
    }
    else
    {
        strncpy(to, from, bufsize);
        to[bufsize - 1] = 0;
    }
}

WEAK FILE *sys_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

static int stub_isabsolute(const char *p)
{
    /* "/..." oder Gerätepfad wie "sdmc:/..." / "romfs:/..." */
    return p[0] == '/' || p[0] == '~' || strstr(p, ":/") != NULL;
}

static int stub_tryopen(const char *dir, const char *name, const char *ext,
    char *dirresult, char **nameresult, unsigned int size)
{
    int fd;
    char buf[MAXPDSTRING];
    struct stat st;
    char *slash;

    if (strlen(dir) + strlen(name) + strlen(ext) + 4 > size)
        return -1;
    sys_expandpath(dir, buf, MAXPDSTRING);
    strcpy(dirresult, buf);
    if (*dirresult && dirresult[strlen(dirresult) - 1] != '/')
        strcat(dirresult, "/");
    strcat(dirresult, name);
    strcat(dirresult, ext);

    if ((fd = open(dirresult, O_RDONLY)) < 0)
        return -1;
    if (fstat(fd, &st) < 0 || S_ISDIR(st.st_mode))
    {
        close(fd);
        return -1;
    }
    slash = strrchr(dirresult, '/');
    if (slash)
    {
        *slash = 0;
        *nameresult = slash + 1;
    }
    else *nameresult = dirresult;
    return fd;
}

WEAK int do_open_via_path(const char *dir, const char *name,
    const char *ext, char *dirresult, char **nameresult, unsigned int size,
    int bin, t_namelist *searchpath, int okgui)
{
    t_namelist *nl;
    int fd;
    (void)bin; (void)okgui;

    if (stub_isabsolute(name))
    {
        char dirbuf[MAXPDSTRING];
        const char *z = strrchr(name, '/');
        int dirlen;
        if (!z) return -1;
        dirlen = (int)(z - name);
        if (dirlen > MAXPDSTRING - 1) dirlen = MAXPDSTRING - 1;
        strncpy(dirbuf, name, dirlen);
        dirbuf[dirlen] = 0;
        return stub_tryopen(dirbuf, z + 1, ext, dirresult, nameresult, size);
    }
    if ((fd = stub_tryopen(dir, name, ext, dirresult, nameresult, size)) >= 0)
        return fd;
#ifdef HAVE_S_STUFF
    for (nl = STUFF->st_temppath; nl; nl = nl->nl_next)
        if ((fd = stub_tryopen(nl->nl_string, name, ext,
            dirresult, nameresult, size)) >= 0)
                return fd;
#endif
    for (nl = searchpath; nl; nl = nl->nl_next)
        if ((fd = stub_tryopen(nl->nl_string, name, ext,
            dirresult, nameresult, size)) >= 0)
                return fd;
    *dirresult = 0;
    *nameresult = dirresult;
    return -1;
}

WEAK int open_via_path(const char *dir, const char *name, const char *ext,
    char *dirresult, char **nameresult, unsigned int size, int bin)
{
#ifdef HAVE_S_STUFF
    t_namelist *sp = STUFF->st_searchpath;
#else
    t_namelist *sp = 0;
#endif
    return do_open_via_path(dir, name, ext, dirresult, nameresult,
        size, bin, sp, 1);
}

/* Hilfe-Patches gibt es auf dem 3DS nicht */
WEAK void open_via_helppath(const char *name, const char *dir)
{
    (void)dir;
    post("no help patch for \"%s\" on 3DS", name);
}

/* ---------------- s_main.c ---------------- */
WEAK int sys_getblksize(void) { return DEFDACBLKSIZE; }

/* Einstellungs-Dialoge (GUI) */
WEAK void glob_start_preference_dialog(t_pd *dummy, t_symbol *s)
{ (void)dummy; (void)s; }
WEAK void glob_start_path_dialog(t_pd *dummy) { (void)dummy; }
WEAK void glob_path_dialog(t_pd *dummy, t_symbol *s, int argc, t_atom *argv)
{ (void)dummy; (void)s; (void)argc; (void)argv; }
WEAK void glob_start_startup_dialog(t_pd *dummy) { (void)dummy; }
WEAK void glob_startup_dialog(t_pd *dummy, t_symbol *s, int argc, t_atom *argv)
{ (void)dummy; (void)s; (void)argc; (void)argv; }

/* [pd add-to-path ...]: zum Suchpfad hinzufügen (wie Original, ohne Speichern) */
WEAK void glob_addtopath(t_pd *dummy, t_symbol *path, t_float saveit)
{
    (void)dummy; (void)saveit;
#ifdef HAVE_S_STUFF
    {
        char buf[MAXPDSTRING];
        sys_expandpath(path->s_name, buf, MAXPDSTRING);
        STUFF->st_searchpath = namelist_append(STUFF->st_searchpath, buf, 0);
    }
#else
    (void)path;
#endif
}

/* ---------------- x_net.c ---------------- */
/* [netsend]/[netreceive] sind auf dem 3DS-Port nicht verfügbar */
WEAK void x_net_setup(void) {}

/* ---------------- POSIX, fehlt in newlib ---------------- */
WEAK uid_t geteuid(void) { return 0; }

/* glob.h / ftw.h hier bewusst NICHT eingebunden: der Linker kennt keine
 * Typen, und so hängt die Datei nicht von deren Verfügbarkeit ab.
 * glob() meldet "kein Treffer" und fasst *pglob nicht an. */
WEAK int glob(const char *pattern, int flags,
    int (*errfunc)(const char *, int), void *pglob)
{
    (void)pattern; (void)flags; (void)errfunc; (void)pglob;
    return 3;   /* GLOB_NOMATCH (newlib/BSD: -3, glibc: 3) – nur != 0 zählt */
}
WEAK void globfree(void *pglob) { (void)pglob; }

struct FTW;
WEAK int nftw(const char *path,
    int (*fn)(const char *, const struct stat *, int, struct FTW *),
    int fd_limit, int flags)
{
    (void)path; (void)fn; (void)fd_limit; (void)flags;
    errno = ENOSYS;
    return -1;
}

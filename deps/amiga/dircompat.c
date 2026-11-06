/* dircompat.c - descriptor-style directory calls for libfilezilla on
 * AmigaOS 3. libnix's DIR has no descriptor, so each directory
 * libfilezilla opens is registered here with its path; dirfd() hands out a
 * token for it, and fstatat()/fdopendir() turn that token back into the
 * path. Also utimensat()/futimens() over libnix's utimes(), and
 * fchmod()/fchown().
 * Copyright (c) 2026 AmiCradle Team */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <proto/dos.h>
#include "fzamiga.h"

#define DIRS 64
#define TOKEN0 0x7ff00000

static struct { DIR *dir; char *path; } tab[DIRS];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

int fz_amiga_dir_register(DIR *d, const char *path)
{
    int i, r = -1;
    char *p;
    if (!d || !path || !(p = strdup(path))) return -1;
    pthread_mutex_lock(&mtx);
    for (i = 0; i < DIRS; i++)
        if (!tab[i].dir) { tab[i].dir = d; tab[i].path = p; r = TOKEN0 + i; break; }
    pthread_mutex_unlock(&mtx);
    if (r < 0) free(p);
    return r;
}

void fz_amiga_dir_forget(DIR *d)
{
    int i;
    pthread_mutex_lock(&mtx);
    for (i = 0; i < DIRS; i++)
        if (tab[i].dir == d) { free(tab[i].path); tab[i].dir = NULL; tab[i].path = NULL; }
    pthread_mutex_unlock(&mtx);
}

int dirfd(DIR *d)
{
    int i, r = -1;
    pthread_mutex_lock(&mtx);
    for (i = 0; i < DIRS; i++)
        if (d && tab[i].dir == d) { r = TOKEN0 + i; break; }
    pthread_mutex_unlock(&mtx);
    if (r < 0) errno = EINVAL;
    return r;
}

/* dir + "/" + name in buf, the AmigaDOS way: no '/' after "VOL:" or "dir/".
 * A name with a ':' already is a whole path. */
static int join(int fd, const char *name, char *buf, size_t size)
{
    size_t l;
    if (fd == AT_FDCWD || strchr(name, ':')) {
        if (strlen(name) >= size) { errno = ENAMETOOLONG; return -1; }
        strcpy(buf, name);
        return 0;
    }
    if (fd < TOKEN0 || fd >= TOKEN0 + DIRS) { errno = EBADF; return -1; }
    pthread_mutex_lock(&mtx);
    if (!tab[fd - TOKEN0].path) { pthread_mutex_unlock(&mtx); errno = EBADF; return -1; }
    l = strlen(tab[fd - TOKEN0].path);
    if (l + strlen(name) + 2 > size) { pthread_mutex_unlock(&mtx); errno = ENAMETOOLONG; return -1; }
    strcpy(buf, tab[fd - TOKEN0].path);
    pthread_mutex_unlock(&mtx);
    if (l && buf[l - 1] != ':' && buf[l - 1] != '/') buf[l++] = '/';
    strcpy(buf + l, name);
    return 0;
}

DIR *fdopendir(int fd)
{
    char path[1024];
    DIR *d;
    if (fd < TOKEN0 || fd >= TOKEN0 + DIRS) { errno = EBADF; return NULL; }
    if (join(fd, "", path, sizeof path)) return NULL;
    if (!(d = opendir(path))) return NULL;
    /* the token now stands for the new DIR */
    pthread_mutex_lock(&mtx);
    tab[fd - TOKEN0].dir = d;
    pthread_mutex_unlock(&mtx);
    return d;
}

int fstatat(int fd, const char *name, struct stat *st, int flags)
{
    char path[1024];
    if (join(fd, name, path, sizeof path)) return -1;
    return (flags & AT_SYMLINK_NOFOLLOW) ? lstat(path, st) : stat(path, st);
}

__stdargs int utimensat(int fd, const char *name, const struct timespec *t, int flags)
{
    char path[1024];
    struct timeval tv[2];
    (void)flags;
    if (join(fd, name, path, sizeof path)) return -1;
    if (!t || t[1].tv_nsec == UTIME_NOW) return utimes(path, NULL);
    if (t[1].tv_nsec == UTIME_OMIT) return 0;   /* AmigaDOS keeps one date: the change */
    tv[1].tv_sec = t[1].tv_sec; tv[1].tv_usec = t[1].tv_nsec / 1000;
    tv[0] = tv[1];
    return utimes(path, tv);
}

__stdargs int futimens(int fd, const struct timespec *t)
{
    char path[1024];
    StdFileDes *sfd = _lx_fhfromfd(fd);
    if (!sfd || !sfd->lx_fh) { errno = EBADF; return -1; }
    if (!NameFromFH((BPTR)sfd->lx_fh, (STRPTR)path, sizeof path)) { errno = EIO; return -1; }
    return utimensat(AT_FDCWD, path, t, 0);
}

/* SQLite's unix VFS: AmigaDOS has no owners, and the mode goes through the
 * file's name. */
int fchmod(int fd, mode_t mode)
{
    char path[1024];
    StdFileDes *sfd = _lx_fhfromfd(fd);
    if (!sfd || !sfd->lx_fh) { errno = EBADF; return -1; }
    if (!NameFromFH((BPTR)sfd->lx_fh, (STRPTR)path, sizeof path)) { errno = EIO; return -1; }
    return chmod(path, mode);
}

int fchown(int fd, uid_t owner, gid_t group)
{
    (void)owner; (void)group;
    if (!_lx_fhfromfd(fd)) { errno = EBADF; return -1; }
    return 0;
}

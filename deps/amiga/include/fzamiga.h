/* fzamiga.h - the POSIX calls libfilezilla, fzssh and FileZilla use that
 * the stove's libnix and socket glue lack (deps/amiga, libfzamiga.a).
 * Taken in with -include, so it only declares what is missing.
 * Copyright (c) 2026 AmiCradle Team */
#ifndef FZAMIGA_H
#define FZAMIGA_H
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <wchar.h>
#include "fzdecl.h"
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
/* pthread.h's cancellation-point wrappers are function-like macros
 * (read(...) and the like) that break "::read" and members named read or
 * close, as the GFOpenWX Toolkit's amigacompat.h says; nothing here is
 * ever cancelled, so they go. */
#undef close
#undef fsync
#undef read
#undef write
#undef sleep
#undef usleep
#undef creat
#undef fcntl
#undef open
#undef nanosleep
#undef readv
#undef writev
/* exec/lists.h (through the socket headers) has function-like macros named
 * like the toolkit's wxComboBox::IsListEmpty() and the like; nothing here
 * uses them. */
#undef IsListEmpty
#undef IsMsgPortEmpty
#ifdef __cplusplus
extern "C" {
#endif
int getentropy(void *buffer, size_t length);
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int code);
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int socketpair(int domain, int type, int protocol, int sv[2]);
time_t timegm(struct tm *tm);
const char *getprogname(void);
size_t wcsnlen(const wchar_t *s, size_t maxlen);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);
size_t mbsnrtowcs(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps);
size_t wcsnrtombs(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps);
int wcsncasecmp(const wchar_t *a, const wchar_t *b, size_t n);
int wcscasecmp(const wchar_t *a, const wchar_t *b);
/* libnix has these; the headers show them only to POSIX 2008 or GNU source */
int ftruncate(int fd, off_t length);
char *realpath(const char *path, char *resolved);
ssize_t readlink(const char *path, char *buf, size_t size);
/* dircompat.c: directories by token (libnix's DIR has no descriptor) */
int fz_amiga_dir_register(DIR *d, const char *path);
void fz_amiga_dir_forget(DIR *d);
int dirfd(DIR *d);
DIR *fdopendir(int fd);
int fstatat(int fd, const char *name, struct stat *st, int flags);
__stdargs int utimensat(int fd, const char *name, const struct timespec *t, int flags);
__stdargs int futimens(int fd, const struct timespec *t);
/* dosrun.c */
int fz_amiga_run_detached(const char *cmdline);
int fz_amiga_progdir(char *buf, int len);
#ifndef AT_FDCWD
#define AT_FDCWD -2
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 2
#endif
#ifndef UTIME_NOW
#define UTIME_NOW -2L
#define UTIME_OMIT -1L
#endif
/* nothing is exec()ed on AmigaOS: no descriptor to close on exec */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
/* bsdsocket.library speaks IPv4 only; these let IPv6 code build and fail
 * at run time as on a host without IPv6 */
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#ifndef _SS_SIZE
#define _SS_SIZE 128
struct sockaddr_storage {
    union {
        struct sockaddr __ss_sa;
        struct sockaddr_in __ss_in;
        struct sockaddr_in6 __ss_in6;
        char __ss_pad[_SS_SIZE];
    };
};
#define ss_family __ss_sa.sa_family
#endif
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_NAMEREQD 4
#define NI_DGRAM 8
#define NI_NOFQDN 16
#endif
#ifndef EAI_NODATA
#define EAI_NODATA EAI_NONAME
#endif
#ifndef EAI_ADDRFAMILY
#define EAI_ADDRFAMILY EAI_FAMILY
#endif
#ifndef INET6_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#endif
#ifdef __cplusplus
}
#endif
#endif

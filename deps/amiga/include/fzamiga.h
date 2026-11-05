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
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
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
size_t wcsnlen(const wchar_t *s, size_t maxlen);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);
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

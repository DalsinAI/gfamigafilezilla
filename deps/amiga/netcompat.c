/* netcompat.c - name lookup and address text for the bsdsocket glue, which
 * has gethostbyname() but none of getaddrinfo(), getnameinfo(),
 * inet_pton(), inet_ntop() or socketpair().
 *
 * Lookups are IPv4 (what the Amiga stacks resolve); gethostbyname() keeps
 * one static answer, so lookups take a lock. inet_pton/inet_ntop read and
 * write IPv6 text too, so literal IPv6 addresses parse. socketpair() is two
 * TCP sockets joined over 127.0.0.1, which the waits in libfilezilla can
 * poll like any other socket.
 *
 * Copyright (c) 2026 AmiCradle Team */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "fzamiga.h"

static pthread_mutex_t lookup_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- address text */
static int pton4(const char *s, unsigned char *d)
{
    int n = 0;
    while (n < 4) {
        unsigned v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned)(*s++ - '0'); if (++digits > 3 || v > 255) return 0; }
        if (!digits) return 0;
        d[n++] = (unsigned char)v;
        if (n < 4) { if (*s++ != '.') return 0; }
    }
    return *s == 0;
}

static int pton6(const char *s, unsigned char *d)
{
    unsigned short w[8]; int n = 0, gap = -1, i;
    if (s[0] == ':' && s[1] == ':') { gap = 0; s += 2; }
    while (*s && n < 8) {
        const char *start = s; unsigned v = 0; int digits = 0;
        if (strchr(s, '.') && !strchr(s, ':')) {      /* embedded IPv4 tail */
            unsigned char b[4];
            if (n > 6 || !pton4(s, b)) return 0;
            w[n++] = (unsigned short)(b[0] << 8 | b[1]); w[n++] = (unsigned short)(b[2] << 8 | b[3]);
            s += strlen(s); break;
        }
        while (isxdigit((unsigned char)*s)) {
            v = v * 16 + (unsigned)(isdigit((unsigned char)*s) ? *s - '0' : (tolower((unsigned char)*s) - 'a' + 10));
            if (++digits > 4) return 0;
            s++;
        }
        if (!digits) return 0;
        w[n++] = (unsigned short)v;
        if (*s == ':') {
            s++;
            if (*s == ':') { if (gap >= 0) return 0; gap = n; s++; }
            else if (!*s) return 0;
        } else if (*s) return 0;
        (void)start;
    }
    if (*s) return 0;
    if (gap >= 0) {
        int move = n - gap;
        if (n == 8) return 0;
        for (i = 0; i < move; i++) w[7 - i] = w[n - 1 - i];
        for (i = gap; i < 8 - move; i++) w[i] = 0;
    } else if (n != 8) return 0;
    for (i = 0; i < 8; i++) { d[2*i] = (unsigned char)(w[i] >> 8); d[2*i+1] = (unsigned char)w[i]; }
    return 1;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af == AF_INET) return pton4(src, dst);
    if (af == AF_INET6) return pton6(src, dst);
    errno = EAFNOSUPPORT; return -1;
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    char buf[INET6_ADDRSTRLEN]; const unsigned char *a = src;
    if (af == AF_INET) {
        snprintf(buf, sizeof buf, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    } else if (af == AF_INET6) {
        unsigned w[8]; int i, best = -1, bestlen = 0, cur = -1, curlen = 0; char *p = buf;
        for (i = 0; i < 8; i++) w[i] = (unsigned)a[2*i] << 8 | a[2*i+1];
        for (i = 0; i < 8; i++) {            /* the longest run of zero words */
            if (!w[i]) { if (cur < 0) { cur = i; curlen = 0; } if (++curlen > bestlen) { best = cur; bestlen = curlen; } }
            else cur = -1;
        }
        if (bestlen < 2) best = -1;
        for (i = 0; i < 8; i++) {
            if (i == best) { *p++ = ':'; if (i == 0) *p++ = ':'; i += bestlen - 1; continue; }
            p += sprintf(p, "%x%s", w[i], i < 7 ? ":" : "");
        }
        *p = 0;
    } else { errno = EAFNOSUPPORT; return NULL; }
    if (strlen(buf) >= (size_t)size) { errno = ENOSPC; return NULL; }
    strcpy(dst, buf);
    return dst;
}

/* ---- getaddrinfo and friends */
static struct addrinfo *one(int family, int socktype, int protocol, const void *addr, unsigned short port)
{
    struct addrinfo *ai = calloc(1, sizeof *ai + sizeof(struct sockaddr_in6));
    struct sockaddr *sa;
    if (!ai) return NULL;
    sa = (struct sockaddr *)(ai + 1);
    ai->ai_family = family; ai->ai_socktype = socktype; ai->ai_protocol = protocol; ai->ai_addr = sa;
    if (family == AF_INET) {
        struct sockaddr_in *s4 = (struct sockaddr_in *)sa;
        s4->sin_family = AF_INET; s4->sin_port = htons(port);
        memcpy(&s4->sin_addr, addr, 4); ai->ai_addrlen = sizeof *s4;
    } else {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)sa;
        s6->sin6_family = AF_INET6; s6->sin6_port = htons(port);
        memcpy(&s6->sin6_addr, addr, 16); ai->ai_addrlen = sizeof *s6;
    }
    return ai;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    int family = hints ? hints->ai_family : AF_UNSPEC, flags = hints ? hints->ai_flags : 0;
    int socktype = hints ? hints->ai_socktype : 0, protocol = hints ? hints->ai_protocol : 0;
    unsigned long port = 0; unsigned char a[16];
    struct addrinfo *head = NULL, **tail = &head;
    *res = NULL;
    if (!node && !service) return EAI_NONAME;
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return EAI_FAMILY;
    if (service && *service) {
        char *end; port = strtoul(service, &end, 10);
        if (*end || port > 65535) {
            struct servent *se;
            if (flags & AI_NUMERICSERV) return EAI_NONAME;
            pthread_mutex_lock(&lookup_lock);
            se = getservbyname(service, socktype == SOCK_DGRAM ? "udp" : "tcp");
            port = se ? ntohs((unsigned short)se->s_port) : 0;
            pthread_mutex_unlock(&lookup_lock);
            if (!se) return EAI_SERVICE;
        }
    }
    if (!node) {                     /* the wildcard or the loopback address */
        unsigned long any = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
        if (family == AF_INET6) return EAI_FAMILY;
        *res = one(AF_INET, socktype, protocol, &any, (unsigned short)port);
        return *res ? 0 : EAI_MEMORY;
    }
    if (pton4(node, a)) {
        if (family == AF_INET6) return EAI_ADDRFAMILY;
        *res = one(AF_INET, socktype, protocol, a, (unsigned short)port);
        return *res ? 0 : EAI_MEMORY;
    }
    if (pton6(node, a)) {
        if (family == AF_INET) return EAI_ADDRFAMILY;
        *res = one(AF_INET6, socktype, protocol, a, (unsigned short)port);
        return *res ? 0 : EAI_MEMORY;
    }
    if (flags & AI_NUMERICHOST) return EAI_NONAME;
    if (family == AF_INET6) return EAI_NODATA;  /* the stacks resolve IPv4 only */
    {
        struct hostent *he; int i, err = 0;
        pthread_mutex_lock(&lookup_lock);
        he = gethostbyname(node);
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) err = EAI_NONAME;
        else
            for (i = 0; he->h_addr_list[i]; i++) {
                struct addrinfo *ai = one(AF_INET, socktype, protocol, he->h_addr_list[i], (unsigned short)port);
                if (!ai) { err = EAI_MEMORY; break; }
                if (i == 0 && (flags & AI_CANONNAME) && he->h_name) ai->ai_canonname = strdup(he->h_name);
                *tail = ai; tail = &ai->ai_next;
            }
        pthread_mutex_unlock(&lookup_lock);
        if (err) { freeaddrinfo(head); return err; }
    }
    *res = head;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res) { struct addrinfo *n = res->ai_next; free(res->ai_canonname); free(res); res = n; }
}

const char *gai_strerror(int code)
{
    switch (code) {
    case 0: return "Success";
    case EAI_AGAIN: return "Temporary failure in name resolution";
    case EAI_FAIL: return "Non-recoverable failure in name resolution";
    case EAI_FAMILY: return "Address family not supported";
    case EAI_MEMORY: return "Out of memory";
    case EAI_NODATA: return "No address associated with the name";
    case EAI_NONAME: return "Name or service not known";
    case EAI_SERVICE: return "Service not supported for the socket type";
    case EAI_SOCKTYPE: return "Socket type not supported";
    case EAI_ADDRFAMILY: return "Address family for the name not supported";
    default: return "Name lookup failed";
    }
}

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags)
{
    const void *addr; unsigned short port;
    (void)salen;
    if (sa->sa_family == AF_INET) { addr = &((const struct sockaddr_in *)sa)->sin_addr; port = ((const struct sockaddr_in *)sa)->sin_port; }
    else if (sa->sa_family == AF_INET6) { addr = &((const struct sockaddr_in6 *)sa)->sin6_addr; port = ((const struct sockaddr_in6 *)sa)->sin6_port; }
    else return EAI_FAMILY;
    if (host && hostlen) {
        int done = 0;
        if (!(flags & NI_NUMERICHOST) && sa->sa_family == AF_INET) {
            struct hostent *he;
            pthread_mutex_lock(&lookup_lock);
            he = gethostbyaddr(addr, 4, AF_INET);
            if (he && he->h_name && strlen(he->h_name) < (size_t)hostlen) { strcpy(host, he->h_name); done = 1; }
            pthread_mutex_unlock(&lookup_lock);
        }
        if (!done) {
            if (flags & NI_NAMEREQD) return EAI_NONAME;
            if (!inet_ntop(sa->sa_family, addr, host, hostlen)) return EAI_FAMILY;
        }
    }
    if (serv && servlen) {
        if ((unsigned)snprintf(serv, servlen, "%u", ntohs(port)) >= (unsigned)servlen) return EAI_MEMORY;
    }
    return 0;
}

/* ---- socketpair: two TCP sockets joined over the loopback */
int socketpair(int domain, int type, int protocol, int sv[2])
{
    struct sockaddr_in a; socklen_t len = sizeof a; int l, c = -1, s = -1, one_ = 1;
    (void)domain; (void)protocol;
    if ((type & 0xff) != SOCK_STREAM) { errno = EOPNOTSUPP; return -1; }
    if ((l = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    if (bind(l, (struct sockaddr *)&a, sizeof a) < 0 || listen(l, 1) < 0 || getsockname(l, (struct sockaddr *)&a, &len) < 0)
        goto fail;
    if ((c = socket(AF_INET, SOCK_STREAM, 0)) < 0 || connect(c, (struct sockaddr *)&a, sizeof a) < 0) goto fail;
    len = sizeof a;
    if ((s = accept(l, (struct sockaddr *)&a, &len)) < 0) goto fail;
    close(l);
    setsockopt(c, 6 /* IPPROTO_TCP */, 1 /* TCP_NODELAY */, &one_, sizeof one_);
    setsockopt(s, 6, 1, &one_, sizeof one_);
    sv[0] = c; sv[1] = s;
    return 0;
fail:
    { int e = errno; close(l); if (c >= 0) close(c); errno = e; }
    return -1;
}

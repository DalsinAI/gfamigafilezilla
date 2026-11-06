/* iconv.c - our own small iconv for AmigaOS 3 (libnix has none).
 * libfilezilla only converts between UTF-8 and wchar_t; this covers that plus
 * UTF-16/32 in either byte order, ISO-8859-1 and ASCII. Stateless, so one
 * descriptor may be used by one thread at a time, as POSIX says.
 * Copyright (c) 2026 AmiCradle Team */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <iconv.h>

enum { E_UTF8 = 1, E_U16BE, E_U16LE, E_U32BE, E_U32LE, E_LATIN1, E_ASCII };

struct fz_iconv { int from, to; };

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
    return !*a && !*b;
}

static int enc(const char *name)
{
    char n[32]; size_t i = 0;
    if (!name) return 0;
    /* drop //TRANSLIT, //IGNORE and the like */
    while (name[i] && i < sizeof n - 1 && !(name[i] == '/' && name[i + 1] == '/')) { n[i] = name[i]; i++; }
    n[i] = 0;
    if (same(n, "UTF-8") || same(n, "UTF8") || !n[0]) return E_UTF8;
    if (same(n, "UTF-16BE") || same(n, "UTF-16") || same(n, "UCS-2BE") || same(n, "UCS-2")) return E_U16BE;
    if (same(n, "UTF-16LE") || same(n, "UCS-2LE")) return E_U16LE;
    if (same(n, "UTF-32BE") || same(n, "UTF-32") || same(n, "UCS-4BE") || same(n, "UCS-4")) return E_U32BE;
    if (same(n, "UTF-32LE") || same(n, "UCS-4LE")) return E_U32LE;
    if (same(n, "WCHAR_T")) return sizeof(wchar_t) == 2 ? E_U16BE : E_U32BE;
    if (same(n, "ISO-8859-1") || same(n, "ISO8859-1") || same(n, "LATIN1") || same(n, "ISO_8859-1")) return E_LATIN1;
    if (same(n, "ASCII") || same(n, "US-ASCII") || same(n, "ANSI_X3.4-1968")) return E_ASCII;
    return 0;
}

iconv_t iconv_open(const char *tocode, const char *fromcode)
{
    struct fz_iconv *cd;
    int t = enc(tocode), f = enc(fromcode);
    if (!t || !f) { errno = EINVAL; return (iconv_t)-1; }
    if (!(cd = malloc(sizeof *cd))) { errno = ENOMEM; return (iconv_t)-1; }
    cd->from = f; cd->to = t;
    return cd;
}

int iconv_close(iconv_t cd)
{
    if (cd == (iconv_t)-1 || !cd) { errno = EBADF; return -1; }
    free(cd);
    return 0;
}

/* One code point from s (n bytes). Returns bytes used, 0 if more input is
 * needed (EINVAL), -1 on a bad sequence (EILSEQ). */
static int get(int e, const unsigned char *s, size_t n, unsigned long *cp)
{
    unsigned long c;
    int len, i;
    switch (e) {
    case E_UTF8:
        c = s[0];
        if (c < 0x80) { *cp = c; return 1; }
        if (c < 0xc2) return -1;
        len = c < 0xe0 ? 2 : c < 0xf0 ? 3 : c < 0xf5 ? 4 : 0;
        if (!len) return -1;
        c &= 0x3f >> (len - 1);
        for (i = 1; i < len; i++) {
            if ((size_t)i >= n) return 0;
            if ((s[i] & 0xc0) != 0x80) return -1;
            c = (c << 6) | (s[i] & 0x3f);
        }
        if ((len == 3 && c < 0x800) || (len == 4 && (c < 0x10000 || c > 0x10ffff))
            || (c >= 0xd800 && c <= 0xdfff)) return -1;
        *cp = c; return len;
    case E_U16BE: case E_U16LE: {
        unsigned long lo;
        if (n < 2) return 0;
        c = e == E_U16BE ? (unsigned long)s[0] << 8 | s[1] : (unsigned long)s[1] << 8 | s[0];
        if (c >= 0xdc00 && c <= 0xdfff) return -1;
        if (c < 0xd800 || c > 0xdbff) { *cp = c; return 2; }
        if (n < 4) return 0;
        lo = e == E_U16BE ? (unsigned long)s[2] << 8 | s[3] : (unsigned long)s[3] << 8 | s[2];
        if (lo < 0xdc00 || lo > 0xdfff) return -1;
        *cp = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00); return 4; }
    case E_U32BE: case E_U32LE:
        if (n < 4) return 0;
        c = e == E_U32BE ? (unsigned long)s[0] << 24 | (unsigned long)s[1] << 16 | (unsigned long)s[2] << 8 | s[3]
                         : (unsigned long)s[3] << 24 | (unsigned long)s[2] << 16 | (unsigned long)s[1] << 8 | s[0];
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return -1;
        *cp = c; return 4;
    case E_LATIN1:
        *cp = s[0]; return 1;
    default: /* ASCII */
        if (s[0] > 0x7f) return -1;
        *cp = s[0]; return 1;
    }
}

/* Writes cp to d (room n). Returns bytes written, 0 if no room (E2BIG), -1 if
 * the code point has no form in this encoding (EILSEQ). */
static int put(int e, unsigned long c, unsigned char *d, size_t n)
{
    switch (e) {
    case E_UTF8:
        if (c < 0x80) { if (n < 1) return 0; d[0] = c; return 1; }
        if (c < 0x800) { if (n < 2) return 0; d[0] = 0xc0 | c >> 6; d[1] = 0x80 | (c & 0x3f); return 2; }
        if (c < 0x10000) { if (n < 3) return 0; d[0] = 0xe0 | c >> 12; d[1] = 0x80 | (c >> 6 & 0x3f);
            d[2] = 0x80 | (c & 0x3f); return 3; }
        if (n < 4) return 0;
        d[0] = 0xf0 | c >> 18; d[1] = 0x80 | (c >> 12 & 0x3f); d[2] = 0x80 | (c >> 6 & 0x3f); d[3] = 0x80 | (c & 0x3f);
        return 4;
    case E_U16BE: case E_U16LE: {
        unsigned long u[2]; int k = 1, i;
        if (c < 0x10000) u[0] = c;
        else { c -= 0x10000; u[0] = 0xd800 | c >> 10; u[1] = 0xdc00 | (c & 0x3ff); k = 2; }
        if (n < (size_t)k * 2) return 0;
        for (i = 0; i < k; i++) {
            d[2 * i + (e == E_U16LE)] = u[i] >> 8; d[2 * i + (e != E_U16LE)] = u[i] & 0xff;
        }
        return k * 2; }
    case E_U32BE: case E_U32LE:
        if (n < 4) return 0;
        if (e == E_U32BE) { d[0] = c >> 24; d[1] = c >> 16; d[2] = c >> 8; d[3] = c; }
        else { d[3] = c >> 24; d[2] = c >> 16; d[1] = c >> 8; d[0] = c; }
        return 4;
    case E_LATIN1:
        if (c > 0xff) return -1;
        if (n < 1) return 0;
        d[0] = c; return 1;
    default:
        if (c > 0x7f) return -1;
        if (n < 1) return 0;
        d[0] = c; return 1;
    }
}

size_t iconv(iconv_t h, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft)
{
    struct fz_iconv *cd = h;
    if (cd == (iconv_t)-1 || !cd) { errno = EBADF; return (size_t)-1; }
    if (!inbuf || !*inbuf) return 0; /* stateless: nothing to reset or flush */
    while (*inleft) {
        unsigned long c;
        int r = get(cd->from, (const unsigned char *)*inbuf, *inleft, &c), w;
        if (r == 0) { errno = EINVAL; return (size_t)-1; }
        if (r < 0) { errno = EILSEQ; return (size_t)-1; }
        if (!outbuf || !*outbuf) { errno = E2BIG; return (size_t)-1; }
        w = put(cd->to, c, (unsigned char *)*outbuf, *outleft);
        if (w == 0) { errno = E2BIG; return (size_t)-1; }
        if (w < 0) { errno = EILSEQ; return (size_t)-1; }
        *inbuf += r; *inleft -= r; *outbuf += w; *outleft -= w;
    }
    return 0;
}

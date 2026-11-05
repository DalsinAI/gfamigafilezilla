/* libccompat.c - the C library calls libfilezilla and FileZilla use that
 * libnix lacks: timegm(), wcsnlen(), getprogname() and the restartable UTF-8 conversions
 * mbsrtowcs()/wcsrtombs() (the programs run their strings as UTF-8).
 * Copyright (c) 2026 AmiCradle Team */
#include <errno.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include "fzamiga.h"

time_t timegm(struct tm *tm)
{
    static const int mdays[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    long long y = tm->tm_year + 1900LL, m = tm->tm_mon, days;
    y += m / 12; m %= 12; if (m < 0) { m += 12; y--; }
    days = (y - 1970) * 365 + ((y - 1969) / 4) - ((y - 1901) / 100) + ((y - 1601) / 400)
         + mdays[m] + tm->tm_mday - 1;
    if (m > 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
    return (time_t)(((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec);
}

size_t wcsnlen(const wchar_t *s, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

/* one UTF-8 sequence to a code point: its length, 0 at the end, -1 bad */
static int utf8_in(const unsigned char *s, unsigned long *cp)
{
    unsigned long c = s[0]; int n, i;
    if (c < 0x80) { *cp = c; return c ? 1 : 0; }
    if ((c & 0xe0) == 0xc0) { n = 2; c &= 0x1f; }
    else if ((c & 0xf0) == 0xe0) { n = 3; c &= 0x0f; }
    else if ((c & 0xf8) == 0xf0) { n = 4; c &= 0x07; }
    else return -1;
    for (i = 1; i < n; i++) { if ((s[i] & 0xc0) != 0x80) return -1; c = c << 6 | (s[i] & 0x3f); }
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) || (n == 2 && c < 0x80) || (n == 3 && c < 0x800) || (n == 4 && c < 0x10000)) return -1;
    *cp = c; return n;
}

size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps)
{
    const unsigned char *s = (const unsigned char *)*src; size_t n = 0;
    (void)ps;
    while (!dst || n < len) {
        unsigned long cp; int k = utf8_in(s, &cp);
        if (k < 0) { errno = EILSEQ; if (dst) *src = (const char *)s; return (size_t)-1; }
        if (k == 0) { if (dst) { dst[n] = 0; *src = NULL; } return n; }
        if (dst) dst[n] = (wchar_t)cp;
        n++; s += k;
    }
    *src = (const char *)s;
    return n;
}

size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps)
{
    const wchar_t *w = *src; size_t n = 0;
    (void)ps;
    for (;; w++) {
        unsigned long c = (unsigned long)*w; unsigned char b[4]; int k;
        if (c < 0x80) { b[0] = (unsigned char)c; k = 1; }
        else if (c < 0x800) { b[0] = (unsigned char)(0xc0 | c >> 6); b[1] = (unsigned char)(0x80 | (c & 0x3f)); k = 2; }
        else if (c < 0x10000) {
            if (c >= 0xd800 && c <= 0xdfff) { errno = EILSEQ; if (dst) *src = w; return (size_t)-1; }
            b[0] = (unsigned char)(0xe0 | c >> 12); b[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3f)); b[2] = (unsigned char)(0x80 | (c & 0x3f)); k = 3; }
        else if (c <= 0x10ffff) {
            b[0] = (unsigned char)(0xf0 | c >> 18); b[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3f));
            b[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3f)); b[3] = (unsigned char)(0x80 | (c & 0x3f)); k = 4; }
        else { errno = EILSEQ; if (dst) *src = w; return (size_t)-1; }
        if (dst) {
            if (n + (size_t)k > len) { *src = w; return n; }
            memcpy(dst + n, b, (size_t)k);
        }
        if (c == 0) { if (dst) *src = NULL; return n; }
        n += (size_t)k;
    }
}

/* the program's name: its CLI command name when run from a shell, else its
 * task's name */
const char *getprogname(void)
{
    static char name[64];
    struct Process *me = (struct Process *)FindTask(NULL);
    if (!name[0]) {
        const char *n = me->pr_Task.tc_Node.ln_Name;
        if (me->pr_CLI) {
            struct CommandLineInterface *cli = (struct CommandLineInterface *)BADDR(me->pr_CLI);
            const unsigned char *b = (const unsigned char *)BADDR(cli->cli_CommandName);
            if (b && b[0]) {
                size_t k = b[0] < sizeof name - 1 ? b[0] : sizeof name - 1;
                memcpy(name, b + 1, k); name[k] = 0;
            }
        }
        if (!name[0] && n) { strncpy(name, n, sizeof name - 1); }
        if (!name[0]) strcpy(name, "FileZilla");
    }
    return name;
}

/* idna.c - our own small IDNA (see include/idna.h): RFC 3492 Punycode for
 * each label that has letters beyond ASCII.
 * Copyright (c) 2026 AmiCradle Team */
#include <stdlib.h>
#include <string.h>
#include "idna.h"
#include "idn-free.h"

enum { BASE = 36, TMIN = 1, TMAX = 26, SKEW = 38, DAMP = 700, IBIAS = 72, INITN = 128 };

static unsigned long adapt(unsigned long delta, unsigned long points, int first)
{
    unsigned long k = 0;
    delta = first ? delta / DAMP : delta / 2;
    delta += delta / points;
    while (delta > ((BASE - TMIN) * TMAX) / 2) { delta /= BASE - TMIN; k += BASE; }
    return k + (BASE - TMIN + 1) * delta / (delta + SKEW);
}

static char digit(unsigned long d) { return (char)(d < 26 ? 'a' + d : '0' + d - 26); }

/* code points cp[0..n) to Punycode in out (room size); its length, or -1 */
static int punycode(const unsigned long *cp, size_t n, char *out, size_t size)
{
    unsigned long m, cur = INITN, delta = 0, bias = IBIAS;
    size_t h, b = 0, o = 0, j;
    for (j = 0; j < n; j++) if (cp[j] < 0x80) { if (o >= size) return -1; out[o++] = (char)cp[j]; b++; }
    h = b;
    if (b && o < size) out[o++] = '-'; else if (b) return -1;
    while (h < n) {
        m = (unsigned long)-1;
        for (j = 0; j < n; j++) if (cp[j] >= cur && cp[j] < m) m = cp[j];
        if ((m - cur) > (0xffffffffUL - delta) / (h + 1)) return -1;
        delta += (m - cur) * (h + 1);
        cur = m;
        for (j = 0; j < n; j++) {
            if (cp[j] < cur && ++delta == 0) return -1;
            if (cp[j] == cur) {
                unsigned long q = delta, k;
                for (k = BASE;; k += BASE) {
                    unsigned long t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
                    if (q < t) break;
                    if (o >= size) return -1;
                    out[o++] = digit(t + (q - t) % (BASE - t));
                    q = (q - t) / (BASE - t);
                }
                if (o >= size) return -1;
                out[o++] = digit(q);
                bias = adapt(delta, h + 1, h == b);
                delta = 0;
                h++;
            }
        }
        delta++; cur++;
    }
    return (int)o;
}

/* one UTF-8 sequence; its length, or -1 */
static int utf8(const unsigned char *s, unsigned long *c)
{
    int n, i;
    if (s[0] < 0x80) { *c = s[0]; return 1; }
    n = (s[0] & 0xe0) == 0xc0 ? 2 : (s[0] & 0xf0) == 0xe0 ? 3 : (s[0] & 0xf8) == 0xf0 ? 4 : 0;
    if (!n) return -1;
    *c = s[0] & (0x7f >> n);
    for (i = 1; i < n; i++) { if ((s[i] & 0xc0) != 0x80) return -1; *c = *c << 6 | (s[i] & 0x3f); }
    return n;
}

int idna_to_ascii_8z(const char *input, char **output, int flags)
{
    size_t len = strlen(input), o = 0, size = len * 8 + 16;
    const unsigned char *s = (const unsigned char *)input;
    char *out = malloc(size);
    unsigned long *cp = malloc((len + 1) * sizeof *cp);
    (void)flags;
    if (!out || !cp) { free(out); free(cp); return IDNA_MALLOC_ERROR; }
    while (*s) {
        size_t n = 0, start = o; int ascii = 1, sep = 0, k;
        /* a label: up to '.', or the ideographic and fullwidth full stops */
        while (*s) {
            unsigned long c;
            k = utf8(s, &c);
            if (k < 0) { free(out); free(cp); return IDNA_PUNYCODE_ERROR; }
            s += k;
            if (c == '.' || c == 0x3002 || c == 0xff0e || c == 0xff61) { sep = 1; break; }
            if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) c += 32;
            if (c >= 0x80) ascii = 0;
            cp[n++] = c;
        }
        if (ascii) {
            size_t j;
            for (j = 0; j < n; j++) out[o++] = (char)cp[j];
        } else {
            memcpy(out + o, "xn--", 4); o += 4;
            k = punycode(cp, n, out + o, size - o - 2);
            if (k < 0) { free(out); free(cp); return IDNA_PUNYCODE_ERROR; }
            o += (size_t)k;
        }
        if (o - start > 63) { free(out); free(cp); return IDNA_INVALID_LENGTH; }
        if (sep) out[o++] = '.';
    }
    out[o] = 0;
    free(cp);
    *output = out;
    return IDNA_SUCCESS;
}

int stringprep_check_version(void) { return 1; }

void idn_free(void *ptr) { free(ptr); }

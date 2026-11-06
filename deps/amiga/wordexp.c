/* wordexp.c - the shell-word expansion FileZilla uses to read paths from
 * user-dirs.dirs: quotes, backslashes, ~ and $NAME / ${NAME} from the
 * environment, split on blanks. No commands (WRDE_NOCMD is how it is
 * always called; $( and ` give WRDE_CMDSUB), no globbing, no arithmetic.
 * Also munmap(), which libfilezilla names but never reaches here: it maps
 * nothing on the Amiga.
 * Copyright (c) 2026 AmiCradle Team */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

struct sb { char *p; size_t n, cap; };

static int put(struct sb *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap * 2 : 64;
        char *q;
        while (c < b->n + n + 1) c *= 2;
        if (!(q = realloc(b->p, c))) return -1;
        b->p = q; b->cap = c;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
    return 0;
}

static int add_word(wordexp_t *w, struct sb *b)
{
    char **v = realloc(w->we_wordv, (w->we_offs + w->we_wordc + 2) * sizeof *v);
    if (!v) return -1;
    w->we_wordv = v;
    v[w->we_offs + w->we_wordc++] = b->p ? b->p : strdup("");
    v[w->we_offs + w->we_wordc] = NULL;
    b->p = NULL; b->n = b->cap = 0;
    return v[w->we_offs + w->we_wordc - 1] ? 0 : -1;
}

static int is_name(int c, int first)
{
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (!first && c >= '0' && c <= '9');
}

/* $NAME or ${NAME} at *s; moves *s past it */
static int var(const char **s, struct sb *b, int flags)
{
    const char *p = *s + 1, *start, *val;
    char name[128];
    size_t n;
    int brace = *p == '{';
    if (brace) p++;
    start = p;
    if (*p == '(' && !brace) return WRDE_CMDSUB;
    while (is_name((unsigned char)*p, p == start)) p++;
    n = (size_t)(p - start);
    if (brace) { if (*p != '}') return WRDE_SYNTAX; p++; }
    if (!n) { *s = *s + 1; return put(b, "$", 1) ? WRDE_NOSPACE : 0; }
    if (n >= sizeof name) n = sizeof name - 1;
    memcpy(name, start, n); name[n] = 0;
    val = getenv(name);
    if (!val && (flags & WRDE_UNDEF)) return WRDE_BADVAL;
    *s = p;
    return val && put(b, val, strlen(val)) ? WRDE_NOSPACE : 0;
}

int wordexp(const char *s, wordexp_t *w, int flags)
{
    struct sb b = { 0, 0, 0 };
    int inword = 0, r;
    char q = 0;
    if (!(flags & WRDE_APPEND)) {
        w->we_wordc = 0; w->we_wordv = NULL;
        if (!(flags & WRDE_DOOFFS)) w->we_offs = 0;
        if (w->we_offs) {
            size_t i;
            if (!(w->we_wordv = calloc(w->we_offs + 1, sizeof *w->we_wordv))) return WRDE_NOSPACE;
            for (i = 0; i < w->we_offs; i++) w->we_wordv[i] = NULL;
        }
    }
    for (; *s; ) {
        char c = *s;
        if (!q && (c == ' ' || c == '\t' || c == '\n')) {
            if (inword && add_word(w, &b)) goto nospace;
            inword = 0; s++; continue;
        }
        if (!q && strchr("|&;<>(){}", c)) { r = WRDE_BADCHAR; goto fail; }
        if (c == '`') { r = WRDE_CMDSUB; goto fail; }
        inword = 1;
        if (q == '\'') {
            if (c == '\'') q = 0; else if (put(&b, s, 1)) goto nospace;
            s++; continue;
        }
        if (c == '\\') {
            if (!s[1]) { r = WRDE_SYNTAX; goto fail; }
            if (q == '"' && !strchr("$`\"\\\n", s[1])) { if (put(&b, s, 2)) goto nospace; }
            else if (s[1] != '\n' && put(&b, s + 1, 1)) goto nospace;
            s += 2; continue;
        }
        if (c == '"') { q = q ? 0 : '"'; s++; continue; }
        if (c == '\'' && !q) { q = '\''; s++; continue; }
        if (c == '$') { if ((r = var(&s, &b, flags))) goto fail; continue; }
        if (c == '~' && !q && b.n == 0) {
            const char *h = getenv("HOME");
            s++;
            if (*s && *s != '/') { if (put(&b, "~", 1)) goto nospace; continue; }
            if (h && put(&b, h, strlen(h))) goto nospace;
            continue;
        }
        if (put(&b, s, 1)) goto nospace;
        s++;
    }
    if (q) { r = WRDE_SYNTAX; goto fail; }
    if (inword && add_word(w, &b)) goto nospace;
    if (!w->we_wordv && !(w->we_wordv = calloc(1, sizeof *w->we_wordv))) return WRDE_NOSPACE;
    return 0;
nospace:
    r = WRDE_NOSPACE;
fail:
    free(b.p);
    return r;
}

void wordfree(wordexp_t *w)
{
    size_t i;
    if (!w || !w->we_wordv) return;
    for (i = 0; i < w->we_wordc; i++) free(w->we_wordv[w->we_offs + i]);
    free(w->we_wordv);
    w->we_wordv = NULL; w->we_wordc = 0;
}

int munmap(void *addr, size_t len)
{
    (void)addr; (void)len;
    errno = EINVAL;
    return -1;
}

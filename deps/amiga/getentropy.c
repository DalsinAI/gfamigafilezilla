/* getentropy.c - getentropy() for AmigaOS 3, which has no random device.
 *
 * GnuTLS, libfilezilla and fzssh ask for their seeds with getentropy().
 * The machine has no hardware source, so the pool is fed with what changes
 * from run to run and call to call: the E-clock read many times around
 * short busy loops (its low bits against the CPU's speed jitter), the date
 * stamp, the task, free memory, and a seed file kept in the program's
 * drawer (PROGDIR:random.seed), rewritten with fresh output on the first
 * call of each run. Everything goes through SHA-256; each answer is
 * SHA-256(pool, counter) and the pool moves on after every call.
 *
 * Copyright (c) 2026 AmiCradle Team */
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

/* ---- SHA-256 (FIPS 180-4) */
typedef struct { uint32_t h[8]; uint64_t n; uint8_t b[64]; unsigned l; } sha256;
static const uint32_t K[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha_block(sha256 *s, const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2; int i;
    for (i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (; i < 64; i++)
        w[i] = (ROR(w[i-2],17) ^ ROR(w[i-2],19) ^ (w[i-2] >> 10)) + w[i-7]
             + (ROR(w[i-15],7) ^ ROR(w[i-15],18) ^ (w[i-15] >> 3)) + w[i-16];
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
    for (i = 0; i < 64; i++) {
        t1 = h + (ROR(e,6) ^ ROR(e,11) ^ ROR(e,25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a,2) ^ ROR(a,13) ^ ROR(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void sha_init(sha256 *s)
{
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, iv, sizeof iv); s->n = 0; s->l = 0;
}
static void sha_add(sha256 *s, const void *v, size_t n)
{
    const uint8_t *p = v;
    s->n += n;
    while (n--) { s->b[s->l++] = *p++; if (s->l == 64) { sha_block(s, s->b); s->l = 0; } }
}
static void sha_end(sha256 *s, uint8_t out[32])
{
    uint64_t bits = s->n * 8; uint8_t z = 0x80; int i;
    sha_add(s, &z, 1); z = 0;
    while (s->l != 56) sha_add(s, &z, 1);
    for (i = 7; i >= 0; i--) { z = (uint8_t)(bits >> (8 * i)); sha_add(s, &z, 1); }
    for (i = 0; i < 32; i++) out[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}

/* ---- the pool */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t pool[32];
static uint32_t counter;
static int seeded;
struct Device *TimerBase;
static struct timerequest tr;

static void gather(sha256 *s)
{
    struct EClockVal ev; struct DateStamp ds; struct Task *me = FindTask(NULL);
    volatile uint32_t spin = 0; int i, j;
    DateStamp(&ds); sha_add(s, &ds, sizeof ds);
    sha_add(s, &me, sizeof me);
    { ULONG m = AvailMem(MEMF_ANY); sha_add(s, &m, sizeof m); }
    { ULONG id = SysBase->IdleCount, dc = SysBase->DispCount; sha_add(s, &id, sizeof id); sha_add(s, &dc, sizeof dc); }
    if (TimerBase)
        for (i = 0; i < 256; i++) {
            ReadEClock(&ev); sha_add(s, &ev, sizeof ev);
            for (j = (ev.ev_lo & 31) + 8; j > 0; j--) spin += j;
        }
    sha_add(s, (const void *)&spin, sizeof spin);
}

static void seed_file(int write_back)
{
    BPTR f; uint8_t buf[32];
    if (!write_back) {
        if ((f = Open((STRPTR)"PROGDIR:random.seed", MODE_OLDFILE))) {
            sha256 s; LONG n = Read(f, buf, sizeof buf); Close(f);
            if (n > 0) { sha_init(&s); sha_add(&s, pool, 32); sha_add(&s, buf, (size_t)n); sha_end(&s, pool); }
        }
        return;
    }
    {   sha256 s; sha_init(&s); sha_add(&s, "seed", 4); sha_add(&s, pool, 32); sha_end(&s, buf); }
    if ((f = Open((STRPTR)"PROGDIR:random.seed", MODE_NEWFILE))) { Write(f, buf, sizeof buf); Close(f); }
}

int getentropy(void *buffer, size_t length)
{
    uint8_t *out = buffer, blk[32]; sha256 s;
    if (length > 256) { errno = EIO; return -1; }
    pthread_mutex_lock(&lock);
    if (!TimerBase && OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&tr, 0) == 0)
        TimerBase = tr.tr_node.io_Device;
    sha_init(&s); sha_add(&s, pool, 32); gather(&s); sha_end(&s, pool);
    if (!seeded) { seed_file(0); seed_file(1); seeded = 1; }
    while (length) {
        size_t k = length < 32 ? length : 32;
        counter++;
        sha_init(&s); sha_add(&s, "out", 3); sha_add(&s, pool, 32); sha_add(&s, &counter, sizeof counter); sha_end(&s, blk);
        memcpy(out, blk, k); out += k; length -= k;
    }
    sha_init(&s); sha_add(&s, "next", 4); sha_add(&s, pool, 32); sha_end(&s, pool);
    memset(blk, 0, sizeof blk);
    pthread_mutex_unlock(&lock);
    return 0;
}

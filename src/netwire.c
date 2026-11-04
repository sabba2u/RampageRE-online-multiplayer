/* netwire.c - the bytes of the online messages (netwire.h) */
#include "netwire.h"
#include <stdio.h>
#include <string.h>

static int room(NetBuf *b, int n)
{
    if (b->err || b->len + n > b->cap) {
        b->err = 1;
        return 0;
    }
    return 1;
}

void net_w_bytes(NetBuf *b, const void *p, int n)
{
    if (room(b, n)) {
        memcpy(b->p + b->len, p, (size_t)n);
        b->len += n;
    }
}

void net_r_bytes(NetBuf *b, void *p, int n)
{
    if (room(b, n)) {
        memcpy(p, b->p + b->len, (size_t)n);
        b->len += n;
    } else
        memset(p, 0, (size_t)n);
}

static void w_le(NetBuf *b, uint64_t v, int n)
{
    uint8_t t[8];
    for (int i = 0; i < n; i++)
        t[i] = (uint8_t)(v >> 8 * i);
    net_w_bytes(b, t, n);
}

static uint64_t r_le(NetBuf *b, int n)
{
    uint8_t t[8];
    uint64_t v = 0;
    net_r_bytes(b, t, n);
    for (int i = 0; i < n; i++)
        v |= (uint64_t)t[i] << 8 * i;
    return v;
}

void net_w_u8(NetBuf *b, uint32_t v) { w_le(b, v, 1); }
void net_w_u16(NetBuf *b, uint32_t v) { w_le(b, v, 2); }
void net_w_u32(NetBuf *b, uint32_t v) { w_le(b, v, 4); }
void net_w_f32(NetBuf *b, float v) { uint32_t u; memcpy(&u, &v, 4); w_le(b, u, 4); }
void net_w_f64(NetBuf *b, double v) { uint64_t u; memcpy(&u, &v, 8); w_le(b, u, 8); }
uint32_t net_r_u8(NetBuf *b) { return (uint32_t)r_le(b, 1); }
uint32_t net_r_u16(NetBuf *b) { return (uint32_t)r_le(b, 2); }
uint32_t net_r_u32(NetBuf *b) { return (uint32_t)r_le(b, 4); }
float net_r_f32(NetBuf *b) { uint32_t u = (uint32_t)r_le(b, 4); float v; memcpy(&v, &u, 4); return v; }
double net_r_f64(NetBuf *b) { uint64_t u = r_le(b, 8); double v; memcpy(&v, &u, 8); return v; }

uint32_t net_hash(const void *p, int n, uint32_t h)
{
    const uint8_t *s = (const uint8_t *)p;
    for (int i = 0; i < n; i++)
        h = (h ^ s[i]) * 16777619u;
    return h;
}

/* ---- net_pack: zero runs and literals */
static void w_var(NetBuf *b, uint32_t v)
{
    while (v >= 0x80) {
        net_w_u8(b, (v & 0x7F) | 0x80);
        v >>= 7;
    }
    net_w_u8(b, v);
}

static uint32_t r_var(NetBuf *b)
{
    uint32_t v = 0;
    for (int s = 0; s < 35 && !b->err; s += 7) {
        uint32_t c = net_r_u8(b);
        v |= (c & 0x7F) << s;
        if (!(c & 0x80))
            break;
    }
    return v;
}

int net_pack(NetBuf *out, const void *src, int n)
{
    const uint8_t *s = (const uint8_t *)src;
    int i = 0;
    while (i < n && !out->err) {
        int z = i;
        while (z < n && !s[z])
            z++;
        int l = z;                                   /* literals until 3 zeros in a row, or zeros to the end */
        while (l < n) {
            if (s[l]) {
                l++;
                continue;
            }
            int r = l;
            while (r < n && !s[r] && r - l < 3)
                r++;
            if (r - l >= 3 || r == n)
                break;
            l = r;
        }
        w_var(out, (uint32_t)(z - i));
        w_var(out, (uint32_t)(l - z));
        net_w_bytes(out, s + z, l - z);
        i = l;
    }
    return out->err ? -1 : 0;
}

int net_unpack(NetBuf *in, void *dst, int n)
{
    uint8_t *d = (uint8_t *)dst;
    int i = 0;
    while (i < n && !in->err) {
        uint32_t z = r_var(in), l = r_var(in);
        if (in->err || z > (uint32_t)(n - i) || l > (uint32_t)(n - i) - z || (!z && !l))
            return -1;
        memset(d + i, 0, z);
        i += (int)z;
        net_r_bytes(in, d + i, (int)l);
        i += (int)l;
    }
    return in->err || i != n ? -1 : 0;
}

/* ---- the digest */
const char *const NET_DSYS_NAME[NET_DSYS] = { "game", "monsters", "buildings", "people", "army", "bosses", "traffic",
                                              "street objects", "edibles", "challenge" };
const float NET_DSYS_TOL[NET_DSYS] = { 0.5f, 3.0f, 0.5f, 3.0f, 3.0f, 0.06f, 4.0f, 0.5f, 0.5f, 0.5f };

int net_dsys_same(const NetDSys *a, const NetDSys *b, float tol)
{
    if (a->h != b->h)
        return 0;
    for (int k = 0; k < NET_DVALS; k++) {
        float d = a->v[k] - b->v[k];
        if (d > tol || d < -tol || d != d)
            return 0;
    }
    return 1;
}

void net_w_digest(NetBuf *b, const NetDigest *d)
{
    net_w_u8(b, NET_DSYS);
    for (int s = 0; s < NET_DSYS; s++) {
        net_w_u32(b, d->s[s].h);
        for (int k = 0; k < NET_DVALS; k++)
            net_w_f32(b, d->s[s].v[k]);
    }
}

void net_r_digest(NetBuf *b, NetDigest *d)
{
    memset(d, 0, sizeof *d);
    if (net_r_u8(b) != NET_DSYS) {
        b->err = 1;
        return;
    }
    for (int s = 0; s < NET_DSYS; s++) {
        d->s[s].h = net_r_u32(b);
        for (int k = 0; k < NET_DVALS; k++)
            d->s[s].v[k] = net_r_f32(b);
    }
}

/* ---- player slots */
void net_w_u64(NetBuf *b, uint64_t v) { net_w_u32(b, (uint32_t)v); net_w_u32(b, (uint32_t)(v >> 32)); }
uint64_t net_r_u64(NetBuf *b) { uint64_t lo = net_r_u32(b); return lo | (uint64_t)net_r_u32(b) << 32; }

int net_game_slots(int steam) { (void)steam; return NET_SLOTS; }   /* (Steam too: a peer per guest) */

int net_slot_pick(const NetSlotPick *s, int n, uint64_t token, double now, double *wait)
{
    if (n > NET_SLOTS)
        n = NET_SLOTS;
    for (int k = 1; token && k < n; k++)             /* its own: kept for it, or open and not yet taken */
        if (s[k].token == token && (s[k].state == NSL_RESERVED || s[k].state == NSL_OPEN))
            return k;
    for (int k = 1; k < n; k++)
        if (s[k].state == NSL_OPEN)
            return k;
    double soonest = -1;
    for (int k = 1; k < n; k++)
        if (s[k].state == NSL_RESERVED && (soonest < 0 || s[k].until - now < soonest))
            soonest = s[k].until - now;
    if (wait)
        *wait = soonest < 0 ? 0 : soonest;
    return soonest >= 0 ? NET_PICK_RESERVED : NET_PICK_FULL;
}

void net_w_hello(NetBuf *b, const NetHello *h)
{
    net_w_u32(b, h->proto);
    net_w_u32(b, h->build);
    net_w_u32(b, h->world);
    net_w_u64(b, h->token);
    net_w_u16(b, (uint32_t)(uint16_t)(int16_t)h->monster);
    int n = 0;                                       /* (last: an older copy reads the rest and stops) */
    while (n < (int)sizeof h->version - 1 && h->version[n])
        n++;
    net_w_u8(b, (uint32_t)n);
    net_w_bytes(b, h->version, n);
    net_w_u8(b, (uint32_t)h->content);               /* (last: a 1.3.0 copy stops before it) */
}

int net_content_ok(int host_wii, int guest_content) { return !host_wii || guest_content != NET_CONTENT_PS2; }

void net_r_hello(NetBuf *b, NetHello *h)
{
    h->proto = net_r_u32(b);
    h->build = net_r_u32(b);
    h->world = net_r_u32(b);
    h->token = net_r_u64(b);
    h->monster = (int16_t)net_r_u16(b);
    h->version[0] = 0;
    h->content = NET_CONTENT_UNSAID;
    if (!b->err && b->len < b->cap) {                /* (an older copy's hello stops here) */
        int n = (int)net_r_u8(b);
        if (n >= (int)sizeof h->version)
            n = (int)sizeof h->version - 1;
        net_r_bytes(b, h->version, n);
        h->version[b->err ? 0 : n] = 0;
    }
    if (!b->err && b->len < b->cap) {                /* (a 1.3.0 copy's stops here) */
        int c = (int)net_r_u8(b);
        h->content = c == NET_CONTENT_PS2 || c == NET_CONTENT_WII ? c : NET_CONTENT_UNSAID;
    }
}

void net_w_slots(NetBuf *b, const NetSlots *s)
{
    int n = s->n < 0 ? 0 : s->n > NET_SLOTS ? NET_SLOTS : s->n;
    net_w_u8(b, (uint32_t)n);
    for (int k = 0; k < n; k++) {
        net_w_u8(b, s->state[k]);
        net_w_f32(b, s->left[k]);
    }
}

void net_r_slots(NetBuf *b, NetSlots *s)
{
    memset(s, 0, sizeof *s);
    s->n = (int)net_r_u8(b);
    if (s->n > NET_SLOTS) {
        b->err = 1;
        s->n = 0;
        return;
    }
    for (int k = 0; k < s->n; k++) {
        s->state[k] = (uint8_t)net_r_u8(b);
        s->left[k] = net_r_f32(b);
    }
}

int net_rejoin_format(char *out, int cap, uint64_t token, long long when)
{
    int n = snprintf(out, (size_t)cap, "rampage-rejoin 1 %08X%08X %lld\n", (unsigned)(token >> 32), (unsigned)token, when);
    return n > 0 && n < cap ? 0 : -1;
}

int net_rejoin_parse(const char *text, uint64_t *token, long long *when)
{
    char hex[20];
    long long w = 0;
    if (sscanf(text, "rampage-rejoin 1 %19s %lld", hex, &w) != 2 || strlen(hex) != 16)
        return -1;
    uint64_t t = 0;
    for (int i = 0; i < 16; i++) {
        char c = hex[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0)
            return -1;
        t = t << 4 | (uint64_t)d;
    }
    if (!t)
        return -1;
    *token = t;
    *when = w;
    return 0;
}

/* ---- the puppets' delta (netwire.h) */
static int dv_put(uint8_t *out, int cap, int at, uint32_t v)
{
    do {
        if (at >= cap)
            return -1;
        out[at++] = (uint8_t)((v & 0x7F) | (v > 0x7F ? 0x80 : 0));
        v >>= 7;
    } while (v);
    return at;
}
static int dv_get(const uint8_t *in, int len, int *at, uint32_t *v)
{
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 7) {
        if (*at >= len)
            return -1;
        uint8_t c = in[(*at)++];
        r |= (uint32_t)(c & 0x7F) << sh;
        if (!(c & 0x80)) {
            *v = r;
            return 0;
        }
    }
    return -1;
}

#define DV_HASH 4096                                 /* the base's 4-byte runs, by hash: where each was last seen */
#define DV_MIN 4                                     /* a copy shorter than this is sent as it is */
#define DV_CHAIN 24                                  /* places a run is looked for at, at most */
static uint32_t dv_h(const uint8_t *p) { return ((uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24) * 2654435761u >> 20; }
static int dv_len(const uint8_t *a, int an, const uint8_t *b, int bn)
{
    int n = an < bn ? an : bn, k = 0;
    while (k < n && a[k] == b[k])
        k++;
    return k;
}
/* a token: the literal bytes before it, then a copy of the base: { varint literals, the bytes, varint copy (>= 1),
 * varint where in the base (zigzag, from where the last copy ended: 0 when nothing moved) }; the last token may stop
 * after its literals. So a record cut out, one put in or a block moved costs a few bytes, not the rest of the snapshot */
static int dv_token(uint8_t *out, int cap, int at, const uint8_t *lit, int nlit, int copy, int off)
{
    at = dv_put(out, cap, at, (uint32_t)nlit);
    if (at < 0 || at + nlit > cap)
        return -1;
    memcpy(out + at, lit, (size_t)nlit);
    at += nlit;
    if (copy > 0) {
        at = dv_put(out, cap, at, (uint32_t)copy);
        if (at >= 0)
            at = dv_put(out, cap, at, (uint32_t)off << 1 ^ (uint32_t)(off >> 31));
    }
    return at;
}

int net_delta_encode(const uint8_t *base, int blen, const uint8_t *cur, int clen, uint8_t *out, int cap)
{
    if (clen < 0 || (clen && !cur) || !out)
        return -1;
    if (!base)
        blen = 0;
    static int head[DV_HASH], prev[NET_DELTA_BASE_MAX];
    if (blen > NET_DELTA_BASE_MAX)
        blen = NET_DELTA_BASE_MAX;
    for (int h = 0; h < DV_HASH; h++)
        head[h] = -1;
    for (int j = 0; j + 4 <= blen; j++) {            /* (a chain from the latest: the nearest first after expect) */
        uint32_t h = dv_h(base + j);
        prev[j] = head[h];
        head[h] = j;
    }
    int at = dv_put(out, cap, 0, (uint32_t)clen), i = 0, lit = 0, expect = 0;
    while (at >= 0 && i < clen) {
        int best = 0, where = -1;
        if (expect < blen) {                         /* where it would be if nothing moved */
            best = dv_len(base + expect, blen - expect, cur + i, clen - i);
            where = expect;
        }
        if (best < 32 && i + 4 <= clen && blen >= 4)
            for (int j = head[dv_h(cur + i)], k = 0; j >= 0 && k < DV_CHAIN; j = prev[j], k++) {
                int l = dv_len(base + j, blen - j, cur + i, clen - i);
                if (l > best) {
                    best = l;
                    where = j;
                }
            }
        if (best >= DV_MIN) {
            at = dv_token(out, cap, at, cur + lit, i - lit, best, where - expect);
            expect = where + best;
            i += best;
            lit = i;
        } else
            i++;
    }
    if (at >= 0 && (lit < clen || clen == 0))
        at = dv_token(out, cap, at, cur + lit, clen - lit, 0, 0);
    return at;
}

int net_delta_decode(const uint8_t *base, int blen, const uint8_t *enc, int elen, uint8_t *out, int cap)
{
    uint32_t n, nl, nc, zz;
    int at = 0;
    if (!base)
        blen = 0;
    if (!enc || !out || dv_get(enc, elen, &at, &n) < 0 || n > (uint32_t)cap)
        return -1;
    uint32_t i = 0;
    long expect = 0;
    while (i < n) {
        if (dv_get(enc, elen, &at, &nl) < 0 || nl > n - i || nl > (uint32_t)(elen - at))
            return -1;
        memcpy(out + i, enc + at, nl);
        at += (int)nl;
        i += nl;
        if (i >= n)
            break;
        if (dv_get(enc, elen, &at, &nc) < 0 || dv_get(enc, elen, &at, &zz) < 0 || nc == 0 || nc > n - i)
            return -1;
        long off = (long)(zz >> 1) ^ -(long)(zz & 1), from = expect + off;
        if (from < 0 || from + (long)nc > blen)
            return -1;
        memcpy(out + i, base + from, nc);
        i += nc;
        expect = from + (long)nc;
    }
    return (int)n;
}

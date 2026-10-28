/* netwire.c - the bytes of the online messages (netwire.h) */
#include "netwire.h"
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

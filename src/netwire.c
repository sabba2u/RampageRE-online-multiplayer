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

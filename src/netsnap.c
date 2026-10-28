/* netsnap.c - the guest's buffer of the host's snapshots (netsnap.h) */
#include "netsnap.h"
#include <string.h>

void netsnap_clear(NetSnapBuf *b) { memset(b, 0, sizeof *b); }

const NetSnapSample *netsnap_newest(const NetSnapBuf *b) { return b->n ? &b->s[b->head] : NULL; }

int netsnap_push(NetSnapBuf *b, double t_host, double t_local_recv, const void *data, int len)
{
    if (len > NET_SNAP_MAX || len < 0)
        return 0;
    if (b->n && t_host <= b->s[b->head].t)           /* out of order, or a repeat */
        return 0;
    double off = t_local_recv - t_host;
    if (!b->have_offset || off < b->offset) {
        b->offset = off;
        b->have_offset = 1;
    }
    b->head = b->n ? (b->head + 1) % NET_SNAP_N : 0;
    if (b->n < NET_SNAP_N)
        b->n++;
    NetSnapSample *s = &b->s[b->head];
    s->t = t_host;
    s->len = len;
    memcpy(s->data, data, (size_t)len);
    return 1;
}

int netsnap_sample(const NetSnapBuf *b, double t_local_now, double delay, const NetSnapSample **a,
                   const NetSnapSample **bb, float *alpha)
{
    *a = *bb = NULL;
    *alpha = 0;
    if (!b->n)
        return 0;
    double t = t_local_now - b->offset - delay;      /* the host's time to show */
    const NetSnapSample *newest = &b->s[b->head];
    if (t >= newest->t) {
        double past = t - newest->t;
        *bb = newest;
        *alpha = (float)(past < NET_SNAP_EXTRAP ? past : NET_SNAP_EXTRAP);
        return 1;
    }
    for (int i = 0; i + 1 < b->n; i++) {            /* newest back: the pair about t */
        const NetSnapSample *hi = &b->s[(b->head - i + NET_SNAP_N) % NET_SNAP_N];
        const NetSnapSample *lo = &b->s[(b->head - i - 1 + NET_SNAP_N) % NET_SNAP_N];
        if (lo->t <= t) {
            *a = lo;
            *bb = hi;
            *alpha = (float)((t - lo->t) / (hi->t - lo->t));
            return 2;
        }
    }
    *bb = &b->s[(b->head - (b->n - 1) + NET_SNAP_N) % NET_SNAP_N];   /* older than all: the oldest, held */
    return 1;
}

/* netsnap.h - online play: the guest's buffer of the host's snapshots, played NET_SNAP_DELAY behind (Emberville's
 * SnapshotBuffer): each sample carries the host's clock; the offset between the clocks is the smallest seen
 * (the fastest packet), samples older than the newest are dropped, and past the newest it goes on at most
 * NET_SNAP_EXTRAP seconds before it holds still. A new block (or a teleport) clears it. */
#ifndef RAMPAGE_NETSNAP_H
#define RAMPAGE_NETSNAP_H

#define NET_SNAP_MAX 1024                            /* a sample's bytes */
#define NET_SNAP_N 32                                /* samples kept */
#define NET_SNAP_DELAY 0.1                           /* seconds behind the host */
#define NET_SNAP_EXTRAP 0.15                         /* seconds past the newest sample at most */

typedef struct {
    double t;                                        /* the host's clock when it was taken */
    int len;
    unsigned char data[NET_SNAP_MAX];
} NetSnapSample;

typedef struct {
    NetSnapSample s[NET_SNAP_N];
    int head, n;                                     /* the newest's index; samples held */
    double offset;                                   /* min (local receive time - host time) */
    int have_offset;
} NetSnapBuf;

void netsnap_clear(NetSnapBuf *b);
/* 0: dropped (older than the newest, or too big); 1: kept */
int netsnap_push(NetSnapBuf *b, double t_host, double t_local_recv, const void *data, int len);
/* the samples about local time now - delay: 0 none yet; 1 hold (*b only, the newest; *alpha the extrapolation in
 * seconds past it, 0 .. NET_SNAP_EXTRAP); 2 interpolate *a .. *b by *alpha (0 .. 1) */
int netsnap_sample(const NetSnapBuf *b, double t_local_now, double delay, const NetSnapSample **a,
                   const NetSnapSample **bb, float *alpha);
const NetSnapSample *netsnap_newest(const NetSnapBuf *b);
#endif

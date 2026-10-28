/* nettransport.h - online play: one connection, host or guest, over UDP (ENet, src/net/third_party/enet). Channel 0
 * reliable and ordered, channel 1 unreliable and sequenced. For trying the internet on one PC, RAMPAGE_NET_LAG
 * (milliseconds) holds every message back that long before it goes, and RAMPAGE_NET_LOSS (percent) drops that share of
 * the unreliable ones. (Steam's sockets are the other back end, phase 2.) */
#ifndef RAMPAGE_NETTRANSPORT_H
#define RAMPAGE_NETTRANSPORT_H

enum { NT_NONE, NT_CONNECT, NT_DISCONNECT, NT_DATA };

typedef struct {
    int type;                                        /* NT_* */
    const unsigned char *data;                       /* NT_DATA: valid until the next nt_poll */
    int len;
} NtEvent;

int nt_host(int port);                               /* 0 ok */
int nt_join(const char *ip, int port);               /* 0: connecting (NT_CONNECT when it is) */
void nt_close(void);
int nt_send(int reliable, const void *data, int len);
int nt_poll(NtEvent *ev, double now);                /* 1 an event, 0 none left this frame */
void nt_flush(void);
int nt_connected(void);
int nt_rtt_ms(void);
unsigned nt_bytes_out(void);                         /* sent so far (counters for the panel) */
unsigned nt_bytes_in(void);
#endif

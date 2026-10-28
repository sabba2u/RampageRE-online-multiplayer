/* nettransport.h - online play: one connection, host or guest, over UDP (ENet, src/net/third_party/enet). Channel 0
 * reliable and ordered, channel 1 unreliable and sequenced. For trying the internet on one PC, RAMPAGE_NET_LAG
 * (milliseconds) holds every message back that long before it goes, and RAMPAGE_NET_LOSS (percent) drops that share of
 * the unreliable ones. Steam is the other back end (netsteam.c, the nts_ functions). */
#ifndef RAMPAGE_NETTRANSPORT_H
#define RAMPAGE_NETTRANSPORT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Steam (netsteam.c): a friends-only lobby the host makes, joined by a lobby id (an invite, Join Game, or
 * +connect_lobby on the command line) */
int nt_host_steam(void);
int nt_join_steam(uint64_t lobby);
int nt_is_steam(void);
int nts_start(void);                                 /* Steam itself, once: 0 running */
int nts_available(void);
const char *nts_why(void);
const char *nts_name(void);
uint64_t nts_lobby(void);
int nts_host(void);
int nts_join_lobby(uint64_t lobby);
void nts_invite(void);                               /* the overlay's invite dialog for the lobby */
void nts_close(void);
void nts_command_line(int argc, char **argv);
uint64_t nts_take_pending(void);                     /* a lobby to join (an invite, Join Game, +connect_lobby), 0 */
int nts_poll(NtEvent *ev, double now);
int nts_send(int reliable, const void *data, int len);
int nts_connected(void);
unsigned nts_bytes_out(void);
unsigned nts_bytes_in(void);
void nts_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif

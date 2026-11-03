/* nettransport.h - online play: the connections, host or guest, over UDP (ENet, src/net/third_party/enet). Channel 0
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

/* the host takes up to NT_PEERS connections at once (each a guest's, or one being told the game is full); a guest has
 * the one, to the host, peer 0. Which player slot a peer plays is the session's (net.c), not the transport's. Over
 * Steam (netsteam.c) the host's guests are peers too, by their Steam ids (up to NT_PEERS) */
#ifndef RAMPAGE_MAX_PLAYERS                           /* port extra: 8 players (4 on the PS2; up to 4 controllers on one PC, the rest online guests) */
#define RAMPAGE_MAX_PLAYERS 8
#endif
#define NT_PEERS (RAMPAGE_MAX_PLAYERS + 2)

typedef struct {
    int type;                                        /* NT_* */
    int peer;                                        /* whose: 0 .. NT_PEERS - 1 */
    const unsigned char *data;                       /* NT_DATA: valid until the next nt_poll */
    int len;
} NtEvent;

int nt_host(int port);                               /* 0 ok */
int nt_join(const char *ip, int port);               /* 0: connecting (NT_CONNECT when it is) */
void nt_close(void);
int nt_send(int reliable, const void *data, int len);   /* to every connected peer (the guest: the host) */
int nt_send_to(int peer, int reliable, const void *data, int len);
void nt_drop(int peer);                              /* let one go, once what was sent to it went (a refusal) */
int nt_poll(NtEvent *ev, double now);                /* 1 an event, 0 none left this frame */
void nt_flush(void);
int nt_connected(void);                              /* any peer */
int nt_rtt_ms(void);                                 /* the first connected peer's */
int nt_peer_rtt_ms(int peer);
int nt_peer_silent_ms(int peer);   /* since anything came from it, ENet's pings too (Steam: 0, never silent) */
int nt_peer_addr(int peer, char *out, int cap);       /* the host: peer's "ip:port" (Steam: "Steam"); 0 ok, -1 none */
unsigned nt_bytes_out(void);                         /* sent so far (counters for the panel) */
unsigned nt_bytes_in(void);
/* finding games on the network (the launcher's Join Local): a UDP message NT_LAN_ASK to the game's port (broadcast);
 * a host answers "NT_LAN_SAY <version> <port>" from its ENet socket (nettransport.c nte_intercept) */
#define NT_LAN_ASK "RampageRE?LAN"
#define NT_LAN_SAY "RampageRE!LAN"
/* the test network (RAMPAGE_NETSIM, nettransport.c): unreliable messages it dropped, reliable ones it made late */
void nt_netsim_stats(unsigned *dropped, unsigned *resent);
/* ENet's own socket, around ENet (netstun.c: STUN and the punches); ip in host byte order. -1 no socket (Steam) */
int nt_raw_send(uint32_t ip, int port, const void *data, int len);
int nt_local_port(void);                             /* the port ENet's socket is bound to, 0 none */

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
int nts_send_to(int peer, int reliable, const void *data, int len);   /* -1: every peer */
void nts_drop(int peer);
int nts_connected(void);
unsigned nts_bytes_out(void);
unsigned nts_bytes_in(void);
void nts_shutdown(void);
/* the friends list's line for this player (rich presence): Spacewar's own "#StatusWithScore" ("{#Status_%gamestatus%}:
 * %score%"), gamestatus one of its AtMainMenu / WaitingForMatch / Winning, score our free text; "status" (the game
 * info dialog) the whole line. Nothing while Steam is not running here */
void nts_set_status(const char *gamestatus, const char *text);

#ifdef __cplusplus
}
#endif
#endif

/* net.h - online play (port extra, docs/superpowers/specs/2026-10-27-online-multiplayer-design.md): two players on
 * two PCs, the host's game the one that counts. The host (player 1) runs the game; the guest (player 2) sends its pad
 * and shows the host's world from snapshots. This is the session: the connection, the handshake, player 2's pad, the
 * snapshots' buffer, the log; src/netgame.c (included by main.c) reads and writes the game's own state.
 * With neither --host nor --join, net_role() is NET_OFF and nothing here runs. */
#ifndef RAMPAGE_NET_H
#define RAMPAGE_NET_H
#include <stdint.h>
#include "netsnap.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { NET_OFF, NET_HOST, NET_GUEST };
enum { NETS_IDLE, NETS_WAITING, NETS_CONNECTING, NETS_HANDSHAKE, NETS_CONNECTED, NETS_FAILED };

/* a pad on the wire: the sticks, and the buttons MV_UP .. MV_L3 (moves.h) as bits 0 .. 17 */
typedef struct { float lx, ly, rx, ry; uint32_t pressed, held; } NetPad;
#define NET_PAD_REDUNDANCY 6                         /* each NM_PAD carries this many frames, newest first */

int net_role(void);
int net_state(void);                                 /* NETS_* */
int net_live(void);                                  /* connected, and both in the same world */
const char *net_state_text(void);
const char *net_peer_text(void);                     /* "127.0.0.1:27960" */

void net_start_host(int port);
void net_start_join(const char *addr);               /* "ip" or "ip:port" */
void net_stop(void);
void net_update(double now);                         /* once a frame: the connection, the messages */
double net_now(void);                                /* the clock net_update was given */

/* the world both must be in for the snapshots to count: hash of city, block, mode and players (main.c sets it as a
 * block starts, 0 none). The host tells the guest its own; the guest's snapshots wait while they differ */
#define NET_WORLD_INFO 64
void net_set_world(uint32_t id, const void *info, int len);   /* info: what the guest needs to load it (netgame.c) */
uint32_t net_world(void);
uint32_t net_host_world(void);                       /* the guest: the host's world (0 not yet told) */
const void *net_host_world_info(int *len);           /* the guest: the host's info with it (NULL none) */

/* player 2's pad: the guest sends its own every frame; the host takes it (the presses since the last take, the held
 * buttons and sticks of the newest; held as it was when a packet is late). 0: none yet or not connected */
void net_send_pad(const NetPad *p);
int net_remote_pad(NetPad *out);

/* the host's snapshots (netgame.c builds them, the bytes after the header) */
void net_send_snap(const void *data, int len);
const NetSnapBuf *net_snaps(void);                   /* the guest's buffer of them */
void net_snaps_clear(void);

/* other messages of netgame.c's own (NM_BUILDING, NM_EVENT): sent as they are, the whole message, first byte the
 * type; received through the handler */
void net_send_msg(int reliable, const void *data, int len);
void net_set_handler(void (*fn)(const uint8_t *data, int len));

/* the desync check: the guest sends its fingerprint of what it shows, with the host time it shows; the host
 * keeps its own of the last few seconds (net_keep_digest) and compares; two in a row that differ are a MISMATCH */
void net_keep_digest(double t_host, uint32_t digest);
void net_send_digest(double t_host, uint32_t digest);
int net_mismatches(void);
int net_digest_checks(void);

/* the log: the panel's, and stdout */
void net_log(const char *fmt, ...);
int net_log_count(void);
const char *net_log_line(int i);                     /* 0 the oldest */

typedef struct { int ping_ms; float snaps_per_s, kb_out_per_s, kb_in_per_s; int snap_bytes; } NetStats;
NetStats net_stats(void);

#ifdef __cplusplus
}
#endif
#endif

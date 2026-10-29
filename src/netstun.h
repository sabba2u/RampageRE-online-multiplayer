/* netstun.h - online play without port forwarding: STUN and UDP hole punching (netstun.c, docs/NETPLAY.md "Getting
 * through routers"). A STUN server (RFC 5389) answers "this is the address and port your packet came from": asked
 * from the very socket ENet plays on, that is the game's public address, which the lobby lists. A guest that joins by
 * code asks the same, then "knocks" (lobby/src/http.ts: POST /games/<code>/knock) with its own; the host, polling its
 * knocks, sends a few small packets to each guest so its router lets the guest's packets in. Not a relay: no game
 * traffic goes through the STUN server or the lobby */
#ifndef RAMPAGE_NETSTUN_H
#define RAMPAGE_NETSTUN_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETSTUN_SERVER "stun.l.google.com:19302"     /* RAMPAGE_STUN=host:port another one, RAMPAGE_STUN=0 none */
#define NETSTUN_PUNCH "RAMPUNCH"                     /* the punch packet: 8 bytes, dropped on arrival */

/* ---- the parts without a network (tests/net_test.c) */
int netstun_request(uint8_t out[20], const uint8_t txid[12]);   /* a Binding Request: its length, 20 */
/* a Binding Success Response to txid: 0 and its (XOR-)MAPPED-ADDRESS, IPv4, ip in host byte order; -1 not one */
int netstun_parse(const uint8_t *p, int len, const uint8_t txid[12], uint32_t *ip, uint16_t *port);
int netstun_is_stun(const uint8_t *p, int len);      /* a STUN message (RFC 5389: the magic cookie) */
int netstun_is_punch(const uint8_t *p, int len);
/* the lobby's knocks (GET /games/<code>/knocks): "knock a.b.c.d:port" lines, up to max into out[] "a.b.c.d:port" */
int netstun_parse_knocks(const char *text, char out[][24], int max);

/* ---- the game's (net.c, nettransport.c, netlobby.c) */
void netstun_start(void);                            /* an ENet socket is up (host or guest): ask for its address */
void netstun_stop(void);
void netstun_update(double now);                     /* net_update's: the asking, the punches */
void netstun_received(const uint8_t *p, int len);    /* a STUN answer on ENet's socket (nettransport.c's intercept) */
const char *netstun_public(void);                    /* "a.b.c.d:port" once the server answered, else "" */
int netstun_done(void);                              /* answered, or given up (no server, no answer) */
void netstun_punch(const char *addr);                /* "a.b.c.d:port": punches toward it for a few seconds */

#ifdef __cplusplus
}
#endif
#endif

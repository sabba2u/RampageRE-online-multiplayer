/* netwire.h - online play (port extra, docs/superpowers/specs/2026-10-27-online-multiplayer-design.md): the bytes of
 * the messages. Little endian; a read past the end sets err and gives 0, so a short or broken message reads as zeros
 * and is then thrown away by its reader (err set). */
#ifndef RAMPAGE_NETWIRE_H
#define RAMPAGE_NETWIRE_H
#include <stdint.h>

typedef struct {
    uint8_t *p;
    int len, cap, err;                               /* written or read so far; the room; went past the end */
} NetBuf;

/* the messages, by their first byte; who sends each, on which channel, and how often */
enum NetMsg {
    NM_HELLO = 1,    /* guest -> host, reliable, once: proto, build id, world id */
    NM_WELCOME,      /* host -> guest, reliable, once: the world id the host plays */
    NM_REJECT,       /* host -> guest, reliable, once: a reason (text), then the host disconnects */
    NM_PAD,          /* guest -> host, unreliable, every frame: the guest's frame and its last NET_PAD_REDUNDANCY pads */
    NM_SNAP,         /* host -> guest, unreliable, 20 Hz: the monsters, both players' pads, the game's flow */
    NM_BUILDING,     /* host -> guest, reliable, on change: a building's cells and state */
    NM_WORLD,        /* host -> guest, reliable, on change: the world id the host now plays (a new block) */
    NM_DIGEST,       /* guest -> host, reliable, every 2 s: the guest's fingerprint of the world */
    NM_EVENT,        /* host -> guest, reliable: one-off things (results, a monster defeated) */
    NM_PUPPET,       /* host -> guest, unreliable, each channel's rate: a system's units (netgame.c NGP_*) */
    NM_PEVENT,       /* host -> guest, reliable, in order: a system's effects and sounds, played at their host time */
    NM_MENU,         /* both ways, reliable: the menus' input (netrep_menu.c) */
    NM_STATE,        /* both ways, reliable: the guest asks for the block's state (a join, a reconnect, a new block);
                      * the host sends every building whole, the edibles' slots, the challenge's upgrade (netgame.c) */
    NM_CHAT,         /* both ways, reliable: a chat line (netgame.c): who (the player slot: 0 the host), the text; the
                      * host passes a guest's on to the others */
    NM_SLOTS,        /* host -> guests, reliable, on change and each second while one is kept: every player slot's
                      * state (NSL_*) and a kept one's seconds left (net_w_slots) */
};

/* 2: player slots. NM_HELLO carries the guest's rejoin token and the monster it would like; NM_WELCOME the slot it
 * plays and its token; NM_SNAP each slot's pad ack; NM_SLOTS */
#define NET_PROTO 2
#define NET_PORT 27960

/* ---- player slots (net.c): slot 0 is the host's player 1, slot k the monster mon[k]. Today a hosted game has 2 (a
 * guest plays player 2); everything below is sized NET_SLOTS so 4 players later is no new wire format */
#define NET_SLOTS 4
#define NET_RESERVE_S 60.0                           /* a dropped guest's slot is kept this long for its token */
enum {
    NSL_OPEN,        /* nobody plays it: waiting for a player (its monster idle, untouchable, grey) */
    NSL_ONLINE,      /* a guest plays it */
    NSL_RESERVED,    /* its guest dropped: kept NET_RESERVE_S for that guest's token (its monster as NSL_OPEN's) */
    NSL_LOCAL,       /* a player on the host's PC took it (its keys or controller): it is theirs until the panel's
                      * Open for online */
    NSL_HOST,        /* slot 0 */
};
typedef struct { uint8_t state; uint64_t token; double until; } NetSlotPick;
/* the slot a guest that says `token` (0 none) gets, of slots 1 .. n - 1: its own kept or open one (the same token), else
 * the first open one; NET_PICK_RESERVED when only kept ones are left (*wait: the soonest one's seconds left),
 * NET_PICK_FULL when every one is played */
enum { NET_PICK_FULL = -1, NET_PICK_RESERVED = -2 };
int net_slot_pick(const NetSlotPick *s, int n, uint64_t token, double now, double *wait);

/* NM_HELLO after its type byte */
typedef struct { uint32_t proto, build, world; uint64_t token; int monster; } NetHello;
void net_w_hello(NetBuf *b, const NetHello *h);
void net_r_hello(NetBuf *b, NetHello *h);
/* NM_SLOTS after its type byte: n, then each slot's state and seconds left */
typedef struct { int n; uint8_t state[NET_SLOTS]; float left[NET_SLOTS]; } NetSlots;
void net_w_slots(NetBuf *b, const NetSlots *s);
void net_r_slots(NetBuf *b, NetSlots *s);
/* the guest's rejoin file (netrejoin.cfg): "rampage-rejoin 1 <token hex> <unix time>". 0 ok */
int net_rejoin_format(char *out, int cap, uint64_t token, long long when);
int net_rejoin_parse(const char *text, uint64_t *token, long long *when);
void net_w_u64(NetBuf *b, uint64_t v);
uint64_t net_r_u64(NetBuf *b);

static inline NetBuf netbuf(void *p, int cap) { NetBuf b = { (uint8_t *)p, 0, cap, 0 }; return b; }
void net_w_u8(NetBuf *b, uint32_t v);
void net_w_u16(NetBuf *b, uint32_t v);
void net_w_u32(NetBuf *b, uint32_t v);
void net_w_f32(NetBuf *b, float v);
void net_w_f64(NetBuf *b, double v);
void net_w_bytes(NetBuf *b, const void *p, int n);
uint32_t net_r_u8(NetBuf *b);
uint32_t net_r_u16(NetBuf *b);
uint32_t net_r_u32(NetBuf *b);
float net_r_f32(NetBuf *b);
double net_r_f64(NetBuf *b);
void net_r_bytes(NetBuf *b, void *p, int n);

uint32_t net_hash(const void *p, int n, uint32_t h);   /* FNV-1a, h = 2166136261 to start */

/* plain data packed small: runs of zero bytes counted, the rest as it is. Each step is a varint zero run, a varint
 * literal length and the literals, until n bytes are covered. 0 ok (out->err set when it did not fit) */
int net_pack(NetBuf *out, const void *src, int n);
/* the bytes net_pack wrote for n bytes into dst; 0 ok, -1 broken (dst then undefined) */
int net_unpack(NetBuf *in, void *dst, int n);

/* ---- the desync check's fingerprint: one entry a system, each a hash of what must be the same exactly and a few
 * values compared within a tolerance (places that the guest's delay and blending move a little) */
enum { NDS_GAME, NDS_MONSTERS, NDS_BUILDINGS, NDS_PEOPLE, NDS_ARMY, NDS_BOSS, NDS_TRAFFIC, NDS_STREET, NDS_EDIBLES,
       NDS_CHALLENGE, NET_DSYS };
#define NET_DVALS 8
typedef struct { uint32_t h; float v[NET_DVALS]; } NetDSys;
typedef struct { NetDSys s[NET_DSYS]; } NetDigest;
extern const char *const NET_DSYS_NAME[NET_DSYS];   /* "game", "monsters", ... */
extern const float NET_DSYS_TOL[NET_DSYS];           /* each system's tolerance on its values */
/* the same: hashes equal and every value within tol */
int net_dsys_same(const NetDSys *a, const NetDSys *b, float tol);
void net_w_digest(NetBuf *b, const NetDigest *d);
void net_r_digest(NetBuf *b, NetDigest *d);
#endif

/* net.c - online play's session (net.h): the connections, the handshake, the player slots, the guests' pads, the
 * snapshots, the log.
 *
 * Player slots. Slot 0 is the host's player 1; slot k plays the game's monster k (mon[k]). Each transport peer
 * (nettransport.c: up to NT_PEERS at once) says hello and is given a slot, or told no ("The game is full", or that the
 * only slot left is kept for someone). A slot is open (nobody plays it), online (a guest does), reserved (its guest
 * dropped: kept NET_RESERVE_S for the rejoin token that guest was given), local (a player on the host's PC took it with
 * their keys or controller) or the host's. The guest keeps its token in memory and in netrejoin.cfg beside the game, so
 * a guest that crashed and was started again within the minute gets its slot and monster back mid-round, however it
 * joins (a code through the lobby, the address, a rampage:// link): the host matches the token, not the address.
 * Every guest is told every slot's state (NM_SLOTS) for the labels over the monsters (netgame.c). */
#include "net.h"
#include "netwire.h"
#include "nettransport.h"
#include "netupnp.h"
#include "netstun.h"
#include "netlobby.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define LOG_LINES 200
#define DIGESTS 256                                  /* the host's own, at most 30 a second: eight seconds */
#define NET_GAME_SLOTS 2                             /* the slots a hosted game has today (a guest plays player 2) */
#define REJOIN_FILE "netrejoin.cfg"
#define REJOIN_KEEP_S 3600                           /* a token older than this in the file is not offered */

/* one player slot, the host's view (a guest has only the states, NM_SLOTS) */
typedef struct {
    int state;                                       /* NSL_* */
    int peer;                                        /* NSL_ONLINE: its transport peer */
    uint64_t token;                                  /* the guest's rejoin token (0 none) */
    double until;                                    /* NSL_RESERVED: kept till then */
    int monster;                                     /* the monster its guest asked for (-1 any) */
    NetPad remote;                                   /* the newest pad, and the presses not yet taken */
    uint32_t remote_frame, remote_pressed;
    int have_remote;
    int sys_run[NET_DSYS];                           /* its digests' systems that differed in a row */
} Slot;

static struct {
    int role, state, port;
    double now;
    char peer[64], reject[128];
    uint32_t world, host_world;
    uint8_t info[NET_WORLD_INFO], host_info[NET_WORLD_INFO];
    int info_len, host_info_len;
    void (*handler)(const uint8_t *, int);
    /* the guest */
    uint32_t pad_frame;
    NetPad pads[NET_PAD_REDUNDANCY];                 /* newest first */
    double pad_at;                                   /* the last sent: at most NET_PAD_RATE a second */
    uint32_t pad_pending;                            /* presses since then, sent with the next */
    NetSnapBuf snaps;
    int my_slot, wanted_monster;
    double give_up_at;                               /* net_give_up: the connection closed then (0 none) */
    NetSlots told;                                   /* the host's last NM_SLOTS, and when it came */
    double told_at;
    /* the host */
    Slot slot[NET_SLOTS];
    int nslots;
    int peer_slot[NT_PEERS];                         /* each peer's slot, -1 saying hello (or none) */
    int peer_on[NT_PEERS];
    int msg_slot;                                    /* the slot of the message being handled */
    int slots_dirty;
    double slots_sent;
    struct { double t; NetDigest d; } digests[DIGESTS];
    int ndigests, digest_head, mismatches, checks;
    int sys_mismatches[NET_DSYS], sys_state[NET_DSYS];   /* (sys_state: 1 + the last check's) */
    /* the panel's counters */
    double stat_at;
    int snaps_since, snap_bytes;
    unsigned out_at, in_at;
    NetStats stats;
} g;

static uint64_t g_token;                             /* the guest's rejoin token (kept over net_stop: a rejoin) */

static char g_log[LOG_LINES][160];
static int g_log_n, g_log_head;

void net_log(const char *fmt, ...)
{
    char *s = g_log[g_log_head];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof g_log[0], fmt, ap);
    va_end(ap);
    printf("net: %s\n", s);
    fflush(stdout);
    g_log_head = (g_log_head + 1) % LOG_LINES;
    if (g_log_n < LOG_LINES)
        g_log_n++;
}

int net_log_count(void) { return g_log_n; }
const char *net_log_line(int i) { return g_log[(g_log_head - g_log_n + i + LOG_LINES) % LOG_LINES]; }

int net_role(void) { return g.role; }
int net_state(void) { return g.state; }
double net_now(void) { return g.now; }
const char *net_peer_text(void) { return g.peer; }
uint32_t net_world(void) { return g.world; }
uint32_t net_host_world(void) { return g.role == NET_HOST ? g.world : g.host_world; }
const NetSnapBuf *net_snaps(void) { return &g.snaps; }
void net_snaps_clear(void) { netsnap_clear(&g.snaps); }
void net_set_handler(void (*fn)(const uint8_t *, int)) { g.handler = fn; }
int net_mismatches(void) { return g.mismatches; }
int net_digest_checks(void) { return g.checks; }
NetStats net_stats(void) { return g.stats; }

int net_live(void)
{
    return g.state == NETS_CONNECTED && g.world && (g.role == NET_HOST || g.host_world == g.world);
}

const char *net_state_text(void)
{
    switch (g.state) {
    case NETS_WAITING:
        if (g.role == NET_HOST && net_slot_count(NSL_OPEN) == 0)
            return net_slot_count(NSL_RESERVED) ? "Hosting: a player's place kept for them" : "Hosting: every player slot taken";
        return "Waiting for a player to join";
    case NETS_CONNECTING: return "Connecting";
    case NETS_HANDSHAKE: return "Saying hello";
    case NETS_CONNECTED:
        if (!net_live())
            return g.role == NET_GUEST ? "Connected, waiting for the host's game" : "Connected, not in a game yet";
        return "Playing online";
    case NETS_FAILED: return g.reject[0] ? g.reject : "Connection failed";
    default: return "Offline";
    }
}

/* this build's id: two different builds never play together. The program file's own bytes, hashed once (Windows);
 * elsewhere the protocol alone */
static uint32_t build_id(void)
{
    static uint32_t id;
    if (id)
        return id;
    id = 2166136261u;
#ifdef _WIN32
    char path[1024];
    DWORD n = GetModuleFileNameA(NULL, path, sizeof path);
    FILE *f = n && n < sizeof path ? fopen(path, "rb") : NULL;
    if (f) {
        static uint8_t buf[65536];
        size_t got;
        while ((got = fread(buf, 1, sizeof buf, f)) > 0)
            id = net_hash(buf, (int)got, id);
        fclose(f);
    }
#endif
    return id ? id : 1;
}

uint32_t net_build_id(void) { return build_id(); }
int net_port(void) { return g.port; }

static void w_world(NetBuf *b)
{
    net_w_u32(b, g.world);
    net_w_u8(b, (uint32_t)g.info_len);
    net_w_bytes(b, g.info, g.info_len);
}

static void r_world(NetBuf *b)
{
    g.host_world = net_r_u32(b);
    g.host_info_len = (int)net_r_u8(b);
    if (g.host_info_len > NET_WORLD_INFO)
        g.host_info_len = 0;
    net_r_bytes(b, g.host_info, g.host_info_len);
    if (b->err)
        g.host_world = 0;
}

const void *net_host_world_info(int *len)
{
    *len = g.host_info_len;
    return g.host_world && g.host_info_len ? g.host_info : NULL;
}

/* the host: a message to every guest that plays (a peer still saying hello gets none: its NM_WELCOME comes first) */
static void send_guests(int except, int reliable, const void *data, int len)
{
    for (int k = 1; k < NET_SLOTS; k++)
        if (k != except && g.slot[k].state == NSL_ONLINE)
            nt_send_to(g.slot[k].peer, reliable, data, len);
}

static void send_world(void)
{
    uint8_t m[8 + NET_WORLD_INFO];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_WORLD);
    w_world(&b);
    send_guests(-1, 1, m, b.len);
}

/* ---- the slots */
static void slots_reset(void)
{
    memset(g.slot, 0, sizeof g.slot);
    for (int k = 0; k < NET_SLOTS; k++) {
        g.slot[k].state = k ? NSL_OPEN : NSL_HOST;
        g.slot[k].peer = -1;
        g.slot[k].monster = -1;
    }
    for (int i = 0; i < NT_PEERS; i++) {
        g.peer_slot[i] = -1;
        g.peer_on[i] = 0;
    }
    g.nslots = NET_GAME_SLOTS;
    g.msg_slot = -1;
    g.slots_dirty = 1;
}

/* the host's state: connected while a guest plays, saying hello while one does, else waiting */
static void host_state(void)
{
    if (g.role != NET_HOST || g.state == NETS_FAILED)
        return;
    int online = 0, hello = 0;
    for (int k = 1; k < NET_SLOTS; k++)
        online |= g.slot[k].state == NSL_ONLINE;
    for (int i = 0; i < NT_PEERS; i++)
        hello |= g.peer_on[i] && g.peer_slot[i] < 0;
    g.state = online ? NETS_CONNECTED : hello ? NETS_HANDSHAKE : NETS_WAITING;
}

static void slot_set(int k, int state)
{
    if (g.slot[k].state == state)
        return;
    static const char *const NAME[] = { "open", "online", "kept for its player", "local", "the host's" };
    net_log("slot %d: %s", k, NAME[state]);
    g.slot[k].state = state;
    if (state != NSL_ONLINE) {
        g.slot[k].have_remote = 0;
        memset(g.slot[k].sys_run, 0, sizeof g.slot[k].sys_run);
    }
    g.slots_dirty = 1;
    host_state();
}

int net_slots(void) { return g.role == NET_GUEST ? (g.told.n ? g.told.n : NET_GAME_SLOTS) : NET_GAME_SLOTS; }
int net_my_slot(void) { return g.role == NET_GUEST ? g.my_slot : 0; }
int net_msg_slot(void) { return g.msg_slot; }
void net_set_wanted_monster(int monster) { g.wanted_monster = monster; }
uint64_t net_rejoin_token(void) { return g_token; }

int net_slot_state(int k, double *left)
{
    if (left)
        *left = 0;
    if (k < 0 || k >= NET_SLOTS || g.role == NET_OFF)
        return k == 0 ? NSL_HOST : NSL_OPEN;
    if (g.role == NET_GUEST) {
        if (k >= g.told.n)
            return k == 0 ? NSL_HOST : k == g.my_slot ? NSL_ONLINE : NSL_OPEN;
        if (left && (g.told.state[k] & ~NSL_JOINING) == NSL_RESERVED) {
            double l = g.told.left[k] - (g.now - g.told_at);
            *left = l < 0 ? 0 : l;
        }
        return g.told.state[k] & ~NSL_JOINING;
    }
    if (left && g.slot[k].state == NSL_RESERVED)
        *left = g.slot[k].until - g.now < 0 ? 0 : g.slot[k].until - g.now;
    return g.slot[k].state;
}

int net_slot_joining(int k)
{
    if (k < 1 || k >= NET_SLOTS || g.role == NET_OFF)
        return 0;
    if (g.role == NET_GUEST)
        return k < g.told.n && (g.told.state[k] & NSL_JOINING);
    return g.slot[k].state == NSL_ONLINE && !g.slot[k].have_remote;
}

int net_slot_count(int state)
{
    int n = 0;
    for (int k = 0; k < net_slots(); k++)
        n += net_slot_state(k, NULL) == state;
    return n;
}

int net_slot_monster(int k) { return k > 0 && k < NET_SLOTS ? g.slot[k].monster : -1; }

void net_slot_local(int k, int on)
{
    if (g.role != NET_HOST || k < 1 || k >= NET_SLOTS || g.slot[k].state == NSL_ONLINE)
        return;
    if (on && g.slot[k].state != NSL_LOCAL) {
        net_log("slot %d: a player on this PC took it%s", k, g.slot[k].state == NSL_RESERVED ? " (its guest's kept "
                "place is gone)" : "");
        g.slot[k].token = 0;                         /* (the dropped guest's rejoin: the slot is taken now) */
    }
    slot_set(k, on ? NSL_LOCAL : g.slot[k].state == NSL_LOCAL ? NSL_OPEN : g.slot[k].state);
}

static void send_reject(int peer, const char *why)
{
    uint8_t m[160];
    NetBuf o = netbuf(m, sizeof m);
    net_w_u8(&o, NM_REJECT);
    net_w_bytes(&o, why, (int)strlen(why) + 1);
    nt_send_to(peer, 1, m, o.len);
    nt_flush();
}

void net_slot_kick(int k)
{
    if (g.role != NET_HOST || k < 1 || k >= NET_SLOTS || g.slot[k].state != NSL_ONLINE)
        return;
    int p = g.slot[k].peer;
    net_log("slot %d: its guest was removed", k);
    send_reject(p, "The host removed you from the game");
    nt_drop(p);
    if (p >= 0 && p < NT_PEERS) {
        g.peer_on[p] = 0;
        g.peer_slot[p] = -1;
    }
    g.slot[k].token = 0;                             /* (its rejoin token is no good now) */
    g.slot[k].peer = -1;
    slot_set(k, NSL_OPEN);
}

/* every second, and at once on a change: the slots to the guests; a kept slot's time running out */
static void slots_frame(void)
{
    if (g.role != NET_HOST)
        return;
    int kept = 0;
    for (int k = 1; k < NET_SLOTS; k++)
        if (g.slot[k].state == NSL_RESERVED) {
            if (g.now >= g.slot[k].until) {
                net_log("slot %d: nobody came back in %.0f s", k, NET_RESERVE_S);
                slot_set(k, NSL_OPEN);               /* (the token stays: its guest may still take it while open) */
            } else
                kept = 1;
        }
    if (!g.slots_dirty && !(kept && g.now - g.slots_sent >= 1.0))
        return;
    NetSlots s;
    memset(&s, 0, sizeof s);
    s.n = g.nslots;
    for (int k = 0; k < s.n; k++) {
        s.state[k] = (uint8_t)(g.slot[k].state | (net_slot_joining(k) ? NSL_JOINING : 0));
        s.left[k] = g.slot[k].state == NSL_RESERVED ? (float)(g.slot[k].until - g.now) : 0;
    }
    uint8_t m[8 + NET_SLOTS * 5];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_SLOTS);
    net_w_slots(&b, &s);
    send_guests(-1, 1, m, b.len);
    g.slots_dirty = 0;
    g.slots_sent = g.now;
}

/* the guest's rejoin token: netrejoin.cfg beside the game (RAMPAGE_NET_REJOIN: another file), so a game started again
 * after a crash offers it too */
static const char *rejoin_path(void)
{
    const char *p = getenv("RAMPAGE_NET_REJOIN");
    return p && *p ? p : REJOIN_FILE;
}

static void rejoin_load(void)
{
    if (g_token)
        return;
    FILE *f = fopen(rejoin_path(), "rb");
    if (!f)
        return;
    char line[96] = "";
    if (fgets(line, sizeof line, f)) {
        uint64_t t;
        long long when;
        if (net_rejoin_parse(line, &t, &when) == 0 && (long long)time(NULL) - when < REJOIN_KEEP_S) {
            g_token = t;
            net_log("our rejoin token from %s", rejoin_path());
        }
    }
    fclose(f);
}

static void rejoin_save(void)
{
    char line[96];
    if (net_rejoin_format(line, sizeof line, g_token, (long long)time(NULL)) != 0)
        return;
    FILE *f = fopen(rejoin_path(), "wb");
    if (f) {
        fputs(line, f);
        fclose(f);
    }
}

static uint64_t new_token(int k)
{
    static uint32_t n;
    uint64_t t = 0;
#ifdef _WIN32
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    t = (uint64_t)c.QuadPart;
#endif
    uint32_t lo = net_hash(&t, 8, 2166136261u ^ (uint32_t)time(NULL));
    lo = net_hash(&k, 4, lo);
    n++;
    uint32_t hi = net_hash(&n, 4, lo ^ 0x9E3779B9u);
    hi = net_hash(&g.now, 8, hi);
    uint64_t r = (uint64_t)hi << 32 | lo;
    return r ? r : 1;
}

void net_set_world(uint32_t id, const void *info, int len)
{
    if (id == g.world)
        return;
    g.world = id;
    g.info_len = info && len > 0 && len <= NET_WORLD_INFO ? len : 0;
    if (g.info_len)
        memcpy(g.info, info, (size_t)g.info_len);
    netsnap_clear(&g.snaps);
    g.ndigests = g.digest_head = 0;
    for (int k = 0; k < NET_SLOTS; k++)
        memset(g.slot[k].sys_run, 0, sizeof g.slot[k].sys_run);
    if (g.role != NET_OFF)
        net_log("world %08X", id);
    if (g.role == NET_HOST)
        send_world();
}

void net_give_up(const char *why)
{
    if (g.role != NET_GUEST)
        return;
    net_log("left the host: %s", why);
    nt_flush();
    g.give_up_at = g.now + 0.5;                      /* (closed in net_update: the reason, sent last, arrives first) */
    g.state = NETS_FAILED;
    snprintf(g.reject, sizeof g.reject, "%s", why);
}

int net_gave_up(void) { return g.give_up_at != 0; }

void net_start_host(int port)
{
    net_stop();
    g.role = NET_HOST;
    slots_reset();
    if (nt_host(port ? port : NET_PORT) != 0) {
        g.state = NETS_FAILED;
        snprintf(g.reject, sizeof g.reject, "Cannot open port %d (is another copy hosting?)", port ? port : NET_PORT);
        net_log("%s", g.reject);
        return;
    }
    g.state = NETS_WAITING;
    g.port = port ? port : NET_PORT;                 /* (the lobby lists it: netlobby.c) */
    net_log("hosting on port %d", port ? port : NET_PORT);
    net_upnp_open(port ? port : NET_PORT);           /* the router asked to forward it (netupnp.c, in the background) */
    netstun_start();                                 /* and STUN asked how the internet sees it (netstun.c) */
}

void net_start_host_steam(void)
{
    net_stop();
    g.role = NET_HOST;
    slots_reset();
    if (nt_host_steam() != 0) {
        g.state = NETS_FAILED;
        snprintf(g.reject, sizeof g.reject, "%s", nts_why());
        return;
    }
    snprintf(g.peer, sizeof g.peer, "Steam");
    g.state = NETS_WAITING;
}

void net_start_join_steam(uint64_t lobby)
{
    net_stop();
    g.role = NET_GUEST;
    snprintf(g.peer, sizeof g.peer, "Steam lobby %llu", (unsigned long long)lobby);
    if (nt_join_steam(lobby) != 0) {
        g.state = NETS_FAILED;
        snprintf(g.reject, sizeof g.reject, "%s", nts_why());
        return;
    }
    g.state = NETS_CONNECTING;
}

int net_is_steam(void) { return nt_is_steam(); }

void net_start_join(const char *addr)
{
    char to[64];                                     /* the lobby's: a join code, a rampage://join/ link (netlobby.c) */
    int kind = netlobby_join_kind(addr, to, sizeof to);
    if (kind == NLJ_CODE) {
        netlobby_lookup(to);                         /* (its answer joins the address, from netlobby_update) */
        return;
    }
    if (kind == NLJ_ADDR)
        addr = to;
    net_stop();
    g.role = NET_GUEST;
    char ip[64];
    int port = NET_PORT;
    snprintf(ip, sizeof ip, "%s", addr && *addr ? addr : "127.0.0.1");
    char *c = strchr(ip, ':');
    if (c) {
        *c = 0;
        port = atoi(c + 1);
    }
    snprintf(g.peer, sizeof g.peer, "%s:%d", ip, port);
    if (nt_join(ip, port) != 0) {
        g.state = NETS_FAILED;
        snprintf(g.reject, sizeof g.reject, "Cannot reach %s", g.peer);
        net_log("%s", g.reject);
        return;
    }
    g.state = NETS_CONNECTING;
    net_log("joining %s", g.peer);
    netstun_start();                                 /* (for the lobby's knock: netlobby.c) */
}

void net_stop(void)
{
    if (g.role != NET_OFF)
        net_log("stopped");
    net_upnp_close();                                /* (the router's forward, if hosting made one) */
    netstun_stop();
    nt_close();
    uint32_t world = g.world;
    int info_len = g.info_len;
    uint8_t info[NET_WORLD_INFO];
    memcpy(info, g.info, sizeof info);
    void (*handler)(const uint8_t *, int) = g.handler;
    int wanted = g.wanted_monster;
    memset(&g, 0, sizeof g);
    g.wanted_monster = wanted;
    g.msg_slot = -1;
    g.world = world;
    g.info_len = info_len;
    memcpy(g.info, info, sizeof info);
    g.handler = handler;
}

/* a peer's hello: its slot (its own kept one by its token, else an open one) and NM_WELCOME, or NM_REJECT and let go.
 * (A proto 1 hello is shorter: it reads as broken, err set, and is told it speaks another version) */
static void on_hello(int peer, NetBuf *b)
{
    NetHello h;
    net_r_hello(b, &h);
    uint8_t m[160 + NET_WORLD_INFO];
    NetBuf o = netbuf(m, sizeof m);
    char why[128] = "";
    if (h.proto != NET_PROTO)
        snprintf(why, sizeof why, "The other copy speaks another version");
    else if (b->err)
        snprintf(why, sizeof why, "Broken hello");
    else if (h.build != build_id())
        snprintf(why, sizeof why, "The two copies are different builds of RampageRE");
    int k = -1;
    if (!why[0]) {
        NetSlotPick pk[NET_SLOTS];
        for (int i = 0; i < NET_SLOTS; i++) {
            pk[i].state = (uint8_t)g.slot[i].state;
            pk[i].token = g.slot[i].token;
            pk[i].until = g.slot[i].until;
        }
        double wait = 0;
        k = net_slot_pick(pk, g.nslots, h.token, g.now, &wait);
        if (k == NET_PICK_RESERVED)
            snprintf(why, sizeof why, "A player who dropped out has their place kept for %d:%02d more; try again then",
                     (int)(wait + 0.99) / 60, (int)(wait + 0.99) % 60);
        else if (k < 0)
            snprintf(why, sizeof why, "The game is full");
    }
    if (why[0]) {
        send_reject(peer, why);
        net_log("refused a guest: %s", why);
        nt_drop(peer);
        g.peer_on[peer] = 0;
        host_state();
        return;
    }
    Slot *s = &g.slot[k];
    int back = h.token && s->token == h.token;
    if (!back)
        s->token = new_token(k);
    s->monster = h.monster;
    s->peer = peer;
    s->have_remote = 0;
    s->remote_frame = 0;
    s->remote_pressed = 0;
    g.peer_slot[peer] = k;
    net_w_u8(&o, NM_WELCOME);
    net_w_u8(&o, (uint32_t)k);
    net_w_u64(&o, s->token);
    w_world(&o);
    nt_send_to(peer, 1, m, o.len);
    net_log(back ? "the guest of slot %d came back" : "a guest joined: slot %d", k);
    slot_set(k, NSL_ONLINE);
    g.slots_dirty = 1;
}

static void on_pad(Slot *sl, NetBuf *b)
{
    uint32_t frame = net_r_u32(b);
    int n = (int)net_r_u8(b);
    if (n > NET_PAD_REDUNDANCY)
        return;
    NetPad p[NET_PAD_REDUNDANCY];
    for (int i = 0; i < n; i++) {
        p[i].lx = net_r_f32(b); p[i].ly = net_r_f32(b); p[i].rx = net_r_f32(b); p[i].ry = net_r_f32(b);
        p[i].pressed = net_r_u32(b); p[i].held = net_r_u32(b);
    }
    if (b->err || n < 1 || (sl->have_remote && frame <= sl->remote_frame))
        return;
    for (int i = 0; i < n; i++)                      /* the presses of the frames not seen yet */
        if (!sl->have_remote || frame - (uint32_t)i > sl->remote_frame)
            sl->remote_pressed |= p[i].pressed;
    sl->remote = p[0];
    sl->remote_frame = frame;
    if (!sl->have_remote)
        g.slots_dirty = 1;                           /* (its first: no longer joining, the guests told) */
    sl->have_remote = 1;
}

/* the host's own of system s nearest t that is the same as the guest's (within NET_DIGEST_WINDOW): 1 found one */
static int digest_matches(int s, double t, const NetDSys *guest, int *nearby)
{
    *nearby = 0;
    /* the edibles' slots come 4 times a second (NGP_EDIBLES), the guest showing the sample after its time: up to a
     * quarter of a second newer than the rest (a mass collapse at a block's end empties and fills slots each frame) */
    double after = s == NDS_EDIBLES ? NET_DIGEST_WINDOW + 0.25 : NET_DIGEST_WINDOW;
    for (int i = 0; i < g.ndigests; i++) {
        double dt = g.digests[i].t - t;
        if (dt < -NET_DIGEST_WINDOW || dt > after)
            continue;
        *nearby = 1;
        if (net_dsys_same(&g.digests[i].d.s[s], guest, NET_DSYS_TOL[s]))
            return 1;
    }
    return 0;
}

static void trace_dsys(const char *who, const NetDSys *d)
{
    net_log("  %s %08X %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f", who, d->h, d->v[0], d->v[1], d->v[2], d->v[3], d->v[4],
            d->v[5], d->v[6], d->v[7]);
}

static void on_digest(Slot *sl, NetBuf *b)
{
    double t_shown = net_r_f64(b), t_new = net_r_f64(b);
    NetDigest d;
    net_r_digest(b, &d);
    if (b->err || !g.ndigests)
        return;
    int checked = 0;
    for (int s = 0; s < NET_DSYS; s++) {
        double t = s == NDS_BUILDINGS ? t_new : t_shown;
        int nearby;
        int same = digest_matches(s, t, &d.s[s], &nearby);
        if (!nearby)
            continue;                                /* (not kept that far back, or a gap) */
        checked = 1;
        g.sys_state[s] = 1 + same;
        if (same) {
            sl->sys_run[s] = 0;
            continue;
        }
        if (++sl->sys_run[s] % 2 == 0) {             /* (two in a row; still so: counted again every two) */
            g.mismatches++;
            g.sys_mismatches[s]++;
            if (sl->sys_run[s] > 2 && sl->sys_run[s] % 20)
                continue;                            /* (the log: the first and every tenth) */
            net_log("MISMATCH in %s at host time %.2f (slot %d, %d checks in a row)", NET_DSYS_NAME[s], t,
                    (int)(sl - g.slot), sl->sys_run[s]);
            int best = -1;
            double bd = 1e9;
            for (int i = 0; i < g.ndigests; i++) {   /* the host's nearest, for the log */
                double dt = g.digests[i].t - t;
                if (dt < 0) dt = -dt;
                if (dt < bd) { bd = dt; best = i; }
            }
            if (best >= 0)
                trace_dsys("host ", &g.digests[best].d.s[s]);
            trace_dsys("guest", &d.s[s]);
        }
    }
    g.checks += checked;
}

void net_digest_reset(void)
{
    for (int k = 0; k < NET_SLOTS; k++)
        memset(g.slot[k].sys_run, 0, sizeof g.slot[k].sys_run);
    memset(g.sys_state, 0, sizeof g.sys_state);
    g.ndigests = g.digest_head = 0;                  /* (the guest's next digest is of the world taken again) */
}

int net_dsys_state(int s, int *mismatches)
{
    if (s < 0 || s >= NET_DSYS)
        return -1;
    if (mismatches)
        *mismatches = g.sys_mismatches[s];
    return g.sys_state[s] - 1;
}

const char *net_dsys_name(int s) { return s >= 0 && s < NET_DSYS ? NET_DSYS_NAME[s] : "?"; }
int net_dsys_count(void) { return NET_DSYS; }

static void on_data(int peer, const uint8_t *data, int len)
{
    NetBuf b = netbuf((void *)data, len);
    int type = (int)net_r_u8(&b);
    if (g.role == NET_HOST) {
        if (peer < 0 || peer >= NT_PEERS || !g.peer_on[peer])
            return;
        int k = g.peer_slot[peer];
        if (k < 0) {                                 /* saying hello: nothing else counts yet */
            if (type == NM_HELLO)
                on_hello(peer, &b);
            return;
        }
        if (g.slot[k].state != NSL_ONLINE || g.slot[k].peer != peer)
            return;
        if (type == NM_PAD)
            on_pad(&g.slot[k], &b);
        else if (type == NM_DIGEST)
            on_digest(&g.slot[k], &b);
        else if (g.handler) {
            g.msg_slot = k;
            g.handler(data, len);
            g.msg_slot = -1;
        }
        return;
    }
    switch (type) {
    case NM_WELCOME: {
        int k = (int)net_r_u8(&b);
        uint64_t token = net_r_u64(&b);
        r_world(&b);
        if (b.err || k < 1 || k >= NET_SLOTS) {
            net_log("a broken welcome");
            break;
        }
        net_log(token == g_token ? "back in our slot %d (its token kept)" : "the host gave us slot %d", k);
        g.my_slot = k;
        g_token = token;
        rejoin_save();
        g.state = NETS_CONNECTED;
        net_log("joined the host (its world %08X, ours %08X)", g.host_world, g.world);
        break;
    }
    case NM_SLOTS: {
        NetSlots s;
        net_r_slots(&b, &s);
        if (!b.err) {
            g.told = s;
            g.told_at = g.now;
        }
        break;
    }
    case NM_REJECT:
        snprintf(g.reject, sizeof g.reject, "The host said no: %.100s", (const char *)data + 1);
        net_log("%s", g.reject);
        g.state = NETS_FAILED;
        if (strstr(g.reject, "removed you")) {       /* (kicked: its token is no good now) */
            g_token = 0;
            remove(rejoin_path());
        }
        break;
    case NM_WORLD:
        r_world(&b);
        netsnap_clear(&g.snaps);
        net_log("the host's world is now %08X", g.host_world);
        break;
    case NM_SNAP: {
        double t = net_r_f64(&b);
        if (!b.err && net_live()) {
            netsnap_push(&g.snaps, t, g.now, data + b.len, len - b.len);
            g.snaps_since++;
            g.snap_bytes = len;
        }
        break;
    }
    default:
        if (g.handler && g.state == NETS_CONNECTED)
            g.handler(data, len);
    }
}

void net_update(double now)
{
    g.now = now;
    net_upnp_poll();                                 /* (the router's answer, into the log) */
    netstun_update(now);                             /* STUN's asking, the punches toward knocking guests */
    netlobby_update(now);                            /* the lobby's answers, the listing's heartbeat (netlobby.c) */
    uint64_t lobby = nts_take_pending();             /* a Steam invite taken, Join Game, +connect_lobby */
    if (lobby)
        net_start_join_steam(lobby);
    if (g.role == NET_OFF) {
        if (nts_available()) {                       /* (invites are heard while offline too) */
            NtEvent e;
            while (nts_poll(&e, now))
                ;
        }
        return;
    }
    if (g.give_up_at && now >= g.give_up_at && g.state == NETS_FAILED) {
        nt_close();
        g.give_up_at = -1;                           /* (closed; still "gave up" for net_gave_up) */
    }
    NtEvent e;
    while (nt_poll(&e, now)) {
        if (e.type == NT_CONNECT) {
            if (g.role == NET_HOST) {
                if (e.peer >= 0 && e.peer < NT_PEERS) {
                    g.peer_on[e.peer] = 1;
                    g.peer_slot[e.peer] = -1;
                }
                net_log("a guest is connecting");
                host_state();
            } else {
                g.state = NETS_HANDSHAKE;
                rejoin_load();
                uint8_t m[32];
                NetBuf b = netbuf(m, sizeof m);
                NetHello h = { NET_PROTO, build_id(), g.world, g_token, g.wanted_monster };
                net_w_u8(&b, NM_HELLO);
                net_w_hello(&b, &h);
                nt_send(1, m, b.len);
                if (g_token)
                    net_log("offering our rejoin token");
            }
        } else if (e.type == NT_DISCONNECT) {
            if (g.role == NET_HOST) {
                int k = e.peer >= 0 && e.peer < NT_PEERS ? g.peer_slot[e.peer] : -1;
                if (e.peer >= 0 && e.peer < NT_PEERS) {
                    g.peer_on[e.peer] = 0;
                    g.peer_slot[e.peer] = -1;
                }
                if (k > 0 && g.slot[k].state == NSL_ONLINE && g.slot[k].peer == e.peer) {
                    net_log("the guest of slot %d left: its place kept %.0f s", k, NET_RESERVE_S);
                    g.slot[k].until = now + NET_RESERVE_S;
                    g.slot[k].peer = -1;
                    slot_set(k, NSL_RESERVED);
                }
                host_state();
            } else {
                net_log(g.state == NETS_CONNECTING ? "nobody answered at %s" : "the host left", g.peer);
                if (!g.reject[0])
                    snprintf(g.reject, sizeof g.reject, g.state == NETS_CONNECTING ? "Nobody answered" : "The host left");
                g.state = NETS_FAILED;
            }
        } else if (e.type == NT_DATA)
            on_data(e.peer, e.data, e.len);
    }
    slots_frame();
    if (now - g.stat_at >= 1.0) {                    /* the panel's counters, once a second */
        double s = g.stat_at ? now - g.stat_at : 1;
        unsigned out = nt_bytes_out(), in = nt_bytes_in();
        g.stats.ping_ms = nt_rtt_ms();
        g.stats.snaps_per_s = (float)(g.snaps_since / s);
        g.stats.kb_out_per_s = (float)((out - g.out_at) / 1024.0 / s);
        g.stats.kb_in_per_s = (float)((in - g.in_at) / 1024.0 / s);
        g.stats.snap_bytes = g.snap_bytes;
        g.out_at = out;
        g.in_at = in;
        g.snaps_since = 0;
        g.stat_at = now;
    }
    nt_flush();
}

void net_send_pad(const NetPad *p)
{
    if (g.role != NET_GUEST || g.state != NETS_CONNECTED)
        return;
    g.pad_pending |= p->pressed;
    if (g.now - g.pad_at < 1.0 / NET_PAD_RATE)       /* (a fast screen draws more frames than the host needs) */
        return;
    g.pad_at = g.now;
    memmove(g.pads + 1, g.pads, sizeof g.pads - sizeof *g.pads);
    g.pads[0] = *p;
    g.pads[0].pressed = g.pad_pending;
    g.pad_pending = 0;
    g.pad_frame++;
    int n = g.pad_frame < NET_PAD_REDUNDANCY ? (int)g.pad_frame : NET_PAD_REDUNDANCY;
    uint8_t m[8 + NET_PAD_REDUNDANCY * 24];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_PAD);
    net_w_u32(&b, g.pad_frame);
    net_w_u8(&b, (uint32_t)n);
    for (int i = 0; i < n; i++) {
        net_w_f32(&b, g.pads[i].lx); net_w_f32(&b, g.pads[i].ly);
        net_w_f32(&b, g.pads[i].rx); net_w_f32(&b, g.pads[i].ry);
        net_w_u32(&b, g.pads[i].pressed); net_w_u32(&b, g.pads[i].held);
    }
    nt_send(0 /* unreliable */, m, b.len);
}

uint32_t net_pad_frame(void) { return g.pad_frame; }
uint32_t net_remote_frame(int k)
{
    return k > 0 && k < NET_SLOTS && g.slot[k].have_remote ? g.slot[k].remote_frame : 0;
}

int net_remote_pad(int k, NetPad *out)
{
    memset(out, 0, sizeof *out);
    if (g.role != NET_HOST || k < 1 || k >= NET_SLOTS || g.slot[k].state != NSL_ONLINE || !g.slot[k].have_remote)
        return 0;
    Slot *s = &g.slot[k];
    *out = s->remote;
    out->pressed = s->remote_pressed;
    s->remote_pressed = 0;
    return 1;
}

void net_send_snap(const void *data, int len)
{
    if (g.role != NET_HOST || !net_live())
        return;
    static uint8_t m[NET_SNAP_MAX + 16];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_SNAP);
    net_w_f64(&b, g.now);
    net_w_bytes(&b, data, len);
    if (b.err)
        return;
    send_guests(-1, 0, m, b.len);
    g.snaps_since++;
    g.snap_bytes = b.len;
}

void net_send_msg(int reliable, const void *data, int len)
{
    if (g.role == NET_HOST)
        send_guests(-1, reliable, data, len);
    else if (g.role != NET_OFF && g.state == NETS_CONNECTED)
        nt_send(reliable, data, len);
}

void net_send_msg_except(int slot, int reliable, const void *data, int len)
{
    if (g.role == NET_HOST)
        send_guests(slot, reliable, data, len);
}

void net_keep_digest(double t_host, const void *digest)
{
    if (g.ndigests && t_host - g.digests[(g.digest_head + DIGESTS - 1) % DIGESTS].t < 1.0 / 30 - 0.002)
        return;                                      /* (30 a second is plenty for the window) */
    g.digests[g.digest_head].t = t_host;
    memcpy(&g.digests[g.digest_head].d, digest, sizeof(NetDigest));
    g.digest_head = (g.digest_head + 1) % DIGESTS;
    if (g.ndigests < DIGESTS)
        g.ndigests++;
}

void net_send_digest(double t_shown, double t_new, const void *digest)
{
    if (g.role != NET_GUEST || !net_live())
        return;
    uint8_t m[32 + NET_DSYS * (4 + 4 * NET_DVALS)];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_DIGEST);
    net_w_f64(&b, t_shown);
    net_w_f64(&b, t_new);
    net_w_digest(&b, (const NetDigest *)digest);
    if (!b.err)
        nt_send(1, m, b.len);
}

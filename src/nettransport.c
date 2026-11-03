/* nettransport.c - the connections, host or guest (nettransport.h): ENet (IP) here, Steam in netsteam.c. The host's
 * ENet host takes NT_PEERS peers; each keeps its index (g_peers[i]) for as long as it is connected, and the session
 * (net.c) says which player slot each one plays */
#include "nettransport.h"
#include "netstun.h"
#include "version.h"
#include <enet/enet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ENetHost *g_host;
static ENetPeer *g_peers[NT_PEERS];                  /* the other sides (a guest: the host, 0) */
static ENetPacket *g_last;                           /* the packet the last NT_DATA points into */
static int g_inited, g_lag_ms, g_jit_ms, g_loss, g_hosting;
static unsigned g_out, g_in;

/* the bad network, for testing (RAMPAGE_NETSIM=latency,jitter,loss: ms, ms, %; or RAMPAGE_NET_LAG, _JITTER, _LOSS):
 * messages held back latency +- jitter ms before they go, in the order due (SimPacket: drawn per peer per frame); an
 * unreliable one overtaken by a newer one is dropped (as ENet's channel drops it), reliable ones keep their order, and
 * in a 'lost' frame the unreliable ones are gone and the reliable ones come a resend later (twice the latency, 50 ms
 * at least), as ENet's resending would make them. The latency is one way: a round trip is twice it */
typedef struct Held { double due; unsigned seq; int peer, reliable, len; struct Held *next; unsigned char data[]; } Held;   /* (peer
                                                      * -1: every connected one) */
static Held *g_held, **g_held_tail = &g_held;
static double g_now;
/* each peer's packet of this moment: what ENet sends to a peer in one go is one UDP packet, which is late, or lost, as
 * a whole; so the delay and the loss are drawn once per peer per frame (g_now), and a frame's messages stay together */
typedef struct SimPacket { double at; double ms; int lost; double rel_due; } SimPacket;
static SimPacket g_sim_pk[NT_PEERS];
static unsigned g_sim_dropped, g_sim_resent;
static unsigned g_sim_seq[NT_PEERS], g_sim_last[NT_PEERS];   /* (each peer's unreliable messages: the order sent, the
                                                      * newest let go; one older than that is dropped, as ENet's
                                                      * unreliable channel drops one overtaken on the way) */

static void env_init(void)
{
    const char *s = getenv("RAMPAGE_NET_LAG");
    g_lag_ms = s ? atoi(s) : 0;
    s = getenv("RAMPAGE_NET_JITTER");
    g_jit_ms = s ? atoi(s) : 0;
    s = getenv("RAMPAGE_NET_LOSS");
    g_loss = s ? atoi(s) : 0;
    s = getenv("RAMPAGE_NETSIM");
    if (s && *s) {
        int lat = 0, jit = 0, loss = 0;
        sscanf(s, "%d%*[ ,;]%d%*[ ,;]%d", &lat, &jit, &loss);   /* (80,30,5; PowerShell's 80 30 5) */
        g_lag_ms = lat;
        g_jit_ms = jit;
        g_loss = loss;
    }
    if (g_lag_ms < 0) g_lag_ms = 0;
    if (g_jit_ms < 0) g_jit_ms = 0;
    if (g_jit_ms > g_lag_ms) g_jit_ms = g_lag_ms;     /* (no message due before it was sent) */
    if (g_loss < 0) g_loss = 0;
    if (g_loss > 100) g_loss = 100;
    if (g_lag_ms || g_jit_ms || g_loss)
        printf("net: netsim: %d ms latency +- %d ms jitter, %d%% loss (each way)\n", g_lag_ms, g_jit_ms, g_loss);
}

static int lib_init(void)
{
    if (!g_inited) {
        if (enet_initialize() != 0)
            return -1;
        g_inited = 1;
        env_init();
    }
    return 0;
}

static void nte_close(void);

/* what arrives on ENet's socket that is not ENet's: a STUN server's answer, a host's punches (netstun.c) */
static int ENET_CALLBACK nte_intercept(ENetHost *h, ENetEvent *e)
{
    (void)e;
    if (g_hosting && h->receivedDataLength == sizeof NT_LAN_ASK - 1 &&
        memcmp(h->receivedData, NT_LAN_ASK, sizeof NT_LAN_ASK - 1) == 0) {
        char say[64];                                /* (a launcher looking for games on the network: this one) */
        int n = snprintf(say, sizeof say, "%s %s %d", NT_LAN_SAY, RAMPAGE_VERSION, (int)h->address.port);
        ENetBuffer b;
        b.data = say;
        b.dataLength = (size_t)n;
        enet_socket_send(h->socket, &h->receivedAddress, &b, 1);
        return 1;
    }
    if (netstun_is_punch(h->receivedData, (int)h->receivedDataLength))
        return 1;
    if (!netstun_is_stun(h->receivedData, (int)h->receivedDataLength))
        return 0;
    netstun_received(h->receivedData, (int)h->receivedDataLength);
    return 1;
}

static int nte_host(int port)
{
    nte_close();
    if (lib_init())
        return -1;
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = (enet_uint16)port;
    g_host = enet_host_create(&a, NT_PEERS, 2, 0, 0);
    if (g_host)                                      /* (no ENet compression: a copy of another version could not read
                                                      * the first packet, and so not be told it is another version;
                                                      * the deltas, NET_PROTO 7, did nearly all of it) */
        g_host->intercept = nte_intercept;
    g_hosting = g_host != NULL;
    return g_host ? 0 : -1;
}

static int nte_join(const char *ip, int port)
{
    nte_close();
    if (lib_init())
        return -1;
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = 0;                                      /* (bound now, any port: STUN's answer and the lobby's knock name
                                                      * it before the first ENet packet goes) */
    g_host = enet_host_create(&a, 1, 2, 0, 0);
    if (!g_host)
        return -1;
    g_host->intercept = nte_intercept;
    if (enet_address_set_host(&a, ip) != 0)
        return -1;
    a.port = (enet_uint16)port;
    g_peers[0] = enet_host_connect(g_host, &a, 2, 0);
    return g_peers[0] ? 0 : -1;
}

static void nte_close(void)
{
    g_hosting = 0;
    if (g_last) {
        enet_packet_destroy(g_last);
        g_last = NULL;
    }
    while (g_held) {
        Held *h = g_held;
        g_held = h->next;
        free(h);
    }
    g_held_tail = &g_held;
    for (int i = 0; i < NT_PEERS; i++)
        if (g_peers[i]) {
            enet_peer_disconnect_now(g_peers[i], 0);
            g_peers[i] = NULL;
        }
    if (g_host) {
        enet_host_destroy(g_host);
        g_host = NULL;
    }
}

static int send_one(ENetPeer *to, int reliable, const void *data, int len)
{
    if (!to || to->state != ENET_PEER_STATE_CONNECTED)
        return -1;
    ENetPacket *p = enet_packet_create(data, (size_t)len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!p)
        return -1;
    g_out += (unsigned)len;
    if (enet_peer_send(to, reliable ? 0 : 1, p) != 0) {
        enet_packet_destroy(p);
        return -1;
    }
    return 0;
}

static int send_now(int peer, int reliable, const void *data, int len)
{
    if (peer >= 0)
        return peer < NT_PEERS ? send_one(g_peers[peer], reliable, data, len) : -1;
    int r = -1;
    for (int i = 0; i < NT_PEERS; i++)               /* (each its own packet: a lost one is that peer's) */
        if (g_peers[i] && send_one(g_peers[i], reliable, data, len) == 0)
            r = 0;
    return r;
}

static int nte_send(int peer, int reliable, const void *data, int len)
{
    if (peer < 0 && (g_lag_ms > 0 || g_loss > 0)) {  /* (to each its own: a loss, a jitter is that peer's) */
        int r = -1;
        for (int i = 0; i < NT_PEERS; i++)
            if (g_peers[i] && g_peers[i]->state == ENET_PEER_STATE_CONNECTED && nte_send(i, reliable, data, len) == 0)
                r = 0;
        return r;
    }
    if (peer < 0 || peer >= NT_PEERS)
        return send_now(peer, reliable, data, len);
    SimPacket *pk = &g_sim_pk[peer];
    if (pk->at != g_now) {                           /* (this frame's packet to that peer: its fate) */
        pk->at = g_now;
        pk->lost = g_loss > 0 && rand() % 100 < g_loss;
        pk->ms = g_lag_ms + (g_jit_ms > 0 ? (rand() % (2 * g_jit_ms + 1)) - g_jit_ms : 0);
    }
    if (pk->lost && !reliable) {
        g_sim_dropped++;
        return 0;
    }
    if (g_lag_ms <= 0 && !pk->lost)
        return send_now(peer, reliable, data, len);
    double ms = pk->ms;
    if (pk->lost) {                                  /* (a reliable one: ENet would send it again) */
        ms += g_lag_ms * 2 > 50 ? g_lag_ms * 2 : 50;
        g_sim_resent++;
    }
    double due = g_now + ms / 1000.0;
    if (reliable) {                                  /* (in order: never before the last) */
        if (due < pk->rel_due)
            due = pk->rel_due;
        pk->rel_due = due;
    }
    Held *h = malloc(sizeof *h + (size_t)len);
    if (!h)
        return -1;
    h->due = due;
    h->peer = peer;
    h->reliable = reliable;
    h->len = len;
    h->seq = !reliable && peer >= 0 && peer < NT_PEERS ? ++g_sim_seq[peer] : 0;
    Held **at = &g_held;                             /* (in the order due; equal ones in the order sent) */
    while (*at && (*at)->due <= due)
        at = &(*at)->next;
    h->next = *at;
    *at = h;
    if (!h->next)
        g_held_tail = &h->next;
    memcpy(h->data, data, (size_t)len);
    return 0;
}

void nt_netsim_stats(unsigned *dropped, unsigned *resent)
{
    if (dropped) *dropped = g_sim_dropped;
    if (resent) *resent = g_sim_resent;
}

static int nte_poll(NtEvent *ev, double now)
{
    g_now = now;
    ev->type = NT_NONE;
    ev->peer = 0;
    ev->data = NULL;
    ev->len = 0;
    if (g_last) {
        enet_packet_destroy(g_last);
        g_last = NULL;
    }
    if (!g_host)
        return 0;
    while (g_held && g_held->due <= now) {           /* the lag's messages that are due */
        Held *h = g_held;
        g_held = h->next;
        if (!g_held)
            g_held_tail = &g_held;
        if (h->seq && h->seq < g_sim_last[h->peer])
            g_sim_dropped++;                         /* (overtaken: ENet would drop it on arrival) */
        else {
            if (h->seq)
                g_sim_last[h->peer] = h->seq;
            send_now(h->peer, h->reliable, h->data, h->len);
        }
        free(h);
    }
    ENetEvent e;
    while (enet_host_service(g_host, &e, 0) > 0) {
        int i = 0;
        while (i < NT_PEERS && g_peers[i] != e.peer)
            i++;
        switch (e.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (i == NT_PEERS) {                     /* a new one (the guest's own peer 0 is in already) */
                for (i = 0; i < NT_PEERS && g_peers[i]; i++)
                    ;
                if (i == NT_PEERS) {                 /* (ENet takes no more than NT_PEERS: not reached) */
                    enet_peer_disconnect_now(e.peer, 0);
                    continue;
                }
                g_peers[i] = e.peer;
                g_sim_seq[i] = g_sim_last[i] = 0;
                memset(&g_sim_pk[i], 0, sizeof g_sim_pk[i]);
            }
            enet_peer_timeout(e.peer, 0, 5000, 10000);
            ev->type = NT_CONNECT;
            ev->peer = i;
            return 1;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (i == NT_PEERS)
                continue;
            g_peers[i] = NULL;
            ev->type = NT_DISCONNECT;
            ev->peer = i;
            return 1;
        case ENET_EVENT_TYPE_RECEIVE:
            if (i == NT_PEERS) {                     /* (a peer let go: its last packets) */
                enet_packet_destroy(e.packet);
                continue;
            }
            g_last = e.packet;
            g_in += (unsigned)e.packet->dataLength;
            ev->type = NT_DATA;
            ev->peer = i;
            ev->data = e.packet->data;
            ev->len = (int)e.packet->dataLength;
            return 1;
        default:
            break;
        }
    }
    return 0;
}

static void nte_flush(void) { if (g_host) enet_host_flush(g_host); }
static int nte_connected(void)
{
    for (int i = 0; i < NT_PEERS; i++)
        if (g_peers[i] && g_peers[i]->state == ENET_PEER_STATE_CONNECTED)
            return 1;
    return 0;
}
/* the test network's part of a round trip: ENet's own pings are not held back, so its time is added (both ends run
 * with it, as the test scripts do: there and back) */
static int sim_rtt(void) { return 2 * g_lag_ms; }
static int nte_peer_rtt_ms(int peer)
{
    return peer >= 0 && peer < NT_PEERS && g_peers[peer] ? (int)g_peers[peer]->roundTripTime + sim_rtt() : 0;
}
static int nte_peer_silent_ms(int peer)
{
    if (peer < 0 || peer >= NT_PEERS || !g_peers[peer])
        return 1 << 30;
    return (int)(enet_time_get() - g_peers[peer]->lastReceiveTime);
}
static int nte_rtt_ms(void)
{
    for (int i = 0; i < NT_PEERS; i++)
        if (g_peers[i] && g_peers[i]->state == ENET_PEER_STATE_CONNECTED)
            return (int)g_peers[i]->roundTripTime + sim_rtt();
    return 0;
}
/* a refused guest: ENet sends what is queued for it (the NM_REJECT) and then says goodbye; its NT_DISCONNECT is not
 * reported (it never played) */
static void nte_drop(int peer)
{
    if (peer < 0 || peer >= NT_PEERS || !g_peers[peer])
        return;
    for (Held **h = &g_held; *h;) {                  /* (RAMPAGE_NET_LAG: what it was told goes now, not lost) */
        Held *x = *h;
        if (x->peer != peer) {
            h = &x->next;
            continue;
        }
        send_now(peer, x->reliable, x->data, x->len);
        *h = x->next;
        free(x);
    }
    g_held_tail = &g_held;
    while (*g_held_tail)
        g_held_tail = &(*g_held_tail)->next;
    enet_peer_disconnect_later(g_peers[peer], 0);
    g_peers[peer] = NULL;
}
static unsigned nte_bytes_out(void) { return g_out; }

static int g_steam_on(void);

int nt_raw_send(uint32_t ip, int port, const void *data, int len)
{
    if (!g_host || g_steam_on())
        return -1;
    ENetAddress a;
    a.host = ENET_HOST_TO_NET_32(ip);
    a.port = (enet_uint16)port;
    ENetBuffer b;
    b.data = (void *)data;
    b.dataLength = (size_t)len;
    return enet_socket_send(g_host->socket, &a, &b, 1) == len ? 0 : -1;
}

int nt_local_port(void) { return g_host ? g_host->address.port : 0; }
static unsigned nte_bytes_in(void) { return g_in; }

/* ---- the back end in use: ENet (an address) or Steam (a lobby) */
static int g_steam;
static int g_steam_on(void) { return g_steam; }

int nt_host(int port) { nt_close(); g_steam = 0; return nte_host(port); }
int nt_join(const char *ip, int port) { nt_close(); g_steam = 0; return nte_join(ip, port); }
int nt_host_steam(void) { nt_close(); g_steam = 1; return nts_host(); }
int nt_join_steam(uint64_t lobby) { nt_close(); g_steam = 1; return nts_join_lobby(lobby); }
int nt_is_steam(void) { return g_steam; }
void nt_close(void) { nte_close(); nts_close(); }
int nt_send_to(int peer, int reliable, const void *data, int len)
{
    if (!g_steam)
        return nte_send(peer, reliable, data, len);
    if (g_lag_ms <= 0)
        return nts_send_to(peer, reliable, data, len);
    return nte_send(peer, reliable, data, len);      /* (the lag's queue: held, then sent by nt_poll's) */
}
int nt_send(int reliable, const void *data, int len) { return nt_send_to(-1, reliable, data, len); }
void nt_drop(int peer) { if (g_steam) nts_drop(peer); else nte_drop(peer); }
int nt_poll(NtEvent *ev, double now)
{
    if (!g_steam)
        return nte_poll(ev, now);
    g_now = now;
    while (g_held && g_held->due <= now) {           /* RAMPAGE_NET_LAG on Steam too */
        Held *h = g_held;
        g_held = h->next;
        if (!g_held)
            g_held_tail = &g_held;
        if (h->reliable || g_loss <= 0 || rand() % 100 >= g_loss)
            nts_send_to(h->peer, h->reliable, h->data, h->len);
        free(h);
    }
    ev->peer = 0;
    return nts_poll(ev, now);
}
void nt_flush(void) { if (!g_steam) nte_flush(); }
int nt_connected(void) { return g_steam ? nts_connected() : nte_connected(); }
int nt_rtt_ms(void) { return g_steam ? 0 : nte_rtt_ms(); }
int nt_peer_rtt_ms(int peer) { return g_steam ? 0 : nte_peer_rtt_ms(peer); }
int nt_peer_silent_ms(int peer) { return g_steam ? 0 : nte_peer_silent_ms(peer); }
int nt_peer_addr(int peer, char *out, int cap)
{
    if (cap > 0)
        out[0] = 0;
    if (g_steam) {
        snprintf(out, (size_t)cap, "Steam");
        return 0;
    }
    if (peer < 0 || peer >= NT_PEERS || !g_peers[peer])
        return -1;
    char ip[64];
    if (enet_address_get_host_ip(&g_peers[peer]->address, ip, sizeof ip) != 0)
        return -1;
    snprintf(out, (size_t)cap, "%s:%u", ip, (unsigned)g_peers[peer]->address.port);
    return 0;
}
unsigned nt_bytes_out(void) { return g_steam ? nts_bytes_out() : nte_bytes_out(); }
unsigned nt_bytes_in(void) { return g_steam ? nts_bytes_in() : nte_bytes_in(); }

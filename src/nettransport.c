/* nettransport.c - the connections, host or guest (nettransport.h): ENet (IP) here, Steam in netsteam.c. The host's
 * ENet host takes NT_PEERS peers; each keeps its index (g_peers[i]) for as long as it is connected, and the session
 * (net.c) says which player slot each one plays */
#include "nettransport.h"
#include <enet/enet.h>
#include <stdlib.h>
#include <string.h>

static ENetHost *g_host;
static ENetPeer *g_peers[NT_PEERS];                  /* the other sides (a guest: the host, 0) */
static ENetPacket *g_last;                           /* the packet the last NT_DATA points into */
static int g_inited, g_lag_ms, g_loss;
static unsigned g_out, g_in;

/* RAMPAGE_NET_LAG: messages held back before they go */
typedef struct Held { double due; int peer, reliable, len; struct Held *next; unsigned char data[]; } Held;   /* (peer
                                                      * -1: every connected one) */
static Held *g_held, **g_held_tail = &g_held;
static double g_now;

static void env_init(void)
{
    const char *s = getenv("RAMPAGE_NET_LAG");
    g_lag_ms = s ? atoi(s) : 0;
    s = getenv("RAMPAGE_NET_LOSS");
    g_loss = s ? atoi(s) : 0;
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

static int nte_host(int port)
{
    nte_close();
    if (lib_init())
        return -1;
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = (enet_uint16)port;
    g_host = enet_host_create(&a, NT_PEERS, 2, 0, 0);
    return g_host ? 0 : -1;
}

static int nte_join(const char *ip, int port)
{
    nte_close();
    if (lib_init())
        return -1;
    g_host = enet_host_create(NULL, 1, 2, 0, 0);
    if (!g_host)
        return -1;
    ENetAddress a;
    if (enet_address_set_host(&a, ip) != 0)
        return -1;
    a.port = (enet_uint16)port;
    g_peers[0] = enet_host_connect(g_host, &a, 2, 0);
    return g_peers[0] ? 0 : -1;
}

static void nte_close(void)
{
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
    if (!reliable && g_loss > 0 && rand() % 100 < g_loss)
        return 0;
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
    if (g_lag_ms <= 0)
        return send_now(peer, reliable, data, len);
    Held *h = malloc(sizeof *h + (size_t)len);
    if (!h)
        return -1;
    h->due = g_now + g_lag_ms / 1000.0;
    h->peer = peer;
    h->reliable = reliable;
    h->len = len;
    h->next = NULL;
    memcpy(h->data, data, (size_t)len);
    *g_held_tail = h;
    g_held_tail = &h->next;
    return 0;
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
        send_now(h->peer, h->reliable, h->data, h->len);
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
static int nte_peer_rtt_ms(int peer)
{
    return peer >= 0 && peer < NT_PEERS && g_peers[peer] ? (int)g_peers[peer]->roundTripTime : 0;
}
static int nte_rtt_ms(void)
{
    for (int i = 0; i < NT_PEERS; i++)
        if (g_peers[i] && g_peers[i]->state == ENET_PEER_STATE_CONNECTED)
            return (int)g_peers[i]->roundTripTime;
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
static unsigned nte_bytes_in(void) { return g_in; }

/* ---- the back end in use: ENet (an address) or Steam (a lobby) */
static int g_steam;

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
        return nts_send(reliable, data, len);        /* (Steam's one peer) */
    return nte_send(peer, reliable, data, len);      /* (the lag's queue: held, then sent by nt_poll's) */
}
int nt_send(int reliable, const void *data, int len) { return nt_send_to(-1, reliable, data, len); }
void nt_drop(int peer) { if (!g_steam) nte_drop(peer); }
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
            nts_send(h->reliable, h->data, h->len);
        free(h);
    }
    ev->peer = 0;                                    /* (Steam's one peer) */
    return nts_poll(ev, now);
}
void nt_flush(void) { if (!g_steam) nte_flush(); }
int nt_connected(void) { return g_steam ? nts_connected() : nte_connected(); }
int nt_rtt_ms(void) { return g_steam ? 0 : nte_rtt_ms(); }
int nt_peer_rtt_ms(int peer) { return g_steam ? 0 : nte_peer_rtt_ms(peer); }
unsigned nt_bytes_out(void) { return g_steam ? nts_bytes_out() : nte_bytes_out(); }
unsigned nt_bytes_in(void) { return g_steam ? nts_bytes_in() : nte_bytes_in(); }

/* nettransport.c - one ENet connection, host or guest (nettransport.h) */
#include "nettransport.h"
#include <enet/enet.h>
#include <stdlib.h>
#include <string.h>

static ENetHost *g_host;
static ENetPeer *g_peer;                             /* the one other side */
static ENetPacket *g_last;                           /* the packet the last NT_DATA points into */
static int g_inited, g_lag_ms, g_loss;
static unsigned g_out, g_in;

/* RAMPAGE_NET_LAG: messages held back before they go */
typedef struct Held { double due; int reliable, len; struct Held *next; unsigned char data[]; } Held;
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

int nt_host(int port)
{
    nt_close();
    if (lib_init())
        return -1;
    ENetAddress a;
    a.host = ENET_HOST_ANY;
    a.port = (enet_uint16)port;
    g_host = enet_host_create(&a, 1, 2, 0, 0);
    return g_host ? 0 : -1;
}

int nt_join(const char *ip, int port)
{
    nt_close();
    if (lib_init())
        return -1;
    g_host = enet_host_create(NULL, 1, 2, 0, 0);
    if (!g_host)
        return -1;
    ENetAddress a;
    if (enet_address_set_host(&a, ip) != 0)
        return -1;
    a.port = (enet_uint16)port;
    g_peer = enet_host_connect(g_host, &a, 2, 0);
    return g_peer ? 0 : -1;
}

void nt_close(void)
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
    if (g_peer) {
        enet_peer_disconnect_now(g_peer, 0);
        g_peer = NULL;
    }
    if (g_host) {
        enet_host_destroy(g_host);
        g_host = NULL;
    }
}

static int send_now(int reliable, const void *data, int len)
{
    if (!g_peer || g_peer->state != ENET_PEER_STATE_CONNECTED)
        return -1;
    if (!reliable && g_loss > 0 && rand() % 100 < g_loss)
        return 0;
    ENetPacket *p = enet_packet_create(data, (size_t)len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!p)
        return -1;
    g_out += (unsigned)len;
    return enet_peer_send(g_peer, reliable ? 0 : 1, p);
}

int nt_send(int reliable, const void *data, int len)
{
    if (g_lag_ms <= 0)
        return send_now(reliable, data, len);
    Held *h = malloc(sizeof *h + (size_t)len);
    if (!h)
        return -1;
    h->due = g_now + g_lag_ms / 1000.0;
    h->reliable = reliable;
    h->len = len;
    h->next = NULL;
    memcpy(h->data, data, (size_t)len);
    *g_held_tail = h;
    g_held_tail = &h->next;
    return 0;
}

int nt_poll(NtEvent *ev, double now)
{
    g_now = now;
    ev->type = NT_NONE;
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
        send_now(h->reliable, h->data, h->len);
        free(h);
    }
    ENetEvent e;
    while (enet_host_service(g_host, &e, 0) > 0) {
        switch (e.type) {
        case ENET_EVENT_TYPE_CONNECT:
            if (g_peer && g_peer != e.peer) {        /* one guest at a time */
                enet_peer_disconnect_now(e.peer, 0);
                continue;
            }
            g_peer = e.peer;
            enet_peer_timeout(g_peer, 0, 5000, 10000);
            ev->type = NT_CONNECT;
            return 1;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (e.peer != g_peer)
                continue;
            g_peer = NULL;
            ev->type = NT_DISCONNECT;
            return 1;
        case ENET_EVENT_TYPE_RECEIVE:
            g_last = e.packet;
            g_in += (unsigned)e.packet->dataLength;
            ev->type = NT_DATA;
            ev->data = e.packet->data;
            ev->len = (int)e.packet->dataLength;
            return 1;
        default:
            break;
        }
    }
    return 0;
}

void nt_flush(void) { if (g_host) enet_host_flush(g_host); }
int nt_connected(void) { return g_peer && g_peer->state == ENET_PEER_STATE_CONNECTED; }
int nt_rtt_ms(void) { return g_peer ? (int)g_peer->roundTripTime : 0; }
unsigned nt_bytes_out(void) { return g_out; }
unsigned nt_bytes_in(void) { return g_in; }

/* netstun.c - STUN and hole punching (netstun.h). The STUN server's name is looked up on a thread of its own (a slow
 * DNS never holds the frame); the asking, the answers and the punches all go through ENet's own socket
 * (nettransport.c: nt_raw_send, and its intercept hands the answers here), since the public address a router gives is
 * per socket: one asked from another socket would be another address.
 *
 * Built with -DNETSTUN_TEST (tests/build_net_test.bat) only the parsers are here: no network, no net.c. */
#include "netstun.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC 0x2112A442u                            /* RFC 5389's magic cookie, bytes 4-7 of every message */

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

int netstun_request(uint8_t out[20], const uint8_t txid[12])
{
    static const uint8_t head[8] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };   /* Binding, no attributes */
    memcpy(out, head, 8);
    memcpy(out + 8, txid, 12);
    return 20;
}

int netstun_is_stun(const uint8_t *p, int len) { return len >= 20 && (p[0] & 0xC0) == 0 && rd32(p + 4) == MAGIC; }

int netstun_is_punch(const uint8_t *p, int len) { return len == 8 && !memcmp(p, NETSTUN_PUNCH, 8); }

int netstun_parse(const uint8_t *p, int len, const uint8_t txid[12], uint32_t *ip, uint16_t *port)
{
    if (!netstun_is_stun(p, len) || rd16(p) != 0x0101 || memcmp(p + 8, txid, 12))
        return -1;                                   /* (a Binding Success Response, to our request) */
    int end = 20 + rd16(p + 2), found = -1;
    if (end > len)
        return -1;
    for (int a = 20; a + 4 <= end;) {
        int type = rd16(p + a), n = rd16(p + a + 2);
        const uint8_t *v = p + a + 4;
        if (a + 4 + n > end)
            return -1;
        if ((type == 0x0020 || type == 0x0001) && n >= 8 && v[1] == 0x01) {   /* (XOR-)MAPPED-ADDRESS, IPv4 */
            uint32_t i = rd32(v + 4);
            uint16_t pt = rd16(v + 2);
            if (type == 0x0020) {
                i ^= MAGIC;
                pt ^= (uint16_t)(MAGIC >> 16);
            }
            if (type == 0x0020 || found < 0) {      /* (the XOR one wins: some routers rewrite the plain one) */
                *ip = i;
                *port = pt;
                found = type;
            }
        }
        a += 4 + ((n + 3) & ~3);                     /* (attributes are padded to 4 bytes) */
    }
    return found >= 0 ? 0 : -1;
}

/* "a.b.c.d:port" -> ip (host byte order) and port; -1 not that */
static int parse_addr(const char *s, uint32_t *ip, uint16_t *port)
{
    unsigned a, b, c, d, pt;
    char tail;
    if (sscanf(s, "%u.%u.%u.%u:%u%c", &a, &b, &c, &d, &pt, &tail) != 5 || a > 255 || b > 255 || c > 255 || d > 255 ||
        pt < 1 || pt > 65535)
        return -1;
    *ip = a << 24 | b << 16 | c << 8 | d;
    *port = (uint16_t)pt;
    return 0;
}

int netstun_parse_knocks(const char *text, char out[][24], int max)
{
    int n = 0;
    for (const char *p = text; p && *p && n < max; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL) {
        char addr[32];
        uint32_t ip;
        uint16_t port;
        if (sscanf(p, "knock %31s", addr) == 1 && !parse_addr(addr, &ip, &port))
            snprintf(out[n++], 24, "%s", addr);
    }
    return n;
}

#ifndef NETSTUN_TEST
/* ---- the network ---------------------------------------------------------------------------------------------- */
#include "net.h"
#include "nettransport.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#endif

#define TRIES 6                                      /* requests, half a second apart, before giving up */
#define REFRESH_S 20                                 /* asked again: keeps the router's mapping open, sees it change */
#define PUNCH_S 6                                    /* how long a knock is punched toward */
#define PUNCHES 8

static struct {
    int on, tries, gave_up, told;
    double next_at;
    uint8_t txid[12];
    uint32_t pub_ip;                                 /* host byte order; 0 not yet */
    uint16_t pub_port;
    char pub[24];
    struct { uint32_t ip; uint16_t port; double until, next; } punch[PUNCHES];
} S;

/* the server's address, from the lookup thread: resolved 1 found, -1 not */
static volatile int g_resolved;
static uint32_t g_server_ip;
static uint16_t g_server_port;
static char g_server_name[128];

static void resolve(void)
{
    char host[128];
    snprintf(host, sizeof host, "%s", g_server_name);
    char *c = strrchr(host, ':');
    const char *port = c ? c + 1 : "3478";
    if (c)
        *c = 0;
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    int r = -1;
    if (!getaddrinfo(host, port, &hints, &ai) && ai) {
        struct sockaddr_in *sa = (struct sockaddr_in *)ai->ai_addr;
        g_server_ip = ntohl(sa->sin_addr.s_addr);
        g_server_port = ntohs(sa->sin_port);
        r = 1;
    }
    if (ai)
        freeaddrinfo(ai);
    __atomic_store_n(&g_resolved, r, __ATOMIC_RELEASE);
}

#ifdef _WIN32
static DWORD WINAPI resolve_thread(LPVOID p) { (void)p; resolve(); return 0; }
#else
static void *resolve_thread(void *p) { (void)p; resolve(); return NULL; }
#endif

void netstun_start(void)
{
    memset(&S, 0, sizeof S);
    const char *env = getenv("RAMPAGE_STUN");
    if (env && !strcmp(env, "0")) {
        S.gave_up = 1;
        return;
    }
    S.on = 1;
    if (g_resolved > 0 && !env)                      /* (looked up already, by an earlier game) */
        return;
    snprintf(g_server_name, sizeof g_server_name, "%s", env && *env ? env : NETSTUN_SERVER);
    g_resolved = 0;
#ifdef _WIN32
    HANDLE t = CreateThread(NULL, 0, resolve_thread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
    else
        resolve();
#else
    pthread_t t;
    if (pthread_create(&t, NULL, resolve_thread, NULL) == 0)
        pthread_detach(t);
    else
        resolve();
#endif
}

void netstun_stop(void) { memset(&S, 0, sizeof S); }

const char *netstun_public(void) { return S.pub; }
int netstun_done(void) { return S.pub[0] || S.gave_up; }

void netstun_received(const uint8_t *p, int len)
{
    uint32_t ip;
    uint16_t port;
    if (!S.on || netstun_parse(p, len, S.txid, &ip, &port))
        return;
    if (ip != S.pub_ip || port != S.pub_port)
        S.told = 0;                                  /* (new, or the router moved it: said in the log) */
    S.pub_ip = ip;
    S.pub_port = port;
    snprintf(S.pub, sizeof S.pub, "%u.%u.%u.%u:%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255, port);
    S.tries = 0;
    S.next_at = 0;                                   /* (the refresh: counted from the next update) */
}

void netstun_punch(const char *addr)
{
    uint32_t ip;
    uint16_t port;
    if (parse_addr(addr, &ip, &port))
        return;
    int k = 0;
    for (int i = 0; i < PUNCHES; i++) {
        if (S.punch[i].ip == ip && S.punch[i].port == port) {
            k = i;
            break;
        }
        if (S.punch[i].until < S.punch[k].until)     /* (else the oldest one's place) */
            k = i;
    }
    S.punch[k].ip = ip;
    S.punch[k].port = port;
    S.punch[k].until = -1;                           /* (set from the next update's clock) */
    S.punch[k].next = 0;
    net_log("punching toward %s (a guest knocked)", addr);
}

void netstun_update(double now)
{
    for (int i = 0; i < PUNCHES; i++) {
        if (!S.punch[i].port)
            continue;
        if (S.punch[i].until < 0)
            S.punch[i].until = now + PUNCH_S;
        if (now > S.punch[i].until) {
            S.punch[i].port = 0;
            continue;
        }
        if (now >= S.punch[i].next) {
            nt_raw_send(S.punch[i].ip, S.punch[i].port, NETSTUN_PUNCH, 8);
            S.punch[i].next = now + 0.25;
        }
    }
    if (!S.on)
        return;
    if (S.pub[0] && !S.told) {
        S.told = 1;
        net_log("STUN: this game is %s on the internet", S.pub);
    }
    if (S.pub[0] && S.next_at == 0)
        S.next_at = now + REFRESH_S;
    int resolved = __atomic_load_n(&g_resolved, __ATOMIC_ACQUIRE);
    if (resolved < 0 && !S.gave_up) {
        S.gave_up = 1;
        net_log("STUN: %s not found (no internet, or no DNS): friends join with UPnP, a forward, a LAN or a VPN",
                g_server_name);
    }
    if (resolved <= 0 || now < S.next_at)
        return;
    if (S.tries >= TRIES) {
        if (!S.pub[0] && !S.gave_up)
            net_log("STUN: %s did not answer: friends join with UPnP, a forward, a LAN or a VPN", g_server_name);
        S.gave_up = 1;
        S.next_at = now + REFRESH_S;                 /* (tried again later all the same) */
        S.tries = 0;
        return;
    }
    for (int i = 0; i < 12; i++)
        S.txid[i] = (uint8_t)(rand() >> 4);
    uint8_t req[20];
    nt_raw_send(g_server_ip, g_server_port, req, netstun_request(req, S.txid));
    S.tries++;
    S.next_at = now + 0.5;
}
#endif

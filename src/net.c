/* net.c - online play's session (net.h): the connection, the handshake, player 2's pad, the snapshots, the log */
#include "net.h"
#include "netwire.h"
#include "nettransport.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define LOG_LINES 200
#define DIGESTS 128                                  /* the host's own, 20 a second: six seconds */

static struct {
    int role, state;
    double now;
    char peer[64], reject[128];
    uint32_t world, host_world;
    uint8_t info[NET_WORLD_INFO], host_info[NET_WORLD_INFO];
    int info_len, host_info_len;
    void (*handler)(const uint8_t *, int);
    /* the guest */
    uint32_t pad_frame;
    NetPad pads[NET_PAD_REDUNDANCY];                 /* newest first */
    NetSnapBuf snaps;
    /* the host */
    NetPad remote;                                   /* the newest pad, and the presses not yet taken */
    uint32_t remote_frame, remote_pressed;
    int have_remote;
    struct { double t; uint32_t d; } digests[DIGESTS];
    int ndigests, digest_head, mismatch_run, mismatches, checks;
    /* the panel's counters */
    double stat_at;
    int snaps_since, snap_bytes;
    unsigned out_at, in_at;
    NetStats stats;
} g;

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
    case NETS_WAITING: return "Waiting for a player to join";
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

static void send_world(void)
{
    uint8_t m[8 + NET_WORLD_INFO];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_WORLD);
    w_world(&b);
    nt_send(1, m, b.len);
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
    g.ndigests = g.digest_head = g.mismatch_run = 0;
    if (g.role != NET_OFF)
        net_log("world %08X", id);
    if (g.role == NET_HOST && g.state == NETS_CONNECTED)
        send_world();
}

void net_start_host(int port)
{
    net_stop();
    g.role = NET_HOST;
    if (nt_host(port ? port : NET_PORT) != 0) {
        g.state = NETS_FAILED;
        snprintf(g.reject, sizeof g.reject, "Cannot open port %d (is another copy hosting?)", port ? port : NET_PORT);
        net_log("%s", g.reject);
        return;
    }
    g.state = NETS_WAITING;
    net_log("hosting on port %d", port ? port : NET_PORT);
}

void net_start_join(const char *addr)
{
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
}

void net_stop(void)
{
    if (g.role != NET_OFF)
        net_log("stopped");
    nt_close();
    uint32_t world = g.world;
    int info_len = g.info_len;
    uint8_t info[NET_WORLD_INFO];
    memcpy(info, g.info, sizeof info);
    void (*handler)(const uint8_t *, int) = g.handler;
    memset(&g, 0, sizeof g);
    g.world = world;
    g.info_len = info_len;
    memcpy(g.info, info, sizeof info);
    g.handler = handler;
}

static void on_hello(NetBuf *b)
{
    uint32_t proto = net_r_u32(b), build = net_r_u32(b);
    (void)net_r_u32(b);                              /* the guest's world: the host's counts */
    uint8_t m[160 + NET_WORLD_INFO];
    NetBuf o = netbuf(m, sizeof m);
    const char *why = b->err ? "Broken hello" : proto != NET_PROTO ? "The other copy speaks another version" :
                      build != build_id() ? "The two copies are different builds of RampageRE" : NULL;
    if (why) {
        net_w_u8(&o, NM_REJECT);
        net_w_bytes(&o, why, (int)strlen(why) + 1);
        nt_send(1, m, o.len);
        nt_flush();
        net_log("refused the guest: %s", why);
        return;
    }
    net_w_u8(&o, NM_WELCOME);
    w_world(&o);
    nt_send(1, m, o.len);
    g.state = NETS_CONNECTED;
    g.have_remote = 0;
    g.remote_frame = 0;
    net_log("the guest joined");
}

static void on_pad(NetBuf *b)
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
    if (b->err || n < 1 || (g.have_remote && frame <= g.remote_frame))
        return;
    for (int i = 0; i < n; i++)                      /* the presses of the frames not seen yet */
        if (!g.have_remote || frame - (uint32_t)i > g.remote_frame)
            g.remote_pressed |= p[i].pressed;
    g.remote = p[0];
    g.remote_frame = frame;
    g.have_remote = 1;
}

static void on_digest(NetBuf *b)
{
    double t = net_r_f64(b);
    uint32_t d = net_r_u32(b);
    if (b->err || !g.ndigests)
        return;
    int best = -1;
    double bd = 1e9;
    for (int i = 0; i < g.ndigests; i++) {           /* the host's own nearest that time */
        double dt = g.digests[i].t - t;
        if (dt < 0) dt = -dt;
        if (dt < bd) { bd = dt; best = i; }
    }
    if (best < 0 || bd > 0.06)
        return;
    g.checks++;
    if (g.digests[best].d != d) {
        if (++g.mismatch_run == 2) {
            g.mismatches++;
            net_log("MISMATCH at host time %.2f: host %08X, guest %08X", t, g.digests[best].d, d);
        }
    } else
        g.mismatch_run = 0;
}

static void on_data(const uint8_t *data, int len)
{
    NetBuf b = netbuf((void *)data, len);
    int type = (int)net_r_u8(&b);
    if (g.role == NET_HOST) {
        if (type == NM_HELLO && g.state == NETS_HANDSHAKE)
            on_hello(&b);
        else if (g.state != NETS_CONNECTED)
            return;
        else if (type == NM_PAD)
            on_pad(&b);
        else if (type == NM_DIGEST)
            on_digest(&b);
        else if (g.handler)
            g.handler(data, len);
        return;
    }
    switch (type) {
    case NM_WELCOME:
        r_world(&b);
        g.state = NETS_CONNECTED;
        net_log("joined the host (its world %08X, ours %08X)", g.host_world, g.world);
        break;
    case NM_REJECT:
        snprintf(g.reject, sizeof g.reject, "The host said no: %.100s", (const char *)data + 1);
        net_log("%s", g.reject);
        g.state = NETS_FAILED;
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
    if (g.role == NET_OFF)
        return;
    NtEvent e;
    while (nt_poll(&e, now)) {
        if (e.type == NT_CONNECT) {
            if (g.role == NET_HOST) {
                g.state = NETS_HANDSHAKE;
                net_log("a guest is connecting");
            } else {
                g.state = NETS_HANDSHAKE;
                uint8_t m[16];
                NetBuf b = netbuf(m, sizeof m);
                net_w_u8(&b, NM_HELLO);
                net_w_u32(&b, NET_PROTO);
                net_w_u32(&b, build_id());
                net_w_u32(&b, g.world);
                nt_send(1, m, b.len);
            }
        } else if (e.type == NT_DISCONNECT) {
            if (g.role == NET_HOST) {
                net_log("the guest left");
                g.state = NETS_WAITING;
                g.have_remote = 0;
            } else {
                net_log(g.state == NETS_CONNECTING ? "nobody answered at %s" : "the host left", g.peer);
                if (!g.reject[0])
                    snprintf(g.reject, sizeof g.reject, g.state == NETS_CONNECTING ? "Nobody answered" : "The host left");
                g.state = NETS_FAILED;
            }
        } else if (e.type == NT_DATA)
            on_data(e.data, e.len);
    }
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
    memmove(g.pads + 1, g.pads, sizeof g.pads - sizeof *g.pads);
    g.pads[0] = *p;
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

int net_remote_pad(NetPad *out)
{
    memset(out, 0, sizeof *out);
    if (g.role != NET_HOST || g.state != NETS_CONNECTED || !g.have_remote)
        return 0;
    *out = g.remote;
    out->pressed = g.remote_pressed;
    g.remote_pressed = 0;
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
    nt_send(0, m, b.len);
    g.snaps_since++;
    g.snap_bytes = b.len;
}

void net_send_msg(int reliable, const void *data, int len)
{
    if (g.role != NET_OFF && g.state == NETS_CONNECTED)
        nt_send(reliable, data, len);
}

void net_keep_digest(double t_host, uint32_t digest)
{
    g.digests[g.digest_head].t = t_host;
    g.digests[g.digest_head].d = digest;
    g.digest_head = (g.digest_head + 1) % DIGESTS;
    if (g.ndigests < DIGESTS)
        g.ndigests++;
}

void net_send_digest(double t_host, uint32_t digest)
{
    if (g.role != NET_GUEST || !net_live())
        return;
    uint8_t m[16];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_DIGEST);
    net_w_f64(&b, t_host);
    net_w_u32(&b, digest);
    nt_send(1, m, b.len);
}

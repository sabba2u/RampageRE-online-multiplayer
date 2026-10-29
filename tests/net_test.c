/* net_test.c - online play's unit tests: the wire format and the snapshot buffer (tests/build_net_test.bat) */
#include "../src/netwire.h"
#include "../src/netsnap.h"
#include "../src/netlobby.h"
#include "../src/netupnp.h"
#include "../src/netstun.h"
#include <stdarg.h>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_wire_round_trip(void)
{
    uint8_t m[64];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, 0xAB); net_w_u16(&b, 0xBEEF); net_w_u32(&b, 0xDEADBEEF); net_w_f32(&b, -1.5f); net_w_f64(&b, 1e-9);
    net_w_bytes(&b, "hi", 3);
    CHECK(!b.err && b.len == 1 + 2 + 4 + 4 + 8 + 3);
    NetBuf r = netbuf(m, b.len);
    CHECK(net_r_u8(&r) == 0xAB);
    CHECK(net_r_u16(&r) == 0xBEEF);
    CHECK(net_r_u32(&r) == 0xDEADBEEF);
    CHECK(net_r_f32(&r) == -1.5f);
    CHECK(net_r_f64(&r) == 1e-9);
    char s[3];
    net_r_bytes(&r, s, 3);
    CHECK(!strcmp(s, "hi") && !r.err);
}

static void test_wire_read_past_end(void)
{
    uint8_t m[2] = { 1, 2 };
    NetBuf r = netbuf(m, 2);
    CHECK(net_r_u32(&r) == 0 && r.err);
    uint8_t w[2];
    NetBuf o = netbuf(w, 2);
    net_w_u32(&o, 5);
    CHECK(o.err && o.len == 0);
}

static void test_snap_interpolates_halfway(void)
{
    static NetSnapBuf b;
    netsnap_clear(&b);
    float x0 = 0, x1 = 10;
    netsnap_push(&b, 0.0, 5.0, &x0, 4);              /* offset 5 */
    netsnap_push(&b, 0.1, 5.1, &x1, 4);
    const NetSnapSample *a, *c;
    float alpha;
    int r = netsnap_sample(&b, 5.15, 0.1, &a, &c, &alpha);   /* host time 0.05 */
    CHECK(r == 2 && a && c && fabsf(alpha - 0.5f) < 1e-4f);
}

static void test_snap_drops_old(void)
{
    static NetSnapBuf b;
    netsnap_clear(&b);
    int x = 0;
    CHECK(netsnap_push(&b, 1.0, 1.0, &x, 4) == 1);
    CHECK(netsnap_push(&b, 0.9, 1.1, &x, 4) == 0);
    CHECK(netsnap_push(&b, 1.0, 1.2, &x, 4) == 0);
    CHECK(b.n == 1);
}

static void test_snap_holds_past_extrapolation(void)
{
    static NetSnapBuf b;
    netsnap_clear(&b);
    int x = 0;
    netsnap_push(&b, 0.0, 0.0, &x, 4);
    netsnap_push(&b, 0.1, 0.1, &x, 4);
    const NetSnapSample *a, *c;
    float alpha;
    int r = netsnap_sample(&b, 0.25, 0.1, &a, &c, &alpha);   /* host 0.15: 0.05 past */
    CHECK(r == 1 && c == netsnap_newest(&b) && fabsf(alpha - 0.05f) < 1e-4f);
    r = netsnap_sample(&b, 5.0, 0.1, &a, &c, &alpha);         /* far past: capped */
    CHECK(r == 1 && fabsf(alpha - (float)NET_SNAP_EXTRAP) < 1e-4f);
}

static void test_snap_clear(void)
{
    static NetSnapBuf b;
    int x = 0;
    netsnap_push(&b, 0.0, 0.0, &x, 4);
    netsnap_clear(&b);
    const NetSnapSample *a, *c;
    float alpha;
    CHECK(netsnap_sample(&b, 1, 0.1, &a, &c, &alpha) == 0 && !netsnap_newest(&b));
}

static void test_snap_wraps(void)
{
    static NetSnapBuf b;
    netsnap_clear(&b);
    for (int i = 0; i < NET_SNAP_N * 2 + 3; i++) {
        float x = (float)i;
        netsnap_push(&b, i * 0.05, i * 0.05, &x, 4);
    }
    const NetSnapSample *a, *c;
    float alpha;
    double tn = (NET_SNAP_N * 2 + 2.5) * 0.05;  /* shows (2N + 0.5) x 0.05: between 2N and 2N + 1 */
    int r = netsnap_sample(&b, tn, 0.1, &a, &c, &alpha);
    float xa, xc;
    memcpy(&xa, a->data, 4);
    memcpy(&xc, c->data, 4);
    CHECK(r == 2 && xc == xa + 1 && xc == (float)(NET_SNAP_N * 2 + 1));
}

static void test_pack_round_trip(void)
{
    static uint8_t src[70000], dst[70000], m[80000];
    srand(7);
    for (int i = 0; i < (int)sizeof src; i++)     /* zero runs of every length, literals, zeros at the end */
        src[i] = (i / 37) % 3 == 0 ? (uint8_t)(rand() % 2 ? 0 : rand()) : (i / 37) % 3 == 1 ? 0 : (uint8_t)(i * 7 + 1);
    for (int i = (int)sizeof src - 100; i < (int)sizeof src; i++)
        src[i] = 0;
    NetBuf b = netbuf(m, sizeof m);
    CHECK(net_pack(&b, src, sizeof src) == 0 && !b.err);
    CHECK(b.len < (int)sizeof src);
    NetBuf r = netbuf(m, b.len);
    memset(dst, 0xCC, sizeof dst);
    CHECK(net_unpack(&r, dst, sizeof dst) == 0 && r.len == b.len);
    CHECK(!memcmp(src, dst, sizeof src));
    static const uint8_t small[][5] = { { 0, 0, 0, 0, 0 }, { 1, 2, 3, 4, 5 }, { 0, 1, 0, 0, 1 }, { 1, 0, 0, 0, 0 },
                                        { 1, 0, 1, 0, 0 } };
    for (int k = 0; k < 5; k++) {
        uint8_t o[5], w[32];
        NetBuf bb = netbuf(w, sizeof w), rr;
        CHECK(net_pack(&bb, small[k], 5) == 0);
        rr = netbuf(w, bb.len);
        CHECK(net_unpack(&rr, o, 5) == 0 && !memcmp(o, small[k], 5));
    }
    uint8_t all0[4096], w[16];                       /* all zeros: a few bytes */
    memset(all0, 0, sizeof all0);
    NetBuf z = netbuf(w, sizeof w);
    CHECK(net_pack(&z, all0, sizeof all0) == 0 && z.len <= 4);
    NetBuf zr = netbuf(w, z.len - 1);                /* cut short: broken */
    CHECK(net_unpack(&zr, all0, sizeof all0) != 0);
    NetBuf tiny = netbuf(w, 2);                      /* no room: err */
    CHECK(net_pack(&tiny, src, 1000) != 0 && tiny.err);
}

static void test_digest(void)
{
    NetDigest a, b;
    memset(&a, 0, sizeof a);
    for (int s = 0; s < NET_DSYS; s++) {
        a.s[s].h = 1000u + (uint32_t)s;
        for (int k = 0; k < NET_DVALS; k++)
            a.s[s].v[k] = (float)(s * 10 + k);
    }
    uint8_t m[1024];
    NetBuf w = netbuf(m, sizeof m);
    net_w_digest(&w, &a);
    NetBuf r = netbuf(m, w.len);
    net_r_digest(&r, &b);
    CHECK(!w.err && !r.err && !memcmp(&a, &b, sizeof a));
    CHECK(net_dsys_same(&a.s[1], &b.s[1], 0.5f));
    b.s[1].v[2] += 0.4f;
    CHECK(net_dsys_same(&a.s[1], &b.s[1], 0.5f));
    b.s[1].v[2] += 0.2f;
    CHECK(!net_dsys_same(&a.s[1], &b.s[1], 0.5f));
    b.s[2].h++;
    CHECK(!net_dsys_same(&a.s[2], &b.s[2], 100));
    CHECK(NET_DSYS_NAME[NDS_ARMY] && !strcmp(NET_DSYS_NAME[NDS_ARMY], "army"));
}

/* the message types: each its own number (a type given another's number silently routes it to the wrong reader) */
static void test_message_types_distinct(void)
{
    int t[] = { NM_HELLO, NM_WELCOME, NM_REJECT, NM_PAD, NM_SNAP, NM_BUILDING, NM_WORLD, NM_DIGEST, NM_EVENT, NM_PUPPET,
                NM_PEVENT, NM_MENU, NM_STATE, NM_CHAT, NM_SLOTS };
    int n = (int)(sizeof t / sizeof *t), same = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            same += t[i] == t[j];
    CHECK(same == 0);
}

/* the player slots (netwire.c): who gets which slot by its token, kept slots, a full game */
static void test_slot_pick(void)
{
    NetSlotPick s[NET_SLOTS] = { { NSL_HOST, 0, 0 }, { NSL_OPEN, 0, 0 }, { NSL_OPEN, 0, 0 }, { NSL_OPEN, 0, 0 } };
    double wait = -1;
    CHECK(net_slot_pick(s, 2, 0, 0, &wait) == 1);                /* a newcomer: the first open one */
    CHECK(net_slot_pick(s, 2, 0x1234, 0, &wait) == 1);           /* a token nobody has: as a newcomer */
    s[1].state = NSL_RESERVED;                                   /* its guest dropped: kept for its token */
    s[1].token = 0xABCDEF0123456789ull;
    s[1].until = 60;
    CHECK(net_slot_pick(s, 2, 0xABCDEF0123456789ull, 13, &wait) == 1);   /* that guest: its own slot back */
    CHECK(net_slot_pick(s, 2, 0, 13, &wait) == NET_PICK_RESERVED && fabs(wait - 47) < 1e-9);   /* another: told */
    CHECK(net_slot_pick(s, 2, 0x55, 13, &wait) == NET_PICK_RESERVED);
    CHECK(net_slot_pick(s, 3, 0, 13, &wait) == 2);                /* (with a third slot open: that one) */
    s[1].state = NSL_OPEN;                                       /* the minute ran out: anyone's */
    CHECK(net_slot_pick(s, 2, 0, 70, &wait) == 1);
    CHECK(net_slot_pick(s, 2, 0xABCDEF0123456789ull, 70, &wait) == 1);
    s[1].state = NSL_ONLINE;
    CHECK(net_slot_pick(s, 2, 0, 70, &wait) == NET_PICK_FULL);   /* every slot played */
    s[1].state = NSL_LOCAL;                                      /* a player on the host's PC took it: full too */
    CHECK(net_slot_pick(s, 2, 0xABCDEF0123456789ull, 70, &wait) == NET_PICK_FULL);
    s[2].state = NSL_ONLINE;
    s[2].token = 7;                                              /* (an online one's token is not taken over) */
    CHECK(net_slot_pick(s, 3, 7, 70, &wait) == NET_PICK_FULL);
}

static void test_slot_messages(void)
{
    uint8_t m[64];
    NetBuf b = netbuf(m, sizeof m);
    NetHello h = { 2, 0xCAFE1234, 0x55AA, 0x0102030405060708ull, 17 }, r;
    net_w_hello(&b, &h);
    NetBuf rb = netbuf(m, b.len);
    net_r_hello(&rb, &r);
    CHECK(!rb.err && r.proto == 2 && r.build == 0xCAFE1234 && r.world == 0x55AA && r.token == 0x0102030405060708ull &&
          r.monster == 17);
    h.monster = -1;
    b = netbuf(m, sizeof m);
    net_w_hello(&b, &h);
    rb = netbuf(m, b.len);
    net_r_hello(&rb, &r);
    CHECK(r.monster == -1);
    rb = netbuf(m, 12);                                          /* a proto 1 hello: shorter, read as broken */
    net_r_hello(&rb, &r);
    CHECK(rb.err);
    NetSlots s = { 2, { NSL_HOST, NSL_RESERVED }, { 0, 47.5f } }, t;
    b = netbuf(m, sizeof m);
    net_w_slots(&b, &s);
    rb = netbuf(m, b.len);
    net_r_slots(&rb, &t);
    CHECK(!rb.err && t.n == 2 && t.state[1] == NSL_RESERVED && t.left[1] == 47.5f);
    m[0] = 9;                                                    /* more slots than there can be */
    rb = netbuf(m, b.len);
    net_r_slots(&rb, &t);
    CHECK(rb.err && t.n == 0);
    char line[64];
    uint64_t tok = 0;
    long long when = 0;
    CHECK(net_rejoin_format(line, sizeof line, 0x00ABCDEF01234567ull, 1793000000LL) == 0);
    CHECK(net_rejoin_parse(line, &tok, &when) == 0 && tok == 0x00ABCDEF01234567ull && when == 1793000000LL);
    CHECK(net_rejoin_parse("rampage-rejoin 1 0000000000000000 5", &tok, &when) < 0);   /* (0 is no token) */
    CHECK(net_rejoin_parse("rampage-rejoin 1 XYZ 5", &tok, &when) < 0);
    CHECK(net_rejoin_parse("", &tok, &when) < 0);
}

/* the lobby's parts without a network (src/netlobby.c, built with -DNETLOBBY_TEST) */
static void test_lobby_url(void)
{
    char host[64], path[64];
    int port, https;
    CHECK(!netlobby_parse_url("http://localhost:19980", host, sizeof host, &port, path, sizeof path, &https));
    CHECK(!strcmp(host, "localhost") && port == 19980 && !path[0] && !https);
    CHECK(!netlobby_parse_url("https://lobby.example.org/rampage/", host, sizeof host, &port, path, sizeof path, &https));
    CHECK(!strcmp(host, "lobby.example.org") && port == 443 && !strcmp(path, "/rampage") && https);
    CHECK(!netlobby_parse_url("192.168.1.5:19980", host, sizeof host, &port, path, sizeof path, &https));
    CHECK(!strcmp(host, "192.168.1.5") && port == 19980 && !https);
    CHECK(netlobby_parse_url("ftp://x", host, sizeof host, &port, path, sizeof path, &https) < 0);
    CHECK(netlobby_parse_url("http://x:99999", host, sizeof host, &port, path, sizeof path, &https) < 0);
    CHECK(netlobby_parse_url("http://a b", host, sizeof host, &port, path, sizeof path, &https) < 0);
    CHECK(netlobby_parse_url("http://", host, sizeof host, &port, path, sizeof path, &https) < 0);
}

static void test_lobby_join_kind(void)
{
    char out[64];
    CHECK(netlobby_join_kind("k7pq2", out, sizeof out) == NLJ_CODE && !strcmp(out, "K7PQ2"));
    CHECK(netlobby_join_kind("rampage://join/K7PQ2/", out, sizeof out) == NLJ_CODE && !strcmp(out, "K7PQ2"));
    CHECK(netlobby_join_kind("rampage://join/203.0.113.5:27960", out, sizeof out) == NLJ_ADDR &&
          !strcmp(out, "203.0.113.5:27960"));
    CHECK(netlobby_join_kind("127.0.0.1", out, sizeof out) == NLJ_ADDR && !strcmp(out, "127.0.0.1"));
    CHECK(netlobby_join_kind("K0PQ2", out, sizeof out) == NLJ_ADDR);     /* 0 is no code letter: a host name */
    CHECK(netlobby_join_kind("rampage://host/K7PQ2", out, sizeof out) == NLJ_NONE && !out[0]);
    CHECK(netlobby_join_kind("rampage://join/a;b", out, sizeof out) == NLJ_NONE);
    CHECK(netlobby_join_kind("1.2.3.4:", out, sizeof out) == NLJ_NONE);
    CHECK(netlobby_join_kind("1.2.3.4:70000", out, sizeof out) == NLJ_NONE);
    CHECK(netlobby_join_kind("", out, sizeof out) == NLJ_NONE);
    CHECK(netlobby_is_code("ABCDE") && !netlobby_is_code("ABCD") && !netlobby_is_code("ABCDEF") &&
          !netlobby_is_code("ABCD1"));
}

/* netupnp.c's stand-ins for net.c and netlobby.c's network half (not in this test) */
static char upnp_logged[512];
void net_log(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vsnprintf(upnp_logged, sizeof upnp_logged, fmt, a);
    va_end(a);
}
static char upnp_external[64] = "unset";
void netlobby_set_external(const char *addr) { snprintf(upnp_external, sizeof upnp_external, "%s", addr); }

/* the port forward with no router to ask: RAMPAGE_NET_UPNP=0 leaves it alone at once; a search from 127.0.0.1
 * (RAMPAGE_NET_UPNP_IF), where no router can answer, ends in NUP_FAILED within its 2 s, said in the log, and the
 * lobby is never told an address */
static void test_upnp_no_router(void)
{
    const char *t = NULL;
    _putenv("RAMPAGE_NET_UPNP=0");
    net_upnp_open(27999);
    CHECK(net_upnp_state(&t) == NUP_OFF && t && strstr(t, "off"));
    _putenv("RAMPAGE_NET_UPNP=");
    _putenv("RAMPAGE_NET_UPNP_IF=127.0.0.1");
    net_upnp_open(27999);
    CHECK(net_upnp_state(NULL) == NUP_WORKING);
    int s = NUP_WORKING;
    for (int i = 0; i < 100 && s == NUP_WORKING; i++) {
        Sleep(100);
        net_upnp_poll();
        s = net_upnp_state(&t);
    }
    CHECK(s == NUP_FAILED);
    CHECK(strstr(upnp_logged, "UPnP:") != NULL);
    CHECK(!strcmp(upnp_external, "unset"));
    net_upnp_close();
    CHECK(net_upnp_state(NULL) == NUP_OFF);
    _putenv("RAMPAGE_NET_UPNP_IF=");
}

static void test_lobby_games_txt(void)
{
    NetLobbyGame g[4];
    const char *txt = "rampage-lobby 1\r\n"
                      "K7PQ2\t127.0.0.1:27980\t1\t2\tCAFE1234\tsabba\tGeorge\tChicago 1\r\n"
                      "\n"
                      "short\tline\n"
                      "M4TR9\t-\t2\t2\t0BADF00D\tsomeone with a very long name indeed\t\t\n";
    int n = netlobby_parse_games(txt, g, 4);
    CHECK(n == 2);
    CHECK(!strcmp(g[0].code, "K7PQ2") && !strcmp(g[0].join, "127.0.0.1:27980") && g[0].players == 1 && g[0].max == 2);
    CHECK(!strcmp(g[0].build, "CAFE1234") && !strcmp(g[0].name, "sabba") && !strcmp(g[0].monster, "George") &&
          !strcmp(g[0].city, "Chicago 1"));
    CHECK(!strcmp(g[1].join, "-") && !g[1].monster[0] && !g[1].city[0] && strlen(g[1].name) == sizeof g[1].name - 1);
    CHECK(g[0].reserved == 0);
    CHECK(netlobby_parse_games("rampage-lobby 1\nK7PQ2\t1.2.3.4:27960\t1\t2\tCAFE1234\ts\tGeorge\tChicago 1\t1\n",
                               g, 4) == 1 &&
          g[0].players == 1 && g[0].max == 2 && g[0].reserved == 1 && !strcmp(g[0].city, "Chicago 1"));
    CHECK(netlobby_parse_games(txt, g, 1) == 1);
    CHECK(netlobby_parse_games("rampage-lobby 1\n", g, 4) == 0);
    CHECK(netlobby_parse_games("rampage-lobby 1", g, 4) == 0);
    CHECK(netlobby_parse_games("<html>", g, 4) == -1);
    CHECK(netlobby_parse_games("rampage-lobby 12\n", g, 4) == -1);
    char code[8], token[80];
    int hb;
    CHECK(!netlobby_parse_registered("ok K7PQ2 0123abcd 10\n", code, sizeof code, token, sizeof token, &hb) &&
          !strcmp(code, "K7PQ2") && !strcmp(token, "0123abcd") && hb == 10);
    CHECK(netlobby_parse_registered("error too many games\n", code, sizeof code, token, sizeof token, &hb) < 0);
}

/* STUN (netstun.c): a request's bytes, an answer's XOR-MAPPED-ADDRESS (203.0.113.7:54321, after a plain
 * MAPPED-ADDRESS a router rewrote), another request's answer refused, the punch, the lobby's knocks */
static void test_stun(void)
{
    uint8_t tx[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }, req[20];
    CHECK(netstun_request(req, tx) == 20 && req[0] == 0 && req[1] == 1 && req[4] == 0x21 && req[7] == 0x42 &&
          !memcmp(req + 8, tx, 12));
    CHECK(netstun_is_stun(req, 20) && !netstun_is_stun(req, 19));
    uint8_t r[44] = { 0x01, 0x01, 0, 24, 0x21, 0x12, 0xA4, 0x42 };
    memcpy(r + 8, tx, 12);
    uint8_t mapped[12] = { 0, 0x01, 0, 8, 0, 0x01, 0x12, 0x34, 192, 168, 1, 2 };   /* (rewritten: 192.168.1.2:4660) */
    memcpy(r + 20, mapped, 12);
    uint32_t ip = 203u << 24 | 0u << 16 | 113u << 8 | 7u;
    uint16_t port = 54321;
    uint8_t xor_[12] = { 0, 0x20, 0, 8, 0, 0x01, (uint8_t)((port ^ 0x2112) >> 8), (uint8_t)(port ^ 0x2112),
                         (uint8_t)((ip ^ 0x2112A442u) >> 24), (uint8_t)((ip ^ 0x2112A442u) >> 16),
                         (uint8_t)((ip ^ 0x2112A442u) >> 8), (uint8_t)(ip ^ 0x2112A442u) };
    memcpy(r + 32, xor_, 12);
    uint32_t gi = 0;
    uint16_t gp = 0;
    CHECK(netstun_parse(r, 44, tx, &gi, &gp) == 0 && gi == ip && gp == port);
    uint8_t other[12] = { 9 };
    CHECK(netstun_parse(r, 44, other, &gi, &gp) < 0);
    CHECK(netstun_parse(r, 40, tx, &gi, &gp) < 0);   /* (cut short) */
    CHECK(netstun_parse(req, 20, tx, &gi, &gp) < 0); /* (a request, no answer) */
    CHECK(netstun_is_punch((const uint8_t *)NETSTUN_PUNCH, 8) && !netstun_is_punch((const uint8_t *)"RAMPUNCHX", 9));
    char k[8][24];
    CHECK(netstun_parse_knocks("ok\nknock 203.0.113.7:54321\nknock 999.1.1.1:5\nknock 10.0.0.2:27960\n", k, 8) == 2 &&
          !strcmp(k[0], "203.0.113.7:54321") && !strcmp(k[1], "10.0.0.2:27960"));
    CHECK(netstun_parse_knocks("", k, 8) == 0);
}

int main(void)
{
    test_message_types_distinct();
    test_slot_pick();
    test_slot_messages();
    test_wire_round_trip();
    test_wire_read_past_end();
    test_snap_interpolates_halfway();
    test_snap_drops_old();
    test_snap_holds_past_extrapolation();
    test_snap_clear();
    test_snap_wraps();
    test_pack_round_trip();
    test_digest();
    test_lobby_url();
    test_lobby_join_kind();
    test_lobby_games_txt();
    test_upnp_no_router();
    test_stun();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}

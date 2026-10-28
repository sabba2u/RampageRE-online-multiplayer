/* net_test.c - online play's unit tests: the wire format and the snapshot buffer (tests/build_net_test.bat) */
#include "../src/netwire.h"
#include "../src/netsnap.h"
#include <math.h>
#include <stdio.h>
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

int main(void)
{
    test_wire_round_trip();
    test_wire_read_past_end();
    test_snap_interpolates_halfway();
    test_snap_drops_old();
    test_snap_holds_past_extrapolation();
    test_snap_clear();
    test_snap_wraps();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}

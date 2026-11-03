/* rumble_test.c - Boss Rumble's rules (src/rumble.c): the waves, the factors, the picks, the power-ups' timing, the
 * best-time file, the time's text (tests/build_rumble_test.bat) */
#include "../src/rumble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void test_waves(void)
{
    static const int want[RUMBLE_WAVES] = { 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4 };
    for (int w = 1; w <= RUMBLE_WAVES; w++)
        CHECK(rumble_wave_size(w) == want[w - 1]);
    CHECK(rumble_wave_size(0) == 0 && rumble_wave_size(13) == 0 && rumble_wave_size(-1) == 0);
}

static int near(float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; }

static void test_factor(void)
{
    CHECK(rumble_factor(1) == 1.0f && rumble_factor(3) == 1.0f);
    CHECK(rumble_factor(4) == 1.5f && rumble_factor(6) == 1.5f);
    CHECK(rumble_factor(7) == 2.0f && rumble_factor(9) == 2.0f);
    CHECK(rumble_factor(10) == 2.75f && rumble_factor(12) == 2.75f);
    CHECK(rumble_factor(0) == 1.0f && rumble_factor(13) == 1.0f);
}

static void test_hp_scale(void)                      /* a boss's health, half again for each player after the first */
{
    CHECK(near(rumble_hp_scale(1), 1.0f) && near(rumble_hp_scale(2), 1.5f));
    CHECK(near(rumble_hp_scale(4), 2.5f) && near(rumble_hp_scale(8), 4.5f));
    CHECK(near(rumble_hp_scale(0), 1.0f) && near(rumble_hp_scale(-3), 1.0f));
}

static void test_rest(void)                          /* the breath after an attack, shorter by group of three */
{
    CHECK(near(rumble_rest(1), 1.4f) && near(rumble_rest(3), 1.4f));
    CHECK(near(rumble_rest(4), 1.2f) && near(rumble_rest(7), 1.0f));
    CHECK(near(rumble_rest(10), 0.8f) && near(rumble_rest(12), 0.8f));
    CHECK(near(rumble_rest(0), 1.4f) && near(rumble_rest(13), 1.4f));
}

static void test_pick(void)
{
    for (unsigned seed = 1; seed < 200; seed++) {
        unsigned s = seed;
        int o[4];
        CHECK(rumble_pick(&s, 10, 4, o) == 4);
        for (int i = 0; i < 4; i++) {
            CHECK(o[i] >= 0 && o[i] < 10);
            for (int j = 0; j < i; j++)
                CHECK(o[i] != o[j]);
        }
    }
    unsigned a = 5, b = 5;
    int p[4], q[4];
    rumble_pick(&a, 10, 3, p);
    rumble_pick(&b, 10, 3, q);
    CHECK(!memcmp(p, q, 3 * sizeof(int)) && a == b && a != 5);
    unsigned t = 1;
    int r[4];
    CHECK(rumble_pick(&t, 2, 4, r) == 4);          /* a small roster: repeats, no hang */
    for (int i = 0; i < 4; i++)
        CHECK(r[i] >= 0 && r[i] < 2);
    CHECK(rumble_pick(&t, 0, 2, r) == 0);
}

static void test_pickup_time(void)
{
    unsigned s = 3;
    float lo = 100, hi = 0;
    for (int i = 0; i < 1000; i++) {
        float f = rumble_next_pickup(&s);
        CHECK(f >= 15 && f <= 25);
        if (f < lo) lo = f;
        if (f > hi) hi = f;
    }
    CHECK(lo < 16 && hi > 24);                       /* (spread over the range) */
}

static void test_best(void)
{
    const char *path = "tests/rumble_best_test.txt";
    remove(path);
    float b = -1;
    CHECK(!rumble_best_read(path, "AA", &b) && b == 0);
    CHECK(rumble_best_write(path, "AA", 90));
    CHECK(rumble_best_write(path, "AA", 60));        /* the best kept */
    CHECK(rumble_best_write(path, "BB", 30));
    CHECK(rumble_best_write(path, "My Name", 45.5f));
    CHECK(rumble_best_read(path, "AA", &b) && b == 90);
    CHECK(rumble_best_read(path, "BB", &b) && b == 30);
    CHECK(rumble_best_read(path, "My Name", &b) && b == 45.5f);
    CHECK(!rumble_best_read(path, "CC", &b) && b == 0);
    CHECK(rumble_best_write(path, "BB", 120));
    CHECK(rumble_best_read(path, "BB", &b) && b == 120);
    CHECK(rumble_best_read(path, "AA", &b) && b == 90);
    remove(path);
}

static void test_time_text(void)
{
    char t[16];
    rumble_time_text(75.4f, t); CHECK(!strcmp(t, "1:15"));
    rumble_time_text(0, t); CHECK(!strcmp(t, "0:00"));
    rumble_time_text(59.99f, t); CHECK(!strcmp(t, "0:59"));
    rumble_time_text(3725, t); CHECK(!strcmp(t, "1:02:05"));
    rumble_time_text(-3, t); CHECK(!strcmp(t, "0:00"));
}

int main(void)
{
    test_waves();
    test_factor();
    test_hp_scale();
    test_rest();
    test_pick();
    test_pickup_time();
    test_best();
    test_time_text();
    printf("rumble_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}

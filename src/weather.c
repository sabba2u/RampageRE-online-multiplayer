/* weather.c - port extra: rain, lightning and thunder (weather.h) */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include <GL/gl.h>
#include "weather.h"
#include "sound.h"

#define DROPS 700                                    /* streaks on screen at full rain */
#define FADE_S 2.0f                                  /* the rain comes and goes over this */
#define BOLT_PTS 48

static struct {
    int kind, local, visible;
    float amount;                                    /* 0 .. 1: how much rain there is now (fading to the kind's) */
    float t;                                         /* the weather's own clock (play time) */
    float next_strike;                               /* when this copy picks its next strike (-1: not yet drawn) */
    unsigned rng;
    float drop[DROPS][4];                            /* each streak: x, y (0..1 of the screen), speed, length */
    int seeded;
    /* the strike showing: its age, the bolt's points (0..1 of the screen), how many */
    float strike_age;
    float bolt[BOLT_PTS][2];
    int nbolt;
} W = { 0, 1, 0, 0, 0, -1, 12345u, { { 0 } }, 0, 99, { { 0 } }, 0 };

/* the thunder, played by the mixer's thread: set here, read there (a new one starts over the old) */
static volatile int thunder_go;                      /* frames left before it starts (the light first) */
static volatile int thunder_len;                     /* frames of it left (0 none) */
static volatile float thunder_gain;

static float frand(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return (float)((*s >> 8) & 0xFFFFFF) / 16777216.0f;
}

int weather_of(const char *name)
{
    if (!name)
        return -1;
    if (!strcmp(name, "clear") || !strcmp(name, "0"))
        return WEATHER_CLEAR;
    if (!strcmp(name, "rain") || !strcmp(name, "1"))
        return WEATHER_RAIN;
    if (!strcmp(name, "storm") || !strcmp(name, "2"))
        return WEATHER_STORM;
    return -1;
}

const char *weather_name(int kind)
{
    return kind == WEATHER_RAIN ? "rain" : kind == WEATHER_STORM ? "storm" : "clear";
}

void weather_set(int kind) { W.kind = kind < 0 || kind > 2 ? 0 : kind; }
int weather_get(void) { return W.kind; }
void weather_local(int on) { W.local = on != 0; }
void weather_visible(int on) { W.visible = on != 0; }

static void seed_drops(void)
{
    for (int i = 0; i < DROPS; i++) {
        W.drop[i][0] = frand(&W.rng) * 1.2f - 0.1f;
        W.drop[i][1] = frand(&W.rng);
        W.drop[i][2] = 1.6f + frand(&W.rng) * 1.2f;  /* screens a second */
        W.drop[i][3] = 0.03f + frand(&W.rng) * 0.04f;
    }
    W.seeded = 1;
}

int weather_update(float dt, float *x, unsigned *seed)
{
    if (!W.seeded)
        seed_drops();
    float want = W.kind == WEATHER_CLEAR ? 0.0f : 1.0f;
    if (W.amount < want)
        W.amount = fminf(want, W.amount + dt / FADE_S);
    else if (W.amount > want)
        W.amount = fmaxf(want, W.amount - dt / FADE_S);
    W.t += dt;
    W.strike_age += dt;
    for (int i = 0; i < DROPS; i++) {                /* down, and a little to the left (the wind) */
        W.drop[i][1] += W.drop[i][2] * dt;
        W.drop[i][0] -= W.drop[i][2] * dt * 0.18f;
        if (W.drop[i][1] > 1.05f || W.drop[i][0] < -0.1f) {
            W.drop[i][1] -= 1.1f;
            W.drop[i][0] = frand(&W.rng) * 1.2f;
        }
    }
    if (W.kind != WEATHER_STORM || W.amount < 0.8f || !W.local || dt <= 0) {
        if (W.kind != WEATHER_STORM)
            W.next_strike = -1;
        return 0;
    }
    if (W.next_strike < 0)
        W.next_strike = W.t + 4 + frand(&W.rng) * 10;   /* every 4 to 18 s */
    if (W.t < W.next_strike)
        return 0;
    W.next_strike = W.t + 6 + frand(&W.rng) * 12;
    *x = 0.12f + frand(&W.rng) * 0.76f;
    *seed = (unsigned)(frand(&W.rng) * 4294967040.0f) | 1u;
    weather_strike(*x, *seed);
    return 1;
}

void weather_strike(float x, unsigned seed)
{
    unsigned s = seed ? seed : 1;
    float bottom = 0.55f + frand(&s) * 0.25f;        /* where it lands: down in the city */
    float px = x + (frand(&s) - 0.5f) * 0.12f, py = -0.02f;
    W.nbolt = 0;
    while (py < bottom && W.nbolt < BOLT_PTS - 1) {  /* a jagged path down, drawn to where it lands */
        W.bolt[W.nbolt][0] = px;
        W.bolt[W.nbolt][1] = py;
        W.nbolt++;
        py += 0.02f + frand(&s) * 0.04f;
        px = px + (x - px) * 0.25f + (frand(&s) - 0.5f) * 0.05f;
    }
    W.bolt[W.nbolt][0] = x;
    W.bolt[W.nbolt][1] = bottom;
    W.nbolt++;
    W.strike_age = 0;
    /* thunder: 0.4 to 1.6 s later (how far it was), 4 s of it */
    thunder_gain = 0.55f + frand(&s) * 0.35f;
    thunder_len = 4 * SOUND_RATE;
    thunder_go = (int)((0.4f + frand(&s) * 1.2f) * SOUND_RATE);
}

/* the flash's brightness at age a (seconds): on, off, on (Emberville's flicker), then a fading glow */
static float flash_at(float a)
{
    if (a < 0.07f)
        return 0.75f;
    if (a < 0.11f)
        return 0.1f;
    if (a < 0.24f)
        return 0.6f;
    if (a < 0.9f)
        return 0.25f * (0.9f - a) / 0.66f;
    return 0;
}

void weather_draw(int width, int height)
{
    float flash = W.nbolt ? flash_at(W.strike_age) : 0;
    if (!W.visible || (W.amount <= 0.001f && flash <= 0))
        return;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float a = W.amount;
    if (a > 0) {                                     /* the storm's gloom */
        glColor4f(0.02f, 0.03f, 0.06f, 0.28f * a);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(1, 0); glVertex2f(1, 1); glVertex2f(0, 1);
        glEnd();
        float px = height > 0 ? 1.0f / height : 0.001f;
        glLineWidth(height > 900 ? 2.0f : 1.0f);
        glBegin(GL_LINES);
        int n = (int)(DROPS * a);
        for (int i = 0; i < n; i++) {
            const float *d = W.drop[i];
            float len = d[3];
            glColor4f(0.75f, 0.8f, 0.9f, 0.0f);
            glVertex2f(d[0] + len * 0.18f, d[1] - len);
            glColor4f(0.8f, 0.85f, 0.95f, 0.38f);
            glVertex2f(d[0], d[1] + px);
        }
        glEnd();
    }
    if (W.nbolt && W.strike_age < 0.3f) {            /* the bolt: lit while the flash is on */
        int lit = W.strike_age < 0.07f || (W.strike_age > 0.11f && W.strike_age < 0.24f);
        if (lit) {
            glLineWidth(height > 900 ? 9.0f : 6.0f);
            glColor4f(0.55f, 0.7f, 1.0f, 0.45f);
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < W.nbolt; i++)
                glVertex2f(W.bolt[i][0], W.bolt[i][1]);
            glEnd();
            glLineWidth(height > 900 ? 3.0f : 2.0f);
            glColor4f(1.0f, 1.0f, 0.95f, 1.0f);
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < W.nbolt; i++)
                glVertex2f(W.bolt[i][0], W.bolt[i][1]);
            glEnd();
        }
    }
    if (flash > 0) {                                 /* the whole city lit */
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glColor4f(0.8f, 0.85f, 1.0f, flash);
        glBegin(GL_QUADS);
        glVertex2f(0, 0); glVertex2f(1, 0); glVertex2f(1, 1); glVertex2f(0, 1);
        glEnd();
    }
    glPopAttrib();
    glLineWidth(1);
    glColor4f(1, 1, 1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    (void)width;
}

/* the thunder: noise through two low-pass filters (a rumble), a sharp crack at its start, a long roll with swells */
void weather_mix(float *out, int frames)
{
    static float lp1, lp2, lp3, t;
    static unsigned s = 777u;
    for (int f = 0; f < frames; f++) {
        if (thunder_go > 0) {
            thunder_go--;
            if (thunder_go == 0)
                t = 0;
            continue;
        }
        if (thunder_len <= 0)
            return;
        thunder_len--;
        t += 1.0f / SOUND_RATE;
        float n = frand(&s) * 2 - 1;
        lp1 += (n - lp1) * 0.06f;                    /* the roll: deep */
        lp2 += (lp1 - lp2) * 0.06f;
        lp3 += (n - lp3) * 0.35f;                    /* the crack: brighter, short */
        float swell = 0.6f + 0.4f * sinf(t * 5.3f) * sinf(t * 1.7f + 1);
        float env = (t < 0.05f ? t / 0.05f : expf(-(t - 0.05f) * 0.9f)) * swell;
        float crack = t < 0.4f ? expf(-t * 9) * lp3 * 0.6f : 0;
        float v = (lp2 * 9.0f * env + crack) * thunder_gain * 9000.0f;
        out[2 * f] += v;
        out[2 * f + 1] += v * 0.92f;
    }
}

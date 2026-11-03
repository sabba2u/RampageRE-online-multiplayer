/* rumble.c - Boss Rumble's rules (rumble.h) */
#include "rumble.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rumble_wave_size(int wave) { return wave >= 1 && wave <= RUMBLE_WAVES ? (wave - 1) / 3 + 1 : 0; }

float rumble_factor(int wave)
{
    static const float f[4] = { 1.0f, 1.5f, 2.0f, 2.75f };
    return wave >= 1 && wave <= RUMBLE_WAVES ? f[(wave - 1) / 3] : 1.0f;
}

float rumble_hp_scale(int players) { return players > 1 ? 1.0f + 0.5f * (float)(players - 1) : 1.0f; }

float rumble_rest(int wave) { return wave >= 1 && wave <= RUMBLE_WAVES ? RUMBLE_REST - 0.2f * (float)((wave - 1) / 3) : RUMBLE_REST; }

unsigned rumble_rand(unsigned *seed)
{
    *seed = *seed * 69069u + 1u;
    return *seed >> 8;                               /* (the low bits of an LCG cycle short) */
}

int rumble_pick(unsigned *seed, int nroster, int count, int out[RUMBLE_MAX_BOSSES])
{
    if (nroster <= 0 || count <= 0)
        return 0;
    if (count > RUMBLE_MAX_BOSSES)
        count = RUMBLE_MAX_BOSSES;
    for (int i = 0; i < count; i++) {
        int c = (int)(rumble_rand(seed) % (unsigned)nroster);
        if (nroster >= count)                        /* the next one not taken yet */
            for (int again = 1; again;) {
                again = 0;
                for (int j = 0; j < i; j++)
                    if (out[j] == c) {
                        c = (c + 1) % nroster;
                        again = 1;
                    }
            }
        out[i] = c;
    }
    return count;
}

float rumble_next_pickup(unsigned *seed) { return 15.0f + 10.0f * (float)(rumble_rand(seed) % 10001u) / 10000.0f; }

/* a line "<seconds> <name>": the name's start, NULL when it is not one */
static const char *line_name(const char *line, float *t)
{
    char *end;
    *t = strtof(line, &end);
    if (end == line || *end != ' ')
        return NULL;
    return end + 1;
}

static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = 0;
}

int rumble_best_read(const char *path, const char *profile, float *best)
{
    *best = 0;
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f)
        return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        chomp(line);
        float t;
        const char *n = line_name(line, &t);
        if (n && !strcmp(n, profile ? profile : "")) {
            *best = t;
            found = 1;
        }
    }
    fclose(f);
    return found;
}

int rumble_best_write(const char *path, const char *profile, float t)
{
    if (!path)
        return 0;
    enum { MAX_LINES = 256 };
    static char lines[MAX_LINES][256];
    int n = 0, mine = -1;
    FILE *f = fopen(path, "r");
    if (f) {
        while (n < MAX_LINES && fgets(lines[n], sizeof lines[n], f)) {
            chomp(lines[n]);
            float old;
            const char *nm = line_name(lines[n], &old);
            if (!nm)
                continue;
            if (!strcmp(nm, profile ? profile : "")) {
                if (old >= t)
                    t = old;
                mine = n;
            }
            n++;
        }
        fclose(f);
    }
    if (mine < 0 && n < MAX_LINES)
        mine = n++;
    if (mine < 0)
        return 0;
    snprintf(lines[mine], sizeof lines[mine], "%.2f %s", t, profile ? profile : "");
    f = fopen(path, "w");
    if (!f)
        return 0;
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\n", lines[i]);
    return fclose(f) == 0;
}

void rumble_time_text(float t, char out[16])
{
    int s = t > 0 ? (int)t : 0;
    if (s >= 3600)
        snprintf(out, 16, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(out, 16, "%d:%02d", s / 60, s % 60);
}

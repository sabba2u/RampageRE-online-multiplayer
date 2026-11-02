/* rumble.h - port extra: Boss Rumble's rules, nothing of the world (docs/superpowers/specs/2026-11-02-boss-rumble-
 * design.md): 12 waves of bosses (1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4), each group of three tougher by a quarter, the
 * bosses picked at random from the roster, a power-up out in the city every 15 .. 25 s, the best survival time kept
 * per profile in a small text file. main.c runs the mode with them (rumble_driver_step); tests/rumble_test.c. */
#ifndef RAMPAGE_RUMBLE_H
#define RAMPAGE_RUMBLE_H

#define RUMBLE_WAVES 12
#define RUMBLE_MAX_BOSSES 4
#define RUMBLE_PICKUP_LIFE 20.0f                     /* a power-up nobody takes is gone after this */
#define RUMBLE_WAVE_PAUSE 3.0f                       /* between a wave cleared and the next, its banner up */

int rumble_wave_size(int wave);                      /* wave 1 .. 12: the bosses at once; else 0 */
float rumble_factor(int wave);                       /* their health and hits: 1, 1.25, 1.5, 1.75 by group of three */
/* count roster indices 0 .. nroster-1, all different when the roster has that many; *seed advances (the same seed, the
 * same picks): the count, 0 with no roster */
int rumble_pick(unsigned *seed, int nroster, int count, int out[RUMBLE_MAX_BOSSES]);
unsigned rumble_rand(unsigned *seed);                /* the picks' LCG step: its top bits */
float rumble_next_pickup(unsigned *seed);            /* the seconds to the next power-up, 15 .. 25 */
/* the best time of a profile: lines "<seconds> <profile name>"; read: 1 and *best, else 0 and *best 0; write keeps
 * the larger, the other profiles' lines as they were: 1 written */
int rumble_best_read(const char *path, const char *profile, float *best);
int rumble_best_write(const char *path, const char *profile, float t);
void rumble_time_text(float t, char out[16]);        /* "M:SS", from an hour "H:MM:SS" */

#endif

/* rumble.h - port extra: Boss Rumble's rules, nothing of the world (docs/superpowers/specs/2026-11-02-boss-rumble-
 * design.md): 12 waves of bosses (1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4), each group of three tougher (x1, 1.5, 2, 2.75; more health with more players), the
 * bosses picked at random from the roster, a power-up out in the city every 15 .. 25 s, the best survival time kept
 * per profile in a small text file. main.c runs the mode with them (rumble_driver_step); tests/rumble_test.c. */
#ifndef RAMPAGE_RUMBLE_H
#define RAMPAGE_RUMBLE_H

#define RUMBLE_WAVES 12
#define RUMBLE_MAX_BOSSES 4
#define RUMBLE_PICKUP_LIFE 20.0f                     /* a power-up nobody takes is gone after this */
#define RUMBLE_WAVE_PAUSE 3.0f                       /* between a wave cleared and the next, its banner up */
/* the bosses' manners (the owner, 2026-11-02: "the ai never lets up ... they just juggle us"): */
#define RUMBLE_REST 1.4f                             /* a boss's breath after each attack, x 0.7 .. 1.3 */
#define RUMBLE_SPARE_TIME 2.0f                       /* a player knocked down, or hit RUMBLE_SPARE_HITS times with no
                                                      * more than RUMBLE_SPARE_RUN s between, is the bosses' to leave be
                                                      * this long: their blows pass, they go for another or wait */
#define RUMBLE_SPARE_HITS 3
#define RUMBLE_SPARE_RUN 2.5f
#define RUMBLE_REACH 25.0f                           /* a boss this near a living player is in reach */
#define RUMBLE_STUCK 30.0f                           /* out of every player's reach this long: back beside them */

int rumble_wave_size(int wave);                      /* wave 1 .. 12: the bosses at once; else 0 */
float rumble_factor(int wave);                       /* their health and hits: 1, 1.5, 2, 2.75 by group of three */
/* a boss's health x this more with players in the run (not its hits): half again for each after the first, so a
 * crowd faces sturdier bosses (the owner, 2026-11-03: "make sure it gets hard enough") */
float rumble_hp_scale(int players);
/* a boss's breath after each attack in wave w: RUMBLE_REST (1.4 s), 0.2 s less by group of three, 0.8 s at the end */
float rumble_rest(int wave);
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

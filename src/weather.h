/* weather.h - port extra (docs/TODO.md): rain, lightning and thunder over a city. Off (the default) nothing in the game
 * is touched, so the offline game and its regression shots are as before. In a storm a strike now and then lands on a
 * monster (main.c lightning_zap: the zap reaction, 15 HP, an x-ray flicker); the rest is just to look at and hear.
 *   - rain: slanted streaks falling over the world (drawn after the world's particles, before the HUD), the scene a
 *     little darker under it;
 *   - lightning (a storm): now and then a jagged bolt from the top of the screen down into the city, the whole screen
 *     flashing on, off, on (Emberville's BetterWeather, Lightning.cs, the same flicker), then thunder a moment later
 *     (a synthesized rumble: filtered noise, a crack, a long roll);
 *   - online the host's weather is everyone's, and the host picks each strike (netgame.c sends it), so every screen
 *     flashes together. */
#ifndef RAMPAGE_WEATHER_H
#define RAMPAGE_WEATHER_H

enum { WEATHER_CLEAR = 0, WEATHER_RAIN = 1, WEATHER_STORM = 2 };

int weather_of(const char *name);                    /* "clear" / "rain" / "storm" (or 0 / 1 / 2); else -1 */
const char *weather_name(int kind);
void weather_set(int kind);                          /* the weather from now on (it fades in and out over 2 s) */
int weather_get(void);
/* local: this copy picks its own strikes (offline, the host); 0: they come by weather_strike (a guest) */
void weather_local(int on);
void weather_visible(int on);                        /* in a city's play (1), else nothing is drawn (the menus) */
/* once a frame in play (dt: the frame's seconds; 0 while paused): the drops fall, a strike may come (returns 1 when
 * this copy picked one now: *x the strike's place across the screen 0..1, *seed its bolt's shape) */
int weather_update(float dt, float *x, unsigned *seed);
void weather_strike(float x, unsigned seed);         /* a strike, as picked (here or by the host) */
/* one that lands at (x, y), 0..1 of the screen from the top left (on a monster: main.c lightning_zap); y < 0: into
 * the city as weather_strike */
void weather_strike_to(float x, float y, unsigned seed);
/* render_gl.c: the rain, the bolt and the flash over the world (GL state: the HUD's 2D, set and put back here) */
void weather_draw(int width, int height);
/* render_gl.c, in the world's pass (its matrices, its depth): the drops landing on the ground about the camera at
 * cam (x, y, z), hidden by what stands in front; height: the window's (the lines' width) */
void weather_draw_ground(const float cam[3], int height);
/* the mixer (sound.c, its own thread): the thunder added into out (stereo floats at SOUND_RATE) */
void weather_mix(float *out, int frames);

#endif

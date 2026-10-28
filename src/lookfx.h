/* lookfx.h - the look of the 3D picture (port extra): depth of field (the distance blurred, the monsters sharp),
 * tilt-shift (the top and bottom of the picture blurred and the colours livelier: a model city) and the camera pulled
 * back and tilted down about the action. Set in the Insert panel ("Camera and look", netui.cpp), kept in lookfx.cfg
 * beside the program. All of it off by default: the picture is then the game's. */
#ifndef RAMPAGE_LOOKFX_H
#define RAMPAGE_LOOKFX_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float dof;                                       /* depth of field: 0 off .. 1 strong */
    float dof_range;                                 /* how far from the focus it stays sharp: 0 near .. 1 far */
    float tilt;                                      /* tilt-shift: 0 off .. 1 strong */
    float tilt_center, tilt_band;                    /* the sharp band: its middle (0 top .. 1 bottom), its height */
    float tilt_color;                                /* the miniature's livelier colours: 0 .. 1 */
    float zoom;                                      /* the camera pulled back: 0 the game's .. 1 far */
    float pitch;                                     /* the camera tilted down: degrees, 0 .. 35 */
} LookFx;

LookFx *lookfx(void);                                /* the settings, changed in place by the panel */
void lookfx_load(void);                              /* lookfx.cfg, at the start */
void lookfx_save(void);
/* render_gl.c, after the 3D picture and before the HUD: the blurs over the frame bound now (its colour and depth);
 * znear / zfar the projection's. Nothing while both are off or lookfx_active(0) */
void lookfx_frame(int w, int h, float znear, float zfar);
void lookfx_active(int on);                          /* main.c: in play (not the menus' 3D) */
/* main.c: the camera moved as the settings say, after the game's own: pos[3], rows[3][3] (right, forward, up) */
void lookfx_camera(float pos[3], float rows[3][3]);

#ifdef __cplusplus
}
#endif
#endif

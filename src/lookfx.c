/* lookfx.c - depth of field, tilt-shift and the camera pulled back (lookfx.h). One pass over the frame: its colour
 * and depth copied into textures, then a full-screen quad whose shader blurs each pixel by its circle of confusion, the
 * larger of the depth of field's (its distance from the focus, the depth at the middle of the picture: the monsters)
 * and the tilt-shift's (its distance from the sharp band). A 24-tap disc, weighted so the sharp parts do not bleed. */
#include "lookfx.h"
#include "plat.h"
#include "postfx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#ifndef APIENTRY
#define APIENTRY
#endif
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT24 0x81A6

typedef char GLch;
static void   (APIENTRY *p_ActiveTexture)(GLenum);
static GLuint (APIENTRY *p_CreateShader)(GLenum);
static void   (APIENTRY *p_ShaderSource)(GLuint, GLsizei, const GLch *const *, const GLint *);
static void   (APIENTRY *p_CompileShader)(GLuint);
static void   (APIENTRY *p_GetShaderiv)(GLuint, GLenum, GLint *);
static void   (APIENTRY *p_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLch *);
static GLuint (APIENTRY *p_CreateProgram)(void);
static void   (APIENTRY *p_AttachShader)(GLuint, GLuint);
static void   (APIENTRY *p_LinkProgram)(GLuint);
static void   (APIENTRY *p_GetProgramiv)(GLuint, GLenum, GLint *);
static void   (APIENTRY *p_UseProgram)(GLuint);
static GLint  (APIENTRY *p_GetUniformLocation)(GLuint, const GLch *);
static void   (APIENTRY *p_Uniform1i)(GLint, GLint);
static void   (APIENTRY *p_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);

static LookFx g_fx = { 0, 0.5f, 0, 0.55f, 0.3f, 0.5f, 0, 0 };
static int g_active, g_state;                        /* g_state: 0 not tried, 1 ready, -1 not available */
static GLuint g_prog, g_color, g_depth;
static int g_tw, g_th;
static GLint u_color, u_depth, u_px, u_dof, u_tilt, u_z;

LookFx *lookfx(void) { return &g_fx; }
void lookfx_active(int on) { g_active = on; }

/* ---- lookfx.cfg: key=value lines */
void lookfx_load(void)
{
    FILE *f = fopen("lookfx.cfg", "r");
    if (!f)
        return;
    char k[32];
    float v;
    while (fscanf(f, " %31[^=]=%f", k, &v) == 2) {
        if (!strcmp(k, "dof")) g_fx.dof = v;
        else if (!strcmp(k, "dof_range")) g_fx.dof_range = v;
        else if (!strcmp(k, "tilt")) g_fx.tilt = v;
        else if (!strcmp(k, "tilt_center")) g_fx.tilt_center = v;
        else if (!strcmp(k, "tilt_band")) g_fx.tilt_band = v;
        else if (!strcmp(k, "tilt_color")) g_fx.tilt_color = v;
        else if (!strcmp(k, "zoom")) g_fx.zoom = v;
        else if (!strcmp(k, "pitch")) g_fx.pitch = v;
    }
    fclose(f);
}

void lookfx_save(void)
{
    FILE *f = fopen("lookfx.cfg", "w");
    if (!f)
        return;
    fprintf(f, "dof=%g\ndof_range=%g\ntilt=%g\ntilt_center=%g\ntilt_band=%g\ntilt_color=%g\nzoom=%g\npitch=%g\n",
            g_fx.dof, g_fx.dof_range, g_fx.tilt, g_fx.tilt_center, g_fx.tilt_band, g_fx.tilt_color, g_fx.zoom,
            g_fx.pitch);
    fclose(f);
}

/* ---- the camera: swung about a point ahead of it (the action, PIVOT ahead), back by zoom and down by pitch */
#define PIVOT 40.0f

void lookfx_camera(float pos[3], float rows[3][3])
{
    if (g_fx.zoom <= 0 && g_fx.pitch <= 0)
        return;
    float *right = rows[0], *fwd = rows[1], *up = rows[2], p[3], a = g_fx.pitch * 3.14159265f / 180, c = cosf(a),
          s = sinf(a), f2[3], u2[3];
    for (int i = 0; i < 3; i++)
        p[i] = pos[i] + fwd[i] * PIVOT;
    for (int i = 0; i < 3; i++) {                    /* turned down about the right axis */
        f2[i] = fwd[i] * c - up[i] * s;
        u2[i] = up[i] * c + fwd[i] * s;
    }
    float d = PIVOT * (1 + 1.5f * g_fx.zoom);
    for (int i = 0; i < 3; i++) {
        fwd[i] = f2[i];
        up[i] = u2[i];
        pos[i] = p[i] - fwd[i] * d;
    }
    (void)right;
}

/* ---- the pass */
static const char *VS =
    "#version 120\n"
    "varying vec2 uv;\n"
    "void main() { uv = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }\n";

static const char *FS =
    "#version 120\n"
    "uniform sampler2D color, depth;\n"
    "uniform vec4 px;    /* 1/w, 1/h, the most blur in pixels, the aspect */\n"
    "uniform vec4 dof;   /* strength, the sharp range (view units), focus depth (view units), - */\n"
    "uniform vec4 tilt;  /* strength, band middle (0 top), band half height, colour */\n"
    "uniform vec4 zr;    /* near, far */\n"
    "varying vec2 uv;\n"
    "float lin(float d) { float z = d * 2.0 - 1.0; return 2.0 * zr.x * zr.y / (zr.y + zr.x - z * (zr.y - zr.x)); }\n"
    "float coc(vec2 t) {\n"
    "    float c = 0.0;\n"
    "    if (dof.x > 0.0) { float z = lin(texture2D(depth, t).r);\n"
    "        c = dof.x * clamp((abs(z - dof.z) - dof.y) / (dof.z + dof.y), 0.0, 1.0); }\n"
    "    if (tilt.x > 0.0) { float y = abs((1.0 - t.y) - tilt.y);\n"
    "        c = max(c, tilt.x * smoothstep(tilt.z, tilt.z + 0.3, y)); }\n"
    "    return c;\n"
    "}\n"
    "const vec2 disc[24] = vec2[24](\n"
    "    vec2(0.21,0.0), vec2(0.0,0.21), vec2(-0.21,0.0), vec2(0.0,-0.21),\n"
    "    vec2(0.33,0.33), vec2(-0.33,0.33), vec2(-0.33,-0.33), vec2(0.33,-0.33),\n"
    "    vec2(0.6,0.12), vec2(0.12,0.6), vec2(-0.6,0.12), vec2(-0.12,-0.6),\n"
    "    vec2(0.12,-0.6), vec2(-0.6,-0.12), vec2(0.45,-0.45), vec2(-0.45,0.45),\n"
    "    vec2(0.95,0.0), vec2(0.67,0.67), vec2(0.0,0.95), vec2(-0.67,0.67),\n"
    "    vec2(-0.95,0.0), vec2(-0.67,-0.67), vec2(0.0,-0.95), vec2(0.67,-0.67));\n"
    "void main() {\n"
    "    float c0 = coc(uv);\n"
    "    vec4 sum = texture2D(color, uv); float wsum = 1.0;\n"
    "    if (c0 > 0.01) {\n"
    "        vec2 r = px.xy * px.z * c0;\n"
    "        for (int i = 0; i < 24; i++) {\n"
    "            vec2 t = uv + disc[i] * r;\n"
    "            float w = clamp(coc(t) * 3.0, 0.15, 1.0);   /* a sharp neighbour gives little */\n"
    "            sum += texture2D(color, t) * w; wsum += w;\n"
    "        }\n"
    "    }\n"
    "    vec3 col = sum.rgb / wsum;\n"
    "    if (tilt.x > 0.0 && tilt.w > 0.0) {   /* the miniature: livelier and a touch more contrast */\n"
    "        float g = dot(col, vec3(0.299, 0.587, 0.114)), k = tilt.w * tilt.x;\n"
    "        col = mix(vec3(g), col, 1.0 + 0.6 * k);\n"
    "        col = (col - 0.5) * (1.0 + 0.15 * k) + 0.5;\n"
    "    }\n"
    "    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = p_CreateShader(type);
    p_ShaderSource(s, 1, &src, NULL);
    p_CompileShader(s);
    GLint ok = 0;
    p_GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        p_GetShaderInfoLog(s, sizeof log, NULL, log);
        printf("lookfx: shader: %s\n", log);
        return 0;
    }
    return s;
}

static int init(void)
{
#define GP(v, name) do { void (*f_)(void) = plat_gl_proc(name); memcpy(&v, &f_, sizeof f_); if (!f_) return -1; } while (0)
    GP(p_ActiveTexture, "glActiveTexture");
    GP(p_CreateShader, "glCreateShader");
    GP(p_ShaderSource, "glShaderSource");
    GP(p_CompileShader, "glCompileShader");
    GP(p_GetShaderiv, "glGetShaderiv");
    GP(p_GetShaderInfoLog, "glGetShaderInfoLog");
    GP(p_CreateProgram, "glCreateProgram");
    GP(p_AttachShader, "glAttachShader");
    GP(p_LinkProgram, "glLinkProgram");
    GP(p_GetProgramiv, "glGetProgramiv");
    GP(p_UseProgram, "glUseProgram");
    GP(p_GetUniformLocation, "glGetUniformLocation");
    GP(p_Uniform1i, "glUniform1i");
    GP(p_Uniform4f, "glUniform4f");
#undef GP
    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!vs || !fs)
        return -1;
    g_prog = p_CreateProgram();
    p_AttachShader(g_prog, vs);
    p_AttachShader(g_prog, fs);
    p_LinkProgram(g_prog);
    GLint ok = 0;
    p_GetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok)
        return -1;
    u_color = p_GetUniformLocation(g_prog, "color");
    u_depth = p_GetUniformLocation(g_prog, "depth");
    u_px = p_GetUniformLocation(g_prog, "px");
    u_dof = p_GetUniformLocation(g_prog, "dof");
    u_tilt = p_GetUniformLocation(g_prog, "tilt");
    u_z = p_GetUniformLocation(g_prog, "zr");
    glGenTextures(1, &g_color);
    glGenTextures(1, &g_depth);
    return 0;
}

static void size_textures(int w, int h)
{
    if (w == g_tw && h == g_th)
        return;
    g_tw = w;
    g_th = h;
    glBindTexture(GL_TEXTURE_2D, g_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, g_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void lookfx_frame(int w, int h, float znear, float zfar)
{
    if (!g_active || (g_fx.dof <= 0 && g_fx.tilt <= 0) || w <= 0 || h <= 0 || postfx_samples() > 0)
        return;                                      /* (MSAA's target cannot be copied: with MSAA on, none) */
    if (!g_state)
        g_state = init() == 0 ? 1 : -1;
    if (g_state < 0)
        return;
    size_textures(w, h);
    /* the frame as drawn: colour and depth (glCopyTexSubImage2D reads the framebuffer bound for reading) */
    p_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_color);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    float focus = 60;                                /* the depth at the middle of the picture: the monsters */
    if (g_fx.dof > 0) {
        p_ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_depth);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
        float d[5];
        static const float at[5][2] = { { 0.5f, 0.45f }, { 0.4f, 0.4f }, { 0.6f, 0.4f }, { 0.45f, 0.55f },
                                         { 0.55f, 0.55f } };
        float best = 1e9f;
        for (int i = 0; i < 5; i++) {                /* the nearest of five taps about the middle */
            glReadPixels((int)(at[i][0] * w), (int)(at[i][1] * h), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d[i]);
            float z = d[i] * 2 - 1, lin = 2 * znear * zfar / (zfar + znear - z * (zfar - znear));
            if (lin < best)
                best = lin;
        }
        static float smooth = 60;
        smooth += (best - smooth) * 0.15f;           /* eased, so a passing helicopter does not snap it */
        focus = smooth;
        p_ActiveTexture(GL_TEXTURE0);
    }
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    p_UseProgram(g_prog);
    p_Uniform1i(u_color, 0);
    p_Uniform1i(u_depth, 1);
    p_Uniform4f(u_px, 1.0f / w, 1.0f / h, 14.0f * h / 720.0f, (float)w / h);
    p_Uniform4f(u_dof, g_fx.dof, 5 + 60 * g_fx.dof_range, focus, 0);
    p_Uniform4f(u_tilt, g_fx.tilt, g_fx.tilt_center, 0.5f * g_fx.tilt_band, g_fx.tilt_color);
    p_Uniform4f(u_z, znear, zfar, 0, 0);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    p_UseProgram(0);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
    p_ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

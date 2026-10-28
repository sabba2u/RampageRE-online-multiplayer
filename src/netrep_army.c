/* netrep_army.c - online play: the army (units, shots) and the bosses (included by netgame.c; channels NGP_ARMY, NGP_BOSS).
 * The host's system runs as offline; the guest's leaves its logic out (net_guest_world() in its step in main.c) and
 * takes each unit from the host's snapshots (ng_pup[...].write / .apply) and its effects and sounds from the host's
 * events (ng_event / .event). See netgame.c "puppets".
 *
 *   snapshots   20 a second each: the army's live units (slot, serial, template, phase, look, place, turn, clip time,
 *               the weapon clusters' turns), the camera's interest, the live shots (place, way, speed; a beam its
 *               firing frame and stream); the bosses' live parts (template, look, place, turn, scale, clip time, the
 *               HUD's share) and the boss. Names go as ids; a name's text rides in the snapshots for half a second
 *               after its first use and then once a second, so a guest that missed one has it soon
 *   events      what main.c turns into effects, sounds and the HUD's floats (army_events / boss_events: the host's are
 *               sent as main.c takes them; the guest's own are dropped and the host's given in their place, at their
 *               host time), and a window soldier's window opening. An event on a unit, a shot or a part the guest's
 *               snapshots do not have yet makes it there and then (from the event), so what follows it follows it
 *   on the guest  army_net_gate / boss_net_gate: its monsters' hits, grabs and throws, the people's weapons, the
 *               windows' spawner make nothing; a held unit is placed in its monster's hand by main.c (army_hold) as
 *               offline, from where the host's snapshot had it */

#define NGA_STRS 1024                                /* names (templates, shapes, clips, nodes) a session at most */
#define NGA_UNITS 160                                /* army.c's MAX_UNITS */
#define NGA_SHOTS 128                                /* army.c's MAX_SHOTS */
#define NGA_PARTS 48                                 /* boss.c's MAX_PARTS */
#define NGA_PEND 512                                 /* the guest's events not yet taken by main.c */
#define NGA_ESHOTS 64                                /* shots made by an event, not yet in a snapshot */

enum { NGA_EV_ARMY = 1, NGA_EV_WINDOW, NGA_EV_BOSS };

/* ---- names: the host's ids (1 on; 0 none), the guest's texts for them */
typedef struct { const char *p; char s[96]; double first[2], last[2]; } NgaStr;
static NgaStr nga_hs[NGA_STRS];
static int nga_nhs;
static const char *nga_gs[NGA_STRS + 1];             /* the guest's: id -> its interned text */

static const char *nga_intern(const char *s)        /* the guest's copy of a text, kept for the session */
{
    static char *pool[4096];
    static int n;
    if (!s)
        return NULL;
    for (int i = 0; i < n; i++)
        if (!strcmp(pool[i], s))
            return pool[i];
    if (n == 4096)
        return NULL;
    size_t len = strlen(s);
    char *c = malloc(len + 1);
    if (!c)
        return NULL;
    memcpy(c, s, len + 1);
    pool[n++] = c;
    return c;
}

/* the host: a name's id, and its text due in this channel's snapshot (ch 0 army, 1 bosses) */
static uint8_t nga_due[NGA_STRS];
static int nga_due_list[NGA_STRS], nga_ndue;
static int nga_str(const char *s, int ch)
{
    if (!s)
        return 0;
    int i = 0;
    while (i < nga_nhs && nga_hs[i].p != s && strcmp(nga_hs[i].s, s))
        i++;
    if (i == nga_nhs) {
        if (nga_nhs == NGA_STRS || strlen(s) >= sizeof nga_hs[0].s)
            return 0;
        memset(&nga_hs[i], 0, sizeof nga_hs[i]);
        snprintf(nga_hs[i].s, sizeof nga_hs[i].s, "%s", s);
        nga_hs[i].first[0] = nga_hs[i].first[1] = nga_hs[i].last[0] = nga_hs[i].last[1] = -1e9;
        nga_nhs++;
    }
    nga_hs[i].p = s;
    double now = net_now();
    NgaStr *e = &nga_hs[i];
    if (e->first[ch] < -1e8)
        e->first[ch] = now;
    if (!nga_due[i] && (now - e->first[ch] < 0.5 || now - e->last[ch] >= 1.0)) {
        nga_due[i] = 1;
        nga_due_list[nga_ndue++] = i;
        e->last[ch] = now;
    }
    return i + 1;
}

static void nga_dict_write(NetBuf *b)                /* the texts due, at the snapshot's end */
{
    net_w_u16(b, (uint32_t)nga_ndue);
    for (int k = 0; k < nga_ndue; k++) {
        int i = nga_due_list[k];
        int len = (int)strlen(nga_hs[i].s);
        net_w_u16(b, (uint32_t)(i + 1));
        net_w_u8(b, (uint32_t)len);
        net_w_bytes(b, nga_hs[i].s, len);
        nga_due[i] = 0;
    }
    nga_ndue = 0;
}

static void nga_dict_read(NetBuf *b)
{
    int n = (int)net_r_u16(b);
    for (int k = 0; k < n && !b->err; k++) {
        int id = (int)net_r_u16(b), len = (int)net_r_u8(b);
        char s[256];
        net_r_bytes(b, s, len);
        s[len] = 0;
        if (!b->err && id >= 1 && id <= NGA_STRS && !nga_gs[id])
            nga_gs[id] = nga_intern(s);
    }
}

static const char *nga_name(int id) { return id >= 1 && id <= NGA_STRS ? nga_gs[id] : NULL; }

/* a text inline (events): its length + 1, 0 none */
static void nga_w_text(NetBuf *b, const char *s)
{
    int len = s ? (int)strlen(s) : -1;
    if (len > 200)
        len = 200;
    net_w_u8(b, (uint32_t)(len + 1));
    if (len > 0)
        net_w_bytes(b, s, len);
}
static const char *nga_r_text(NetBuf *b)
{
    int len = (int)net_r_u8(b) - 1;
    if (len < 0 || b->err)
        return NULL;
    char s[256];
    net_r_bytes(b, s, len);
    s[len] = 0;
    return b->err ? NULL : nga_intern(s);
}

/* ---- turns: a matrix's 3 x 3 (column-major 4 x 4) as a quaternion (x, y, z, w) and its columns' lengths (a mirror:
 * the first one's negative) */
static void nga_m2q(const float *w, float q[4], float sc[3])
{
    float m[3][3];
    for (int c = 0; c < 3; c++) {
        float l = sqrtf(w[4 * c] * w[4 * c] + w[4 * c + 1] * w[4 * c + 1] + w[4 * c + 2] * w[4 * c + 2]);
        sc[c] = l;
        for (int r = 0; r < 3; r++)
            m[r][c] = l > 1e-6f ? w[4 * c + r] / l : (r == c);
    }
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (det < 0) {
        sc[0] = -sc[0];
        for (int r = 0; r < 3; r++)
            m[r][0] = -m[r][0];
    }
    float tr = m[0][0] + m[1][1] + m[2][2], s;
    if (tr > 0) {
        s = sqrtf(tr + 1) * 2;
        q[3] = s / 4; q[0] = (m[2][1] - m[1][2]) / s; q[1] = (m[0][2] - m[2][0]) / s; q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        s = sqrtf(1 + m[0][0] - m[1][1] - m[2][2]) * 2;
        q[3] = (m[2][1] - m[1][2]) / s; q[0] = s / 4; q[1] = (m[0][1] + m[1][0]) / s; q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        s = sqrtf(1 + m[1][1] - m[0][0] - m[2][2]) * 2;
        q[3] = (m[0][2] - m[2][0]) / s; q[0] = (m[0][1] + m[1][0]) / s; q[1] = s / 4; q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        s = sqrtf(1 + m[2][2] - m[0][0] - m[1][1]) * 2;
        q[3] = (m[1][0] - m[0][1]) / s; q[0] = (m[0][2] + m[2][0]) / s; q[1] = (m[1][2] + m[2][1]) / s; q[2] = s / 4;
    }
}

static void nga_q2m(const float q[4], const float sc[3], const float pos[3], float *w)
{
    float x = q[0], y = q[1], z = q[2], qw = q[3];
    float m[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * qw), 2 * (x * z + y * qw) },
                      { 2 * (x * y + z * qw), 1 - 2 * (x * x + z * z), 2 * (y * z - x * qw) },
                      { 2 * (x * z - y * qw), 2 * (y * z + x * qw), 1 - 2 * (x * x + y * y) } };
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++)
            w[4 * c + r] = m[r][c] * sc[c];
        w[4 * c + 3] = 0;
    }
    w[12] = pos[0];
    w[13] = pos[1];
    w[14] = pos[2];
    w[15] = 1;
}

static void nga_nlerp(const float a[4], const float b[4], float t, float out[4])
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = d < 0 ? -1.0f : 1.0f, l = 0;
    for (int k = 0; k < 4; k++) {
        out[k] = a[k] * (1 - t) + b[k] * t * s;
        l += out[k] * out[k];
    }
    l = sqrtf(l);
    for (int k = 0; k < 4; k++)
        out[k] = l > 1e-6f ? out[k] / l : (k == 3);
}

/* a place and turn: 3 floats, 4 shorts, the lengths only when not all 1 (flag) */
static void nga_w_frame(NetBuf *b, const float *world)
{
    float q[4], sc[3];
    nga_m2q(world, q, sc);
    int scaled = fabsf(sc[0] - 1) > 1e-3f || fabsf(sc[1] - 1) > 1e-3f || fabsf(sc[2] - 1) > 1e-3f;
    for (int k = 0; k < 3; k++)
        net_w_f32(b, world[12 + k]);
    for (int k = 0; k < 4; k++)
        net_w_u16(b, (uint32_t)(uint16_t)(int16_t)lrintf(q[k] * 32767));
    net_w_u8(b, (uint32_t)scaled);
    if (scaled)
        for (int k = 0; k < 3; k++)
            net_w_f32(b, sc[k]);
}
typedef struct { float pos[3], q[4], sc[3]; } NgaFrame;
static void nga_r_frame(NetBuf *b, NgaFrame *f)
{
    for (int k = 0; k < 3; k++)
        f->pos[k] = net_r_f32(b);
    float l = 0;
    for (int k = 0; k < 4; k++) {
        f->q[k] = (float)(int16_t)net_r_u16(b) / 32767;
        l += f->q[k] * f->q[k];
    }
    l = sqrtf(l);
    for (int k = 0; k < 4; k++)
        f->q[k] = l > 1e-6f ? f->q[k] / l : (k == 3);
    f->sc[0] = f->sc[1] = f->sc[2] = 1;
    if (net_r_u8(b))
        for (int k = 0; k < 3; k++)
            f->sc[k] = net_r_f32(b);
}
static void nga_frame_world(const NgaFrame *lo, const NgaFrame *hi, float t, float *world)
{
    float pos[3], q[4], sc[3];
    for (int k = 0; k < 3; k++) {
        pos[k] = lo->pos[k] + (hi->pos[k] - lo->pos[k]) * t;
        sc[k] = lo->sc[k] + (hi->sc[k] - lo->sc[k]) * t;
    }
    nga_nlerp(lo->q, hi->q, t, q);
    nga_q2m(q, sc, pos, world);
}

static float nga_lerp_rad(float a, float b, float t)   /* radians, the short way */
{
    float d = fmodf(b - a + 3 * 3.14159265f, 2 * 3.14159265f) - 3.14159265f;
    return a + d * t;
}

/* the clip's time between two samples: on in the same play, else the newer's less the time still to come */
static float nga_clip_time(int same, float ta, float tb, float alpha, double span)
{
    if (same && tb >= ta)
        return ta + (tb - ta) * alpha;
    float t = tb - (float)((1 - alpha) * span);
    return t > 0 ? t : 0;
}

/* ================================================================ the army */

typedef struct {
    int slot, serial, name, phase, shape, clip, nw;
    NgaFrame f;
    float t, len, yaw[ARMY_WEAPONS], pitch[ARMY_WEAPONS];
    uint32_t oflags, inst_at;
} NgaUnit;
typedef struct {
    int id, beam;
    float pos[3], dir[3], speed, gravity, R[3][3], head, tail, length;
} NgaShot;
typedef struct {
    int block, count, nu, ns, interest[2];
    NgaUnit u[NGA_UNITS];
    NgaShot s[NGA_SHOTS];
} NgaArmySnap;

static void nga_w_shot(NetBuf *b, const ArmyShot *s)
{
    net_w_u32(b, (uint32_t)s->id);
    net_w_u8(b, (uint32_t)s->beam);
    for (int k = 0; k < 3; k++)
        net_w_f32(b, s->pos[k]);
    for (int k = 0; k < 3; k++)
        net_w_u16(b, (uint32_t)(uint16_t)(int16_t)lrintf(fmaxf(-1, fminf(1, s->dir[k])) * 32767));
    net_w_f32(b, s->speed);
    net_w_f32(b, s->gravity);
    if (s->beam) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                net_w_f32(b, s->R[r][c]);
        net_w_f32(b, s->head);
        net_w_f32(b, s->tail);
        net_w_f32(b, s->length);
    }
}
static void nga_r_shot(NetBuf *b, NgaShot *s)
{
    memset(s, 0, sizeof *s);
    s->id = (int)net_r_u32(b);
    s->beam = (int)net_r_u8(b);
    for (int k = 0; k < 3; k++)
        s->pos[k] = net_r_f32(b);
    for (int k = 0; k < 3; k++)
        s->dir[k] = (float)(int16_t)net_r_u16(b) / 32767;
    s->speed = net_r_f32(b);
    s->gravity = net_r_f32(b);
    if (s->beam) {
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                s->R[r][c] = net_r_f32(b);
        s->head = net_r_f32(b);
        s->tail = net_r_f32(b);
        s->length = net_r_f32(b);
    }
}

static int nga_army_write(Viewer *v, NetBuf *b)
{
    Army *a = v->army;
    if (!a)
        return 0;
    net_w_u8(b, (uint32_t)v->block);
    int n = army_count(a), live = 0;
    for (int i = 0; i < n; i++)
        live += army_unit(a, i)->phase != AU_GONE;
    net_w_u16(b, (uint32_t)n);
    net_w_u16(b, (uint32_t)live);
    for (int i = 0; i < n; i++) {
        const ArmyUnit *u = army_unit(a, i);
        if (u->phase == AU_GONE)
            continue;
        net_w_u8(b, (uint32_t)i);
        net_w_u32(b, (uint32_t)u->serial);
        net_w_u16(b, (uint32_t)nga_str(u->name, 0));
        net_w_u8(b, (uint32_t)u->phase);
        net_w_u16(b, (uint32_t)nga_str(u->shape, 0));
        net_w_u16(b, (uint32_t)nga_str(u->clip, 0));
        nga_w_frame(b, u->world);
        net_w_f32(b, u->anim_time);
        net_w_f32(b, u->anim_len);
        net_w_u32(b, u->oflags);
        net_w_u32(b, u->inst.at);
        int nw = u->nw < ARMY_WEAPONS ? u->nw : ARMY_WEAPONS;
        net_w_u8(b, (uint32_t)nw);
        for (int k = 0; k < nw; k++) {
            net_w_f32(b, u->w[k].yaw);
            net_w_f32(b, u->w[k].pitch);
        }
    }
    for (int k = 0; k < 2; k++)
        net_w_u16(b, (uint32_t)(uint16_t)(int16_t)army_net_interest(a, k));
    int ns = 0, at = b->len;
    net_w_u16(b, 0);
    for (int i = 0; i < army_shots(a) && b->len < NET_SNAP_MAX - 3000; i++)   /* (room kept for the names) */
        if (army_shot(a, i)->alive) {
            nga_w_shot(b, army_shot(a, i));
            ns++;
        }
    if (!b->err) {
        b->p[at] = (uint8_t)ns;
        b->p[at + 1] = (uint8_t)(ns >> 8);
    }
    nga_dict_write(b);
    return 1;
}

static int nga_army_read(const NetSnapSample *s, NgaArmySnap *o)
{
    NetBuf b = netbuf((void *)s->data, s->len);
    o->block = (int)net_r_u8(&b);
    o->count = (int)net_r_u16(&b);
    o->nu = (int)net_r_u16(&b);
    if (o->nu > NGA_UNITS || o->count > NGA_UNITS)
        return 0;
    for (int i = 0; i < o->nu; i++) {
        NgaUnit *u = &o->u[i];
        u->slot = (int)net_r_u8(&b);
        u->serial = (int)net_r_u32(&b);
        u->name = (int)net_r_u16(&b);
        u->phase = (int)net_r_u8(&b);
        u->shape = (int)net_r_u16(&b);
        u->clip = (int)net_r_u16(&b);
        nga_r_frame(&b, &u->f);
        u->t = net_r_f32(&b);
        u->len = net_r_f32(&b);
        u->oflags = net_r_u32(&b);
        u->inst_at = net_r_u32(&b);
        u->nw = (int)net_r_u8(&b);
        if (u->nw > ARMY_WEAPONS)
            return 0;
        for (int k = 0; k < u->nw; k++) {
            u->yaw[k] = net_r_f32(&b);
            u->pitch[k] = net_r_f32(&b);
        }
    }
    for (int k = 0; k < 2; k++)
        o->interest[k] = (int16_t)net_r_u16(&b);
    o->ns = (int)net_r_u16(&b);
    if (o->ns > NGA_SHOTS)
        return 0;
    for (int i = 0; i < o->ns; i++)
        nga_r_shot(&b, &o->s[i]);
    nga_dict_read(&b);
    return !b.err;
}

/* the guest's own bookkeeping: a unit or a shot an event made before a snapshot had it (the host time it was made) */
static double nga_born[NGA_UNITS];
typedef struct { NgaShot s; double t; } NgaEShot;
static NgaEShot nga_eshot[NGA_ESHOTS];
static int nga_neshot;
static ArmyEvent nga_pend[NGA_PEND];
static int nga_npend;
static double nga_shown;                             /* the host time the army's snapshots show now */

static void nga_put_shot(ArmyShot *dst, const NgaShot *s, const float pos[3])
{
    memset(dst, 0, sizeof *dst);
    dst->id = s->id;
    dst->beam = s->beam;
    dst->alive = 1;
    dst->owner = -1;
    memcpy(dst->pos, pos, sizeof dst->pos);
    memcpy(dst->dir, s->dir, sizeof dst->dir);
    dst->speed = s->speed;
    dst->gravity = s->gravity;
    memcpy(dst->R, s->R, sizeof dst->R);
    dst->head = s->head;
    dst->tail = s->tail;
    dst->length = s->length;
}

/* a shot t seconds on from where it was (its fall as shot_update has it) */
static void nga_shot_on(const NgaShot *s, float t, float pos[3])
{
    for (int k = 0; k < 3; k++)
        pos[k] = s->pos[k] + (s->beam ? 0 : s->dir[k] * s->speed * t);
    if (!s->beam)
        pos[2] -= 0.5f * 9.81f * s->gravity * t * t;
}

static int nga_held_here(const Viewer *v, int slot)   /* a monster here holds it: main.c's army_hold places it */
{
    for (int k = 0; k < v->nmon; k++)
        if (v->mon[k].held_unit == slot)
            return 1;
    return 0;
}

static void nga_army_apply(Viewer *v, const NetSnapSample *sa, const NetSnapSample *sb, float alpha, int how)
{
    static NgaArmySnap A, B;
    Army *a = v->army;
    if (!a || !nga_army_read(sb, &B) || B.block != v->block)
        return;
    int two = how == 2 && nga_army_read(sa, &A) && A.block == v->block;
    double span = two ? sb->t - sa->t : 0;
    nga_shown = two ? sa->t + alpha * span : sb->t + alpha;
    static int in_a[NGA_UNITS];                      /* slot -> its index in A + 1 */
    memset(in_a, 0, sizeof in_a);
    for (int i = 0; two && i < A.nu; i++)
        if (A.u[i].slot < NGA_UNITS)
            in_a[A.u[i].slot] = i + 1;
    static uint8_t seen[NGA_UNITS];
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < B.nu; i++) {
        const NgaUnit *hb = &B.u[i];
        const char *name = nga_name(hb->name);
        if (hb->slot >= NGA_UNITS)
            continue;
        seen[hb->slot] = 1;
        if ((hb->name && !name) || nga_born[hb->slot] > sb->t)
            continue;                                /* its name not heard yet, or an event's newer unit: as it is */
        const NgaUnit *ha = in_a[hb->slot] && A.u[in_a[hb->slot] - 1].serial == hb->serial ? &A.u[in_a[hb->slot] - 1] : NULL;
        ArmyUnit *u = army_net_slot(a, hb->slot);
        if (!u || u->serial != hb->serial || u->phase == AU_GONE || u->name != name)
            u = army_net_unit(a, hb->slot, name, hb->serial, hb->inst_at);
        if (!u)
            continue;
        nga_born[hb->slot] = 0;
        int was = u->phase;
        u->phase = hb->phase;
        if (hb->phase == AU_HELD && was != AU_HELD)
            army_net_hold(a, hb->slot);
        u->oflags = hb->oflags;
        if (!hb->shape || nga_name(hb->shape))
            u->shape = nga_name(hb->shape);
        if (!hb->clip || nga_name(hb->clip))
            u->clip = nga_name(hb->clip);
        if (!(hb->phase == AU_HELD && nga_held_here(v, hb->slot))) {
            if (ha)
                nga_frame_world(&ha->f, &hb->f, alpha, u->world);
            else
                nga_frame_world(&hb->f, &hb->f, 0, u->world);
            memcpy(u->pos, u->world + 12, sizeof u->pos);
        }
        u->anim_len = hb->len;
        if (two)
            u->anim_time = nga_clip_time(ha && ha->shape == hb->shape && ha->clip == hb->clip, ha ? ha->t : hb->t, hb->t,
                                         alpha, span);
        else
            u->anim_time = hb->t + alpha;
        for (int k = 0; k < u->nw && k < hb->nw; k++) {
            int ok = ha && k < ha->nw;
            u->w[k].yaw = ok ? nga_lerp_rad(ha->yaw[k], hb->yaw[k], alpha) : hb->yaw[k];
            u->w[k].pitch = ok ? nga_lerp_rad(ha->pitch[k], hb->pitch[k], alpha) : hb->pitch[k];
        }
    }
    int top = 0;
    for (int i = 0; i < army_count(a); i++) {        /* gone at the host, unless an event made it after B */
        ArmyUnit *u = army_net_slot(a, i);
        if (u->phase != AU_GONE && !seen[i] && !(nga_born[i] > sb->t && nga_born[i] < sb->t + 1))
            u->phase = AU_GONE;
        if (u->phase != AU_GONE)
            top = i + 1;
    }
    army_net_count(a, top);
    for (int k = 0; k < 2; k++)
        army_net_set_interest(a, k, B.interest[k]);
    int *pn, cap;                                    /* the shots: B's (from A's place on), and the events' not yet in B */
    ArmyShot *sh = army_net_shots(a, &pn, &cap);
    int n = 0;
    for (int i = 0; i < B.ns && n < cap; i++) {
        const NgaShot *hb = &B.s[i], *ha = NULL;
        for (int j = 0; two && j < A.ns && !ha; j++)
            if (A.s[j].id == hb->id)
                ha = &A.s[j];
        float pos[3];
        if (ha)
            for (int k = 0; k < 3; k++)
                pos[k] = ha->pos[k] + (hb->pos[k] - ha->pos[k]) * alpha;
        else
            nga_shot_on(hb, (float)(nga_shown - sb->t), pos);
        nga_put_shot(&sh[n++], hb, pos);
    }
    for (int i = 0; i < nga_neshot; i++) {
        NgaEShot *e = &nga_eshot[i];
        int in_b = 0;
        for (int j = 0; j < B.ns && !in_b; j++)
            in_b = B.s[j].id == e->s.id;
        if (in_b || e->t <= sb->t || nga_shown - e->t > 1) {
            nga_eshot[i--] = nga_eshot[--nga_neshot];
            continue;
        }
        float pos[3];
        nga_shot_on(&e->s, (float)fmax(0, nga_shown - e->t), pos);
        if (n < cap)
            nga_put_shot(&sh[n++], &e->s, pos);
    }
    *pn = n;
}

/* ---- the army's events */

static int nga_relayed(int type)
{
    return type == AE_EFFECT || type == AE_EFFECT_STOP || type == AE_EFFECT_MOVE || type == AE_SOUND ||
           type == AE_FLOAT || type == AE_WINDOW_CLOSE || type == AE_FART;
}

static int nga_army_events(Army *a, ArmyEvent *out, int n, int max)
{
    if (ng_guest()) {                                /* the guest's own made nothing: the host's in their place */
        int k = nga_npend < max ? nga_npend : max;
        if (out && k > 0) {
            memcpy(out, nga_pend, (size_t)k * sizeof *out);
            memmove(nga_pend, nga_pend + k, (size_t)(nga_npend - k) * sizeof *nga_pend);
        }
        nga_npend -= k;
        return out ? k : 0;
    }
    if (!ng_host() || !out || !g_ngv)
        return n;
    static uint8_t m[4000];
    NetBuf b = netbuf(m, sizeof m);
    int count = 0, at_count = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || b.len > 3300) {                /* (one event's at most ~600 bytes) */
            if (count) {
                m[at_count] = (uint8_t)count;
                ng_event(NGP_ARMY, m, b.len);
            }
            b = netbuf(m, sizeof m);
            count = 0;
            if (i == n)
                break;
        }
        const ArmyEvent *e = &out[i];
        if (!nga_relayed(e->type))
            continue;
        if (!count) {
            net_w_u8(&b, NGA_EV_ARMY);
            net_w_u8(&b, (uint32_t)g_ngv->block);
            net_w_f64(&b, net_now());
            at_count = b.len;
            net_w_u8(&b, 0);
        }
        count++;
        net_w_u8(&b, (uint32_t)e->type);
        net_w_u32(&b, (uint32_t)e->id);
        net_w_u16(&b, (uint32_t)(uint16_t)(int16_t)e->unit);
        net_w_u32(&b, (uint32_t)e->shot);
        net_w_u8(&b, (uint32_t)(uint8_t)(int8_t)e->monster);
        net_w_u8(&b, (uint32_t)(uint8_t)(int8_t)e->level);
        net_w_u16(&b, (uint32_t)(uint16_t)(int16_t)e->node);
        net_w_u8(&b, (uint32_t)e->die_with);
        nga_w_text(&b, e->name);
        for (int k = 0; k < 3; k++) net_w_f32(&b, e->v[k]);
        for (int k = 0; k < 3; k++) net_w_f32(&b, e->dir[k]);
        for (int k = 0; k < 3; k++) net_w_f32(&b, e->pos[k]);
        net_w_f32(&b, e->amount);
        const ArmyUnit *u = e->unit >= 0 ? army_unit(a, e->unit) : NULL;
        net_w_u8(&b, (uint32_t)(u != NULL));
        if (u) {                                     /* the unit it is on, for a guest that has not got it yet */
            net_w_u32(&b, (uint32_t)u->serial);
            nga_w_text(&b, u->name);
            nga_w_frame(&b, u->world);
        }
        const ArmyShot *s = NULL;
        for (int j = 0; e->shot >= 0 && j < army_shots(a) && !s; j++)
            if (army_shot(a, j)->id == e->shot && army_shot(a, j)->alive)
                s = army_shot(a, j);
        net_w_u8(&b, (uint32_t)(s != NULL));
        if (s)
            nga_w_shot(&b, s);
    }
    return n;
}

static void nga_window_open(int building, int storey, int column, int lr, const float rect[4], int opens_h)
{
    if (!ng_host() || !g_ngv)
        return;
    uint8_t m[64];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NGA_EV_WINDOW);
    net_w_u8(&b, (uint32_t)g_ngv->block);
    net_w_u16(&b, (uint32_t)building);
    net_w_u8(&b, (uint32_t)storey);
    net_w_u8(&b, (uint32_t)column);
    net_w_u8(&b, (uint32_t)lr);
    for (int k = 0; k < 4; k++)
        net_w_f32(&b, rect[k]);
    net_w_u8(&b, (uint32_t)opens_h);
    if (!b.err)
        ng_event(NGP_ARMY, m, b.len);
}

static void nga_army_event(Viewer *v, NetBuf *b)
{
    int kind = (int)net_r_u8(b), block = (int)net_r_u8(b);
    if (b->err || block != v->block)
        return;
    if (kind == NGA_EV_WINDOW) {                     /* (the spawner's openWindow, city.c) */
        int bi = (int)net_r_u16(b), storey = (int)net_r_u8(b), column = (int)net_r_u8(b), lr = (int)net_r_u8(b);
        float rect[4];
        for (int k = 0; k < 4; k++)
            rect[k] = net_r_f32(b);
        int opens_h = (int)net_r_u8(b);
        if (!b->err && (uint32_t)bi < v->scene.nbuildings)
            building_window_open(&v->scene.buildings[bi].b, storey, column, lr, 1, 0.5f, rect, opens_h);
        return;
    }
    if (kind != NGA_EV_ARMY || !v->army)
        return;
    double t = net_r_f64(b);
    int count = (int)net_r_u8(b);
    for (int i = 0; i < count && !b->err; i++) {
        ArmyEvent e;
        memset(&e, 0, sizeof e);
        e.type = (int)net_r_u8(b);
        e.id = (int)net_r_u32(b);
        e.unit = (int16_t)net_r_u16(b);
        e.shot = (int)net_r_u32(b);
        e.monster = (int8_t)net_r_u8(b);
        e.level = (int8_t)net_r_u8(b);
        e.node = (int16_t)net_r_u16(b);
        e.die_with = (int)net_r_u8(b);
        e.name = nga_r_text(b);
        for (int k = 0; k < 3; k++) e.v[k] = net_r_f32(b);
        for (int k = 0; k < 3; k++) e.dir[k] = net_r_f32(b);
        for (int k = 0; k < 3; k++) e.pos[k] = net_r_f32(b);
        e.amount = net_r_f32(b);
        if (net_r_u8(b)) {                           /* its unit: made here if the snapshots have not got it yet */
            int serial = (int)net_r_u32(b);
            const char *name = nga_r_text(b);
            NgaFrame f;
            nga_r_frame(b, &f);
            ArmyUnit *u = army_net_slot(v->army, e.unit);
            if (!b->err && e.unit >= 0 && e.unit < NGA_UNITS && (!u || u->serial != serial || u->phase == AU_GONE)) {
                u = army_net_unit(v->army, e.unit, name, serial, 0);
                if (u) {
                    nga_frame_world(&f, &f, 0, u->world);
                    memcpy(u->pos, u->world + 12, sizeof u->pos);
                    nga_born[e.unit] = t;
                }
            }
        }
        if (net_r_u8(b)) {                           /* its shot: here at once, till the snapshots have it */
            NgaShot s;
            nga_r_shot(b, &s);
            int *pn, cap, have = 0;
            ArmyShot *sh = army_net_shots(v->army, &pn, &cap);
            for (int j = 0; j < *pn && !have; j++)
                have = sh[j].id == s.id && sh[j].alive;
            if (!b->err && !have && *pn < cap) {
                nga_put_shot(&sh[(*pn)++], &s, s.pos);
                if (nga_neshot < NGA_ESHOTS) {
                    nga_eshot[nga_neshot].s = s;
                    nga_eshot[nga_neshot++].t = t;
                }
            }
        }
        if (!b->err && nga_npend < NGA_PEND)
            nga_pend[nga_npend++] = e;
    }
}

static void nga_army_reset(Viewer *v)
{
    (void)v;
    nga_npend = 0;
    nga_neshot = 0;
    memset(nga_born, 0, sizeof nga_born);
}

static int nga_gate(void) { return ng_guest(); }

/* ================================================================ the bosses */

typedef struct {
    int slot, name, owner, is_boss, animate, shape, clip, nw;
    NgaFrame f;
    float facing[3], scale, t, len, hp_frac, yaw[BOSS_WEAPONS], pitch[BOSS_WEAPONS];
} NgbPart;
typedef struct { int block, count, main, defeated, np; NgbPart p[NGA_PARTS]; } NgbSnap;

static int ngb_write(Viewer *v, NetBuf *b)
{
    Boss *bo = v->boss;
    if (!bo)
        return 0;
    net_w_u8(b, (uint32_t)v->block);
    int n = boss_count(bo), live = 0;
    for (int i = 0; i < n; i++)
        live += boss_part(bo, i)->alive != 0;
    net_w_u8(b, (uint32_t)n);
    net_w_u8(b, (uint32_t)(uint8_t)(int8_t)boss_main(bo));
    net_w_u8(b, (uint32_t)boss_defeated(bo));
    net_w_u8(b, (uint32_t)live);
    for (int i = 0; i < n; i++) {
        const BossPart *p = boss_part(bo, i);
        if (!p->alive)
            continue;
        net_w_u8(b, (uint32_t)i);
        net_w_u16(b, (uint32_t)nga_str(p->name, 1));
        net_w_u8(b, (uint32_t)(uint8_t)(int8_t)p->owner);
        net_w_u8(b, (uint32_t)(p->is_boss | p->animate << 1));
        net_w_u16(b, (uint32_t)nga_str(p->shape, 1));
        net_w_u16(b, (uint32_t)nga_str(p->clip, 1));
        nga_w_frame(b, p->world);
        for (int k = 0; k < 3; k++)
            net_w_f32(b, p->facing[k]);
        net_w_f32(b, p->scale);
        net_w_f32(b, p->anim_time);
        net_w_f32(b, p->anim_len);
        net_w_f32(b, p->hp_frac);
        int nw = p->nw < BOSS_WEAPONS ? p->nw : BOSS_WEAPONS;
        net_w_u8(b, (uint32_t)nw);
        for (int k = 0; k < nw; k++) {
            net_w_f32(b, p->w[k].yaw);
            net_w_f32(b, p->w[k].pitch);
        }
    }
    nga_dict_write(b);
    return 1;
}

static int ngb_read(const NetSnapSample *s, NgbSnap *o)
{
    NetBuf b = netbuf((void *)s->data, s->len);
    o->block = (int)net_r_u8(&b);
    o->count = (int)net_r_u8(&b);
    o->main = (int8_t)net_r_u8(&b);
    o->defeated = (int)net_r_u8(&b);
    o->np = (int)net_r_u8(&b);
    if (o->np > NGA_PARTS || o->count > NGA_PARTS)
        return 0;
    for (int i = 0; i < o->np; i++) {
        NgbPart *p = &o->p[i];
        p->slot = (int)net_r_u8(&b);
        p->name = (int)net_r_u16(&b);
        p->owner = (int8_t)net_r_u8(&b);
        int fl = (int)net_r_u8(&b);
        p->is_boss = fl & 1;
        p->animate = fl >> 1 & 1;
        p->shape = (int)net_r_u16(&b);
        p->clip = (int)net_r_u16(&b);
        nga_r_frame(&b, &p->f);
        for (int k = 0; k < 3; k++)
            p->facing[k] = net_r_f32(&b);
        p->scale = net_r_f32(&b);
        p->t = net_r_f32(&b);
        p->len = net_r_f32(&b);
        p->hp_frac = net_r_f32(&b);
        p->nw = (int)net_r_u8(&b);
        if (p->nw > BOSS_WEAPONS)
            return 0;
        for (int k = 0; k < p->nw; k++) {
            p->yaw[k] = net_r_f32(&b);
            p->pitch[k] = net_r_f32(&b);
        }
    }
    nga_dict_read(&b);
    return !b.err;
}

static double ngb_born[NGA_PARTS];

static void ngb_apply(Viewer *v, const NetSnapSample *sa, const NetSnapSample *sb, float alpha, int how)
{
    static NgbSnap A, B;
    Boss *bo = v->boss;
    if (!bo || !ngb_read(sb, &B) || B.block != v->block)
        return;
    int two = how == 2 && ngb_read(sa, &A) && A.block == v->block;
    double span = two ? sb->t - sa->t : 0;
    uint8_t seen[NGA_PARTS] = { 0 };
    for (int i = 0; i < B.np; i++) {
        const NgbPart *hb = &B.p[i], *ha = NULL;
        const char *name = nga_name(hb->name);
        if (hb->slot >= NGA_PARTS || !name)
            continue;
        seen[hb->slot] = 1;
        for (int j = 0; two && j < A.np && !ha; j++)
            if (A.p[j].slot == hb->slot && A.p[j].name == hb->name)
                ha = &A.p[j];
        BossPart *p = boss_net_slot(bo, hb->slot);
        if (!p || !p->alive || p->name != name)
            p = boss_net_part(bo, hb->slot, name, hb->f.pos);
        if (!p)
            continue;
        ngb_born[hb->slot] = 0;
        p->owner = hb->owner;
        p->is_boss = hb->is_boss;
        p->animate = hb->animate;
        if (!hb->shape || nga_name(hb->shape))
            p->shape = nga_name(hb->shape);
        if (!hb->clip || nga_name(hb->clip))
            p->clip = nga_name(hb->clip);
        if (ha)
            nga_frame_world(&ha->f, &hb->f, alpha, p->world);
        else
            nga_frame_world(&hb->f, &hb->f, 0, p->world);
        memcpy(p->pos, p->world + 12, sizeof p->pos);
        memcpy(p->facing, hb->facing, sizeof p->facing);
        p->scale = hb->scale;
        p->anim_len = hb->len;
        p->anim_time = two ? nga_clip_time(ha && ha->shape == hb->shape && ha->clip == hb->clip, ha ? ha->t : hb->t,
                                           hb->t, alpha, span) : hb->t + alpha;
        p->hp_frac = hb->hp_frac;
        for (int k = 0; k < p->nw && k < hb->nw; k++) {
            int ok = ha && k < ha->nw;
            p->w[k].yaw = ok ? nga_lerp_rad(ha->yaw[k], hb->yaw[k], alpha) : hb->yaw[k];
            p->w[k].pitch = ok ? nga_lerp_rad(ha->pitch[k], hb->pitch[k], alpha) : hb->pitch[k];
        }
    }
    int top = 0;
    for (int i = 0; i < boss_count(bo); i++) {
        BossPart *p = boss_net_slot(bo, i);
        if (p->alive && !seen[i] && !(ngb_born[i] > sb->t && ngb_born[i] < sb->t + 1))
            p->alive = 0;
        if (p->alive)
            top = i + 1;
    }
    boss_net_state(bo, top, B.main, B.defeated);
}

static int ngb_relayed(int type)
{
    return type == BE_EFFECT || type == BE_EFFECT_STOP || type == BE_SOUND || type == BE_BOSS_HEALTH ||
           type == BE_CAMERA_RMH || type == BE_DEFEATED;
}

static BossEvent ngb_pend[NGA_PEND];
static int ngb_npend;

static int ngb_events(Boss *bo, BossEvent *out, int n, int max)
{
    if (ng_guest()) {
        int k = ngb_npend < max ? ngb_npend : max;
        if (out && k > 0) {
            memcpy(out, ngb_pend, (size_t)k * sizeof *out);
            memmove(ngb_pend, ngb_pend + k, (size_t)(ngb_npend - k) * sizeof *ngb_pend);
        }
        ngb_npend -= k;
        return out ? k : 0;
    }
    if (!ng_host() || !out || !g_ngv)
        return n;
    static uint8_t m[4000];
    NetBuf b = netbuf(m, sizeof m);
    int count = 0, at_count = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || b.len > 3300) {
            if (count) {
                m[at_count] = (uint8_t)count;
                ng_event(NGP_BOSS, m, b.len);
            }
            b = netbuf(m, sizeof m);
            count = 0;
            if (i == n)
                break;
        }
        const BossEvent *e = &out[i];
        if (!ngb_relayed(e->type))
            continue;
        if (!count) {
            net_w_u8(&b, NGA_EV_BOSS);
            net_w_u8(&b, (uint32_t)g_ngv->block);
            net_w_f64(&b, net_now());
            at_count = b.len;
            net_w_u8(&b, 0);
        }
        count++;
        net_w_u8(&b, (uint32_t)e->type);
        net_w_u16(&b, (uint32_t)(uint16_t)(int16_t)e->obj);
        net_w_u8(&b, (uint32_t)(uint8_t)(int8_t)e->monster);
        net_w_u32(&b, (uint32_t)e->id);
        net_w_u32(&b, (uint32_t)e->level);
        nga_w_text(&b, e->name);
        nga_w_text(&b, e->node);
        for (int k = 0; k < 3; k++) net_w_f32(&b, e->pos[k]);
        for (int k = 0; k < 3; k++) net_w_f32(&b, e->dir[k]);
        for (int k = 0; k < 3; k++) net_w_f32(&b, e->v[k]);
        net_w_f32(&b, e->amount);
        const BossPart *p = e->obj >= 0 ? boss_part(bo, e->obj) : NULL;
        net_w_u8(&b, (uint32_t)(p && p->alive));
        if (p && p->alive) {                         /* its part, for a guest that has not got it yet */
            nga_w_text(&b, p->name);
            nga_w_frame(&b, p->world);
        }
    }
    return n;
}

static void ngb_event(Viewer *v, NetBuf *b)
{
    int kind = (int)net_r_u8(b), block = (int)net_r_u8(b);
    if (b->err || block != v->block || kind != NGA_EV_BOSS || !v->boss)
        return;
    double t = net_r_f64(b);
    int count = (int)net_r_u8(b);
    for (int i = 0; i < count && !b->err; i++) {
        BossEvent e;
        memset(&e, 0, sizeof e);
        e.type = (int)net_r_u8(b);
        e.obj = (int16_t)net_r_u16(b);
        e.monster = (int8_t)net_r_u8(b);
        e.building = -1;
        e.id = (int)net_r_u32(b);
        e.level = (int)net_r_u32(b);
        e.name = nga_r_text(b);
        e.node = nga_r_text(b);
        for (int k = 0; k < 3; k++) e.pos[k] = net_r_f32(b);
        for (int k = 0; k < 3; k++) e.dir[k] = net_r_f32(b);
        for (int k = 0; k < 3; k++) e.v[k] = net_r_f32(b);
        e.amount = net_r_f32(b);
        if (net_r_u8(b)) {
            const char *name = nga_r_text(b);
            NgaFrame f;
            nga_r_frame(b, &f);
            BossPart *p = boss_net_slot(v->boss, e.obj);
            if (!b->err && name && e.obj >= 0 && e.obj < NGA_PARTS && (!p || !p->alive || p->name != name)) {
                p = boss_net_part(v->boss, e.obj, name, f.pos);
                if (p) {
                    nga_frame_world(&f, &f, 0, p->world);
                    ngb_born[e.obj] = t;
                    if (e.obj >= boss_count(v->boss))
                        boss_net_state(v->boss, e.obj + 1, boss_main(v->boss), boss_defeated(v->boss));
                }
            }
        }
        if (!b->err && ngb_npend < NGA_PEND)
            ngb_pend[ngb_npend++] = e;
    }
}

static void ngb_reset(Viewer *v)
{
    (void)v;
    ngb_npend = 0;
    memset(ngb_born, 0, sizeof ngb_born);
}

static void ngr_army_init(void)
{
    ng_pup[NGP_ARMY] = (NgPuppet){ "army", 20, nga_army_write, nga_army_apply, nga_army_event, nga_army_reset };
    ng_pup[NGP_BOSS] = (NgPuppet){ "bosses", 20, ngb_write, ngb_apply, ngb_event, ngb_reset };
    army_net_gate = nga_gate;
    army_net_events = nga_army_events;
    army_net_window_open = nga_window_open;
    boss_net_gate = nga_gate;
    boss_net_events = ngb_events;
}

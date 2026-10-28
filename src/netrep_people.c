/* netrep_people.c - online play: people (window occupants, walkers, held, thrown) and edibles (included by netgame.c; channels NGP_PEOPLE, NGP_EDIBLES).
 * The host's system runs as offline; the guest's leaves its logic out (net_guest_world() in its step in main.c) and
 * takes each unit from the host's snapshots (ng_pup[...].write / .apply) and its effects and sounds from the host's
 * events (ng_event / .event). See netgame.c "puppets".
 *
 * People (NGP_PEOPLE), the guest's list made the host's:
 *   snapshots  20 a second: every person not gone (its template and look by name, its phase, its play's time, its
 *              place and turn, its window), the script objects (bands, huts, the school's swarm), a flying hut's
 *              velocity, the challenge's counts and every building's open windows (the spawner opens them, outside
 *              building_net_gate). The guest's person `who` is the host's: one of another template there is made over
 *              (people_net_person), one the host has not is gone. Its own effects (the AmbientEffect, ChallengeEffect,
 *              IdleEffect, a kid's balloon: people.c's PE_FX_ON slots) are started and stopped by the snapshot's
 *              bits, so a person the guest first sees late still has them. One held in a monster's hand on the guest
 *              is posed by main.c's people_hold at the guest's own hand, not by the snapshot
 *   events     the people's sounds, one-off effects, banners and windows shutting (people_net_tap), and an eater's
 *              effects (a priest's lightning, the fart: people_net_eaten); the gameplay ones (the monster's knocks and
 *              HP, the grenadiers' weapons, the drops, impacts) are the host's alone
 * Edibles (NGP_EDIBLES): the slots are the same on both (made at the block's load); every grab the host makes
 *   (edible_grab, edible_net_gate) is made again on the guest as its event comes due, so the edible goes, its
 *   effects, sounds and banners play; its stats and the monster's reaction are not paid there (the snapshot's). */

#define NGR_PEOPLE 512                               /* persons a snapshot at most */
#define NGR_NAMES 64                                 /* templates and anims a snapshot at most */
#define NGR_WINDOWS 512                              /* open windows a snapshot at most */
enum { NGR_EV_PERSON, NGR_EV_EATEN };

static void ngr_w_str(NetBuf *b, const char *s)      /* 255: none */
{
    int n = s ? (int)strlen(s) : 255;
    if (s && n > 200)
        n = 200;
    net_w_u8(b, (uint32_t)n);
    if (s)
        net_w_bytes(b, s, n);
}

static const char *ngr_r_str(NetBuf *b, char *out, int cap)
{
    int n = (int)net_r_u8(b);
    if (n == 255)
        return NULL;
    char tmp[256];
    net_r_bytes(b, tmp, n);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, tmp, (size_t)n);
    out[n] = 0;
    return out;
}

/* ---- the snapshot as read */
typedef struct {
    int who, ti, ai, bits, fx;                       /* bits: phase (2), loop 4, reverse 8, in a window 16, done 32 */
    float t, len, pos[3], q[4];
    int b, s, c;
} NgrPerson;

typedef struct {
    int block, n, np, ntmpl, nanim;
    char tmpl[NGR_NAMES][96], anim[NGR_NAMES][32];
    NgrPerson p[NGR_PEOPLE];
    int nobj;
    struct { int flags, state, made; float pos[3]; } obj[16];
    int nhut;
    struct { int k; float vel[3]; } hut[8];
    int ngoals, collected[CHALLENGE_GOALS], met;
    int nwin;
    struct { int bi, o, column, lr, opens_h; float open, rate, rect[4]; } win[NGR_WINDOWS];
} NgrSnap;

static NgrSnap ngr_a, ngr_b;

/* a table of names: its index for s, added when new; -1 when full */
static int ngr_name(const char **tab, int *n, const char *s)
{
    for (int i = 0; i < *n; i++)
        if (!strcmp(tab[i], s))
            return i;
    if (*n >= NGR_NAMES)
        return -1;
    tab[*n] = s;
    return (*n)++;
}

/* rotation of a column-major 4 x 4 <-> quaternion (x, y, z, w) */
static void ngr_quat(const float m[16], float q[4])
{
    float t = m[0] + m[5] + m[10], s;
    if (t > 0) {
        s = sqrtf(t + 1) * 2;
        q[3] = s / 4; q[0] = (m[6] - m[9]) / s; q[1] = (m[8] - m[2]) / s; q[2] = (m[1] - m[4]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        s = sqrtf(1 + m[0] - m[5] - m[10]) * 2;
        q[3] = (m[6] - m[9]) / s; q[0] = s / 4; q[1] = (m[4] + m[1]) / s; q[2] = (m[8] + m[2]) / s;
    } else if (m[5] > m[10]) {
        s = sqrtf(1 + m[5] - m[0] - m[10]) * 2;
        q[3] = (m[8] - m[2]) / s; q[0] = (m[4] + m[1]) / s; q[1] = s / 4; q[2] = (m[9] + m[6]) / s;
    } else {
        s = sqrtf(1 + m[10] - m[0] - m[5]) * 2;
        q[3] = (m[1] - m[4]) / s; q[0] = (m[8] + m[2]) / s; q[1] = (m[9] + m[6]) / s; q[2] = s / 4;
    }
}

static void ngr_mat(const float q0[4], const float p[3], float m[16])
{
    float l = sqrtf(q0[0] * q0[0] + q0[1] * q0[1] + q0[2] * q0[2] + q0[3] * q0[3]);
    float x = 0, y = 0, z = 0, w = 1;
    if (l > 1e-6f)
        x = q0[0] / l, y = q0[1] / l, z = q0[2] / l, w = q0[3] / l;
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w);     m[2] = 2 * (x * z - y * w);
    m[4] = 2 * (x * y - z * w);     m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z + x * w);
    m[8] = 2 * (x * z + y * w);     m[9] = 2 * (y * z - x * w);     m[10] = 1 - 2 * (x * x + y * y);
    m[3] = m[7] = m[11] = 0;
    m[12] = p[0]; m[13] = p[1]; m[14] = p[2];
    m[15] = 1;
}

/* ---- the host: the snapshot */
static int ngr_people_write(Viewer *v, NetBuf *b)
{
    People *P = v->scene.folk;
    if (!P)
        return 0;
    const char *tn[NGR_NAMES], *an[NGR_NAMES];
    int nt = 0, na = 0, n = people_count(P);
    static int ti[NGR_PEOPLE], ai[NGR_PEOPLE], live[NGR_PEOPLE];
    int nl = 0;
    for (int who = 0; who < n && nl < NGR_PEOPLE; who++) {
        const Person *q = people_get(P, who);
        if (q->phase == PP_GONE || !q->tmpl.at || !q->name)
            continue;
        int t = ngr_name(tn, &nt, q->name), a = ngr_name(an, &na, q->anim);
        if (t < 0 || a < 0)
            continue;
        ti[nl] = t;
        ai[nl] = a;
        live[nl++] = who;
    }
    static double said;
    if (getenv("RAMPAGE_NET_TRACE") && net_now() - said > 5) {   /* what the host sends, for the guest's line */
        said = net_now();
        char line[1024];
        int len = 0;
        for (int i = 0; i < nl && len < (int)sizeof line - 40; i++) {
            const Person *q = people_get(P, live[i]);
            len += snprintf(line + len, sizeof line - (size_t)len, " %d:%d(%.0f,%.0f,%.0f)", live[i], q->phase,
                            q->pos[0], q->pos[1], q->pos[2]);
        }
        net_log("people at host time %.2f: %d of %d%s", net_now(), nl, n, len ? line : "");
    }
    net_w_u8(b, (uint32_t)v->block);
    net_w_u8(b, (uint32_t)nt);
    for (int i = 0; i < nt; i++)
        ngr_w_str(b, tn[i]);
    net_w_u8(b, (uint32_t)na);
    for (int i = 0; i < na; i++)
        ngr_w_str(b, an[i]);
    net_w_u16(b, (uint32_t)(n < 0xFFFF ? n : 0xFFFF));
    net_w_u16(b, (uint32_t)nl);
    for (int i = 0; i < nl; i++) {
        const Person *q = people_get(P, live[i]);
        int win = q->building >= 0 && q->building < 256 && q->storey >= 0 && q->column >= 0;
        int bits = (q->phase & 3) | (q->anim_loop ? 4 : 0) | (q->anim_reverse ? 8 : 0) | (win ? 16 : 0) |
                   (q->anim_done ? 32 : 0);
        float qt[4];
        ngr_quat(q->world, qt);
        net_w_u16(b, (uint32_t)live[i]);
        net_w_u8(b, (uint32_t)ti[i]);
        net_w_u8(b, (uint32_t)ai[i]);
        net_w_u8(b, (uint32_t)bits);
        net_w_u8(b, (uint32_t)(q->fx & 7));
        net_w_f32(b, q->anim_time);
        net_w_f32(b, q->anim_len);
        for (int k = 0; k < 3; k++)
            net_w_f32(b, q->world[12 + k]);
        for (int k = 0; k < 4; k++)
            net_w_u16(b, (uint32_t)(uint16_t)(int16_t)lrintf(qt[k] * 32767.0f));
        if (win) {
            net_w_u8(b, (uint32_t)q->building);
            net_w_u8(b, (uint32_t)q->storey);
            net_w_u8(b, (uint32_t)q->column);
        }
    }
    int no = people_net_objects(P);                  /* the script objects */
    net_w_u8(b, (uint32_t)no);
    for (int o = 0; o < no; o++) {
        float pos[3] = { 0, 0, 0 };
        int st = 2, made = people_object(P, o, pos), fl = people_net_object_flags(P, o, &st);
        net_w_u8(b, (uint32_t)(fl < 0 ? 0 : fl));
        net_w_u8(b, (uint32_t)st);
        net_w_u16(b, (uint32_t)made);
        for (int k = 0; k < 3; k++)
            net_w_f32(b, pos[k]);
    }
    int nh = 0;                                      /* the huts' flights */
    for (int k = 0; k < v->scene.nscripted && k < 8; k++)
        nh += v->scene.scripted[k].hut;
    net_w_u8(b, (uint32_t)nh);
    for (int k = 0; k < v->scene.nscripted && k < 8; k++)
        if (v->scene.scripted[k].hut) {
            net_w_u8(b, (uint32_t)k);
            for (int j = 0; j < 3; j++)
                net_w_f32(b, v->scene.scripted[k].vel[j]);
        }
    net_w_u8(b, (uint32_t)v->challenge.ngoals);      /* the challenge's counts */
    for (int g = 0; g < v->challenge.ngoals && g < CHALLENGE_GOALS; g++)
        net_w_u32(b, (uint32_t)v->challenge.goal[g].collected);
    net_w_u8(b, (uint32_t)(v->challenge.met + 1));
    int nw = 0;                                      /* the open windows */
    for (uint32_t i = 0; i < v->scene.nbuildings; i++)
        nw += v->scene.buildings[i].b.nwindows;
    nw = nw < NGR_WINDOWS ? nw : NGR_WINDOWS;
    net_w_u16(b, (uint32_t)nw);
    for (uint32_t i = 0, k = 0; i < v->scene.nbuildings; i++) {
        const Building *bb = &v->scene.buildings[i].b;
        for (int w = 0; w < bb->nwindows && (int)k < nw; w++, k++) {
            const BuildingWindow *bw = &bb->window[w];
            net_w_u16(b, i);
            net_w_u8(b, (uint32_t)bw->o);
            net_w_u8(b, (uint32_t)bw->column);
            net_w_u8(b, (uint32_t)(bw->lr | (bw->opens_h ? 2 : 0)));
            net_w_f32(b, bw->open);
            net_w_f32(b, bw->rate);
            net_w_f32(b, bw->x0);
            net_w_f32(b, bw->z0);
            net_w_f32(b, bw->w);
            net_w_f32(b, bw->h);
        }
    }
    return 1;
}

static int ngr_read(const NetSnapSample *s, NgrSnap *o)
{
    NetBuf b = netbuf((void *)s->data, s->len);
    o->block = (int)net_r_u8(&b);
    o->ntmpl = (int)net_r_u8(&b);
    for (int i = 0; i < o->ntmpl && i < NGR_NAMES; i++)
        if (!ngr_r_str(&b, o->tmpl[i], sizeof o->tmpl[i]))
            o->tmpl[i][0] = 0;
    o->nanim = (int)net_r_u8(&b);
    for (int i = 0; i < o->nanim && i < NGR_NAMES; i++)
        if (!ngr_r_str(&b, o->anim[i], sizeof o->anim[i]))
            o->anim[i][0] = 0;
    if (o->ntmpl > NGR_NAMES || o->nanim > NGR_NAMES)
        return 0;
    o->n = (int)net_r_u16(&b);
    o->np = (int)net_r_u16(&b);
    if (o->np > NGR_PEOPLE)
        return 0;
    for (int i = 0; i < o->np; i++) {
        NgrPerson *e = &o->p[i];
        e->who = (int)net_r_u16(&b);
        e->ti = (int)net_r_u8(&b);
        e->ai = (int)net_r_u8(&b);
        e->bits = (int)net_r_u8(&b);
        e->fx = (int)net_r_u8(&b);
        e->t = net_r_f32(&b);
        e->len = net_r_f32(&b);
        for (int k = 0; k < 3; k++)
            e->pos[k] = net_r_f32(&b);
        for (int k = 0; k < 4; k++)
            e->q[k] = (float)(int16_t)net_r_u16(&b) / 32767.0f;
        e->b = e->s = e->c = -1;
        if (e->bits & 16) {
            e->b = (int)net_r_u8(&b);
            e->s = (int)net_r_u8(&b);
            e->c = (int)net_r_u8(&b);
        }
        if (e->ti >= o->ntmpl || e->ai >= o->nanim || e->who >= o->n)
            return 0;
    }
    o->nobj = (int)net_r_u8(&b);
    if (o->nobj > 16)
        return 0;
    for (int i = 0; i < o->nobj; i++) {
        o->obj[i].flags = (int)net_r_u8(&b);
        o->obj[i].state = (int)net_r_u8(&b);
        o->obj[i].made = (int)net_r_u16(&b);
        for (int k = 0; k < 3; k++)
            o->obj[i].pos[k] = net_r_f32(&b);
    }
    o->nhut = (int)net_r_u8(&b);
    if (o->nhut > 8)
        return 0;
    for (int i = 0; i < o->nhut; i++) {
        o->hut[i].k = (int)net_r_u8(&b);
        for (int k = 0; k < 3; k++)
            o->hut[i].vel[k] = net_r_f32(&b);
    }
    o->ngoals = (int)net_r_u8(&b);
    for (int g = 0; g < o->ngoals && g < CHALLENGE_GOALS; g++)
        o->collected[g] = (int)net_r_u32(&b);
    o->met = (int)net_r_u8(&b) - 1;
    o->nwin = (int)net_r_u16(&b);
    if (o->nwin > NGR_WINDOWS)
        return 0;
    for (int i = 0; i < o->nwin; i++) {
        o->win[i].bi = (int)net_r_u16(&b);
        o->win[i].o = (int)net_r_u8(&b);
        o->win[i].column = (int)net_r_u8(&b);
        int lr = (int)net_r_u8(&b);
        o->win[i].lr = lr & 1;
        o->win[i].opens_h = (lr & 2) != 0;
        o->win[i].open = net_r_f32(&b);
        o->win[i].rate = net_r_f32(&b);
        float x0 = net_r_f32(&b), z0 = net_r_f32(&b), w = net_r_f32(&b), h = net_r_f32(&b);
        o->win[i].rect[0] = x0;
        o->win[i].rect[1] = z0;
        o->win[i].rect[2] = x0 + w;
        o->win[i].rect[3] = z0 + h;
    }
    return !b.err;
}

/* ---- the guest */
static struct { char name[96]; Group t; } ngr_tcache[NGR_NAMES];   /* templates by name, this block's */
static int ngr_ntcache;
static uint8_t *ngr_tried;                           /* each person's PE_FX_ON slots already started once */
static int ngr_ntried;

static Group ngr_template(Viewer *v, const char *name)
{
    for (int i = 0; i < ngr_ntcache; i++)
        if (!strcmp(ngr_tcache[i].name, name))
            return ngr_tcache[i].t;
    Group t = v->city ? city_template(v->city, name) : (Group){ 0 };
    if (ngr_ntcache < NGR_NAMES) {
        snprintf(ngr_tcache[ngr_ntcache].name, sizeof ngr_tcache[0].name, "%s", name);
        ngr_tcache[ngr_ntcache++].t = t;
    }
    return t;
}

static uint8_t *ngr_tried_of(int who)
{
    if (who < 0)
        return NULL;
    if (who >= ngr_ntried) {
        int n = who + 64;
        uint8_t *t = realloc(ngr_tried, (size_t)n);
        if (!t)
            return NULL;
        memset(t + ngr_ntried, 0, (size_t)(n - ngr_ntried));
        ngr_tried = t;
        ngr_ntried = n;
    }
    return &ngr_tried[who];
}

/* person who's effect slot: on when the host has it on (started once: an effect that has run out stays out, as the
 * host's does), off when the host's is */
static void ngr_fx(Viewer *v, int who, const Person *q, int want)
{
    uint8_t *tried = ngr_tried_of(who);
    for (int slot = PFX_AMBIENT; slot <= PFX_SCRIPT; slot++) {
        int bit = 1 << slot, k;
        for (k = 0; k < v->nppfx && !(v->ppfx[k].who == who && v->ppfx[k].slot == slot); k++)
            ;
        int have = k < v->nppfx;
        if (!(want & bit)) {
            if (tried)
                *tried &= (uint8_t)~bit;
            if (have) {
                if (v->fxw)
                    fx_stop(v->fxw, v->ppfx[k].fx);
                v->ppfx[k] = v->ppfx[--v->nppfx];
            }
            continue;
        }
        if (have || !tried || (*tried & bit) || !v->fxw || !q || !q->tmpl.at)
            continue;
        *tried |= (uint8_t)bit;
        const char *name = NULL;
        int follow = 1;
        float at[3] = { q->world[12], q->world[13], q->world[14] }, R[3][3];
        if (slot == PFX_AMBIENT)
            name = group_str(q->tmpl, "Effect|AmbientEffect");
        else if (slot == PFX_CHALLENGE)
            name = challenge_is_goal(&v->challenge, q->name) ? group_str(q->tmpl, "Effect|ChallengeEffect") : NULL;
        else if (q->kind == PK_STATIONARY) {         /* idleEffect.createAndStart(pos + 5 up) */
            name = group_str(q->tmpl, "Effect|IdleEffect");
            follow = 0;
            at[2] += 5;
        } else if (q->kind == PK_TOURIST)
            name = group_str(q->tmpl, "Effect|KidBalloonHoldingFX");
        if (!name)
            continue;
        for (int a = 0; a < 3; a++)
            for (int c = 0; c < 3; c++)
                R[a][c] = q->world[4 * a + c];
        int id = follow ? fx_spawn_frame(v->fxw, name, at, (const float (*)[3])R, 1) : fx_spawn(v->fxw, name, at, NULL, 1);
        if (id >= 0 && v->nppfx < 64) {
            v->ppfx[v->nppfx].fx = id;
            v->ppfx[v->nppfx].who = who;
            v->ppfx[v->nppfx].slot = slot;
            v->ppfx[v->nppfx].follow = follow;
            memset(v->ppfx[v->nppfx].off, 0, sizeof v->ppfx[v->nppfx].off);
            v->nppfx++;
        }
    }
}

static void ngr_gone(Viewer *v, int who)
{
    Person *q = people_net_person(v->scene.folk, who, (Group){ 0 });
    if (!q)
        return;
    if (getenv("RAMPAGE_NET_TRACE"))
        net_log("person %d (%s): phase %d -> gone", who, q->name ? q->name : "-", q->phase);
    q->phase = PP_GONE;
    q->building = -1;
    q->mv.active = 0;
    if (q->fx)
        ngr_fx(v, who, q, 0);
    q->fx = 0;
}

/* the open windows as the host's: the missing opened, the ones it has not shut */
static void ngr_windows(Viewer *v, const NgrSnap *s)
{
    for (uint32_t i = 0; i < v->scene.nbuildings; i++) {
        Building *b = &v->scene.buildings[i].b;
        for (int w = 0; w < b->nwindows; w++) {
            BuildingWindow *bw = &b->window[w];
            int k;
            for (k = 0; k < s->nwin; k++)
                if (s->win[k].bi == (int)i && s->win[k].o == bw->o && s->win[k].column == bw->column &&
                    s->win[k].lr == bw->lr)
                    break;
            if (k < s->nwin) {
                if (fabsf(bw->open - s->win[k].open) > 0.2f || (bw->rate > 0) != (s->win[k].rate > 0) ||
                    (bw->rate < 0) != (s->win[k].rate < 0)) {
                    bw->open = s->win[k].open;
                    bw->rate = s->win[k].rate;
                }
            } else if (bw->rate >= 0) {              /* shut: building_update takes it out */
                bw->rate = -4;
                if (bw->open <= 0)
                    bw->open = 0.001f;
            }
        }
    }
    for (int k = 0; k < s->nwin; k++) {
        if (s->win[k].bi < 0 || (uint32_t)s->win[k].bi >= v->scene.nbuildings)
            continue;
        Building *b = &v->scene.buildings[s->win[k].bi].b;
        int w;
        for (w = 0; w < b->nwindows && !(b->window[w].o == s->win[k].o && b->window[w].column == s->win[k].column &&
                                         b->window[w].lr == s->win[k].lr); w++)
            ;
        if (w < b->nwindows)
            continue;
        if (s->win[k].rate < 0 && s->win[k].open < 0.1f)
            continue;                                /* (all but shut) */
        if (building_window_open(b, s->win[k].o, s->win[k].column, s->win[k].lr, 1, 0, s->win[k].rect, s->win[k].opens_h) &&
            b->nwindows > 0 && b->window[b->nwindows - 1].o == s->win[k].o &&
            b->window[b->nwindows - 1].column == s->win[k].column && b->window[b->nwindows - 1].lr == s->win[k].lr) {
            b->window[b->nwindows - 1].open = s->win[k].open;
            b->window[b->nwindows - 1].rate = s->win[k].rate;
        }
    }
}

static void ngr_people_apply(Viewer *v, const NetSnapSample *sa, const NetSnapSample *sb, float alpha, int how)
{
    People *P = v->scene.folk;
    if (!P || !ngr_read(sb, &ngr_b) || ngr_b.block != v->block)
        return;
    int have_a = how == 2 && ngr_read(sa, &ngr_a) && ngr_a.block == ngr_b.block;
    Group tm[NGR_NAMES];
    for (int i = 0; i < ngr_b.ntmpl; i++)
        tm[i] = ngr_template(v, ngr_b.tmpl[i]);
    if (ngr_b.n > 0)
        people_net_person(P, ngr_b.n - 1, (Group){ 0 });
    int ia = 0, next = 0;
    for (int i = 0; i < ngr_b.np; i++) {
        const NgrPerson *e = &ngr_b.p[i];
        for (; next < e->who; next++)
            if (people_get(P, next)->phase != PP_GONE)
                ngr_gone(v, next);
        next = e->who + 1;
        Group t = tm[e->ti];
        if (!t.at)
            continue;
        const Person *was = people_get(P, e->who);
        int made_over = !was || was->tmpl.at != t.at || was->tmpl.book != t.book;
        if (made_over && was) {
            if (was->fx)
                ngr_fx(v, e->who, was, 0);
            scene_person_forget(&v->scene, e->who);   /* a person made over: its old looks not its */
        }
        Person *q = people_net_person(P, e->who, t);
        if (!q)
            continue;
        if (made_over) {
            uint8_t *tr = ngr_tried_of(e->who);
            if (tr)
                *tr = 0;
            q->phase = PP_GONE;                      /* (its first: no hold to blend from) */
        }
        const NgrPerson *ea = NULL;                  /* the same person in the sample before */
        while (have_a && ia < ngr_a.np && ngr_a.p[ia].who < e->who)
            ia++;
        if (have_a && ia < ngr_a.np && ngr_a.p[ia].who == e->who && ngr_a.p[ia].ti < ngr_a.ntmpl &&
            !strcmp(ngr_a.tmpl[ngr_a.p[ia].ti], ngr_b.tmpl[e->ti]))
            ea = &ngr_a.p[ia];
        /* the play */
        const char *anim = ngr_b.anim[e->ai];
        int loop = (e->bits & 4) != 0, reverse = (e->bits & 8) != 0;
        float t1 = e->t, len = e->len;
        if (ea && !strcmp(ngr_a.anim[ea->ai], anim) && !reverse) {
            if (t1 >= ea->t)
                t1 = ea->t + (t1 - ea->t) * alpha;
            else if (loop && len > 0)
                t1 = fmodf(ea->t + (t1 + len - ea->t) * alpha, len);
        } else if (how == 1 && !reverse) {           /* on past the newest, alpha seconds */
            if (loop && len > 0)
                t1 = fmodf(t1 + alpha, len);
            else if (!(e->bits & 32))
                t1 = fminf(len, t1 + alpha);
        }
        snprintf(q->anim, sizeof q->anim, "%s", anim);
        q->anim_time = t1;
        q->anim_len = len;
        q->anim_loop = loop;
        q->anim_reverse = reverse;
        q->anim_done = (e->bits & 32) != 0;
        /* the place: between the samples, unless one of the guest's monsters holds it (people_hold's) */
        int ph = e->bits & 3, held_here = 0;
        if (ph != q->phase && q->phase != PP_GONE && getenv("RAMPAGE_NET_TRACE"))
            net_log("person %d (%s): phase %d -> %d at %.1f %.1f %.1f", e->who, q->name, q->phase, ph, e->pos[0],
                    e->pos[1], e->pos[2]);
        for (int k = 0; k < v->nmon && ph == PP_HELD; k++)
            held_here |= v->mon[k].held == e->who;
        if (ph == PP_HELD && q->phase != PP_HELD && q->phase != PP_GONE)
            people_net_held(P, e->who, q->building >= 0 ? 0.25f : 0.1f);
        if (!(held_here && q->phase == PP_HELD)) {
            float pos[3], qt[4];
            memcpy(pos, e->pos, sizeof pos);
            memcpy(qt, e->q, sizeof qt);
            if (ea) {
                float d = 0;
                for (int k = 0; k < 4; k++)
                    d += ea->q[k] * e->q[k];
                for (int k = 0; k < 3; k++)
                    pos[k] = ea->pos[k] + (e->pos[k] - ea->pos[k]) * alpha;
                for (int k = 0; k < 4; k++)        /* (the short way: normalised in ngr_mat) */
                    qt[k] = ea->q[k] * (1 - alpha) + (d < 0 ? -e->q[k] : e->q[k]) * alpha;
            }
            ngr_mat(qt, pos, q->world);
            if (ph == PP_HELD && q->phase != PP_HELD)
                people_net_held(P, e->who, 0);
            q->phase = ph;
            if (ph == PP_HELD)
                q->flags |= 0x20000u;
            else
                q->flags &= ~0x20000u;
        }
        memcpy(q->pos, &q->world[12], sizeof q->pos);
        q->heading = atan2f(-q->world[4], q->world[5]) * 57.2957795f;
        q->building = e->b;
        q->storey = e->s;
        q->column = e->c;
        q->mv.active = 0;
        if (q->phase == PP_FLYING || q->phase == PP_HELD) {   /* driven here: the frame's collisions move it not */
            collide_mover_kinematic(&q->cm, group_num(t, "Mass", 1), group_num(t, "Restitution", 0.8f),
                                    group_num(t, "SlidingFriction", 0.4f));
            collide_matrix_from16(q->cmat, q->world);
            memset(q->vel, 0, sizeof q->vel);
        }
        q->fx = e->fx;
        ngr_fx(v, e->who, q, e->fx);
    }
    for (; next < people_count(P); next++)
        if (people_get(P, next)->phase != PP_GONE)
            ngr_gone(v, next);
    for (int o = 0; o < ngr_b.nobj; o++)             /* the script objects */
        people_net_object(P, o, ngr_b.obj[o].flags & 1, (ngr_b.obj[o].flags & 2) != 0,
                          ngr_b.obj[o].made ? ngr_b.obj[o].state : 2, ngr_b.obj[o].made, ngr_b.obj[o].pos);
    for (int i = 0; i < ngr_b.nhut; i++) {           /* a hut flying off: its velocity, before its mover starts */
        int k = ngr_b.hut[i].k;
        if (k < v->scene.nscripted && v->scene.scripted[k].hut && !v->scene.scripted[k].cm_on)
            memcpy(v->scene.scripted[k].vel, ngr_b.hut[i].vel, sizeof v->scene.scripted[k].vel);
    }
    for (int g = 0; g < ngr_b.ngoals && g < v->challenge.ngoals && g < CHALLENGE_GOALS; g++)
        v->challenge.goal[g].collected = ngr_b.collected[g];
    if (ngr_b.ngoals == v->challenge.ngoals)
        v->challenge.met = ngr_b.met;
    ngr_windows(v, &ngr_b);
    static double said;
    if (getenv("RAMPAGE_NET_TRACE") && net_now() - said > 5) {   /* what the guest shows: who, phase, place */
        said = net_now();
        char line[1024];
        int len = 0, live = 0;
        for (int who = 0; who < people_count(P); who++) {
            const Person *q = people_get(P, who);
            if (q->phase == PP_GONE)
                continue;
            live++;
            if (len < (int)sizeof line - 40)
                len += snprintf(line + len, sizeof line - (size_t)len, " %d:%d(%.0f,%.0f,%.0f)", who, q->phase,
                                q->pos[0], q->pos[1], q->pos[2]);
        }
        net_log("people at host time %.2f: %d of %d%s", sb->t, live, people_count(P), line);
    }
}

/* ---- the events */
static void ngr_people_tap(People *p, const PeopleEvent *ev, int n)
{
    (void)p;
    if (!ng_host() || !g_ngv)
        return;
    for (int i = 0; i < n; i++) {
        const PeopleEvent *e = &ev[i];
        if (e->type != PE_SOUND && e->type != PE_EFFECT && e->type != PE_WINDOW_CLOSE && e->type != PE_MESSAGE)
            continue;                                /* (the monster's, the army's, the impacts: the host's; the
                                                      * person's own effects: the snapshot's bits) */
        uint8_t m[600];
        NetBuf b = netbuf(m, sizeof m);
        net_w_u8(&b, NGR_EV_PERSON);
        net_w_u8(&b, (uint32_t)g_ngv->block);
        net_w_u8(&b, (uint32_t)e->type);
        net_w_u16(&b, (uint32_t)(uint16_t)(int16_t)e->who);
        net_w_u8(&b, (uint32_t)(e->on & 0xFF));
        for (int k = 0; k < 3; k++)
            net_w_f32(&b, e->v[k]);
        net_w_f32(&b, e->amount);
        net_w_u16(&b, (uint32_t)(uint16_t)(int16_t)e->object);
        ngr_w_str(&b, e->name);
        ngr_w_str(&b, e->node);
        if (!b.err)
            ng_event(NGP_PEOPLE, m, b.len);
    }
}

static void ngr_people_eaten(People *p, int who, int monster, const PeopleEaten *e)
{
    (void)p;
    (void)who;
    if (!ng_host() || !g_ngv || (!e->eat_fx && !e->fart))
        return;
    uint8_t m[300];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NGR_EV_EATEN);
    net_w_u8(&b, (uint32_t)g_ngv->block);
    net_w_u8(&b, (uint32_t)monster);
    net_w_u8(&b, (uint32_t)(e->fart != 0));
    ngr_w_str(&b, e->eat_fx);
    ngr_w_str(&b, e->eat_fx ? e->eat_node : NULL);
    if (!b.err)
        ng_event(NGP_PEOPLE, m, b.len);
}

static void ngr_people_event(Viewer *v, NetBuf *b)
{
    int kind = (int)net_r_u8(b), block = (int)net_r_u8(b);
    if (b->err || block != v->block || !v->scene.folk)
        return;
    char name[256], node[256];
    if (kind == NGR_EV_PERSON) {
        PeopleEvent e;
        memset(&e, 0, sizeof e);
        e.type = (int)net_r_u8(b);
        e.who = (int16_t)net_r_u16(b);
        e.on = (int)net_r_u8(b);
        for (int k = 0; k < 3; k++)
            e.v[k] = net_r_f32(b);
        e.amount = net_r_f32(b);
        e.object = (int16_t)net_r_u16(b);
        e.name = ngr_r_str(b, name, sizeof name);
        e.node = ngr_r_str(b, node, sizeof node);
        if (b->err)
            return;
        if (e.who >= 0)                              /* (its snapshot may come after it) */
            people_net_person(v->scene.folk, e.who, (Group){ 0 });
        people_net_event(v->scene.folk, &e);
    } else if (kind == NGR_EV_EATEN) {
        int monster = (int)net_r_u8(b), fart = (int)net_r_u8(b);
        const char *fx = ngr_r_str(b, name, sizeof name), *at = ngr_r_str(b, node, sizeof node);
        if (b->err || monster >= v->nmon)
            return;
        Mon *mo = &v->mon[monster];
        if (fart)
            mon_fart_later(mo, 2.0f);
        if (fx && at)                                /* the lightning on the priest's eater */
            mon_fx_named(mo, fx, at);
    }
}

static void ngr_people_reset(Viewer *v)
{
    (void)v;
    ngr_ntcache = 0;
    if (ngr_tried)
        memset(ngr_tried, 0, (size_t)ngr_ntried);
}

static int ngr_people_gate(void) { return ng_guest(); }

/* ---- edibles: the host's grabs, made again on the guest */
static int ngr_edible_replaying;

static int ngr_edible_gate(Edibles *e, int building, int storey, int column, int monster, const char *as_type, int eat,
                           const char *monster_move)
{
    (void)e;
    if (ng_guest())
        return !ngr_edible_replaying;
    if (!ng_host() || !g_ngv)
        return 0;
    uint8_t m[600];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, (uint32_t)g_ngv->block);
    net_w_u16(&b, (uint32_t)building);
    net_w_u8(&b, (uint32_t)storey);
    net_w_u8(&b, (uint32_t)column);
    net_w_u8(&b, (uint32_t)monster);
    net_w_u8(&b, (uint32_t)(eat != 0));
    ngr_w_str(&b, as_type);
    ngr_w_str(&b, monster_move);
    if (!b.err)
        ng_event(NGP_EDIBLES, m, b.len);
    net_log("edibles: monster %d took the one at building %d storey %d column %d", monster + 1, building, storey, column);
    return 0;
}

/* the slots, a few times a second (4 at 20 updates a second, 12 at 60): each one's kind and whether its edible is out
 * in the window (and its age there). The guest makes none appear by itself (edible_net_no_spawn): what the host's
 * cells and floors showed, its own deal of a new block differing (the world's random draws are not the same there) */
static int ngr_edible_write(Viewer *v, NetBuf *b)
{
    net_w_u8(b, (uint32_t)v->block);
    ng_edibles_w(v, b);
    /* the cryotube's reveal (reveal.c): the monster it unlocks, its tubes placed, the reveals so far. The guest's own
     * profile picked its own at the block's load; the host's is the one its tube shows */
    net_w_u8(b, (uint32_t)(uint8_t)(int8_t)v->reveal.monster);
    net_w_u8(b, (uint32_t)(uint8_t)(int8_t)v->reveal.placed);
    net_w_u8(b, (uint32_t)(uint8_t)(int8_t)v->reveal.revealed);
    net_w_u8(b, (uint32_t)(uint8_t)(int8_t)v->reveal.count);
    return v->edibles && edible_slots(v->edibles) > 0;
}

static void ngr_edible_apply(Viewer *v, const NetSnapSample *a, const NetSnapSample *s, float alpha, int how)
{
    (void)a;
    (void)alpha;
    (void)how;
    static double done;                              /* the sample last made (each one once) */
    if (!v->edibles || s->t == done)
        return;
    done = s->t;
    NetBuf b = netbuf((void *)s->data, s->len);
    int block = (int)net_r_u8(&b);
    if (b.err || block != v->block)
        return;
    ng_edibles_r(v, &b);
    int monster = (int8_t)net_r_u8(&b), placed = (int8_t)net_r_u8(&b), revealed = (int8_t)net_r_u8(&b);
    int count = (int8_t)net_r_u8(&b);
    /* the host's reveal, while this side is at the same point of it (a tube broken here first, from the host's
     * building change, is not put back by an older snapshot; none here, the guest's monster unlocked already: the
     * host's) */
    static int said = -2;                            /* (the log: a reveal here, by this side's tube) */
    if (v->reveal.revealed != said && said != -2 && v->reveal.monster >= 0 && v->reveal.monster < v->roster.n)
        net_log("edibles: the cryotube revealed %s", v->roster.name[v->reveal.monster]);
    said = v->reveal.revealed;
    if (!b.err && v->reveal.revealed < 0 && revealed >= 0)
        v->reveal.revealed = revealed;
    if (!b.err && revealed == v->reveal.revealed && monster < v->roster.n &&
        (v->reveal.monster != monster || v->reveal.placed != placed || v->reveal.count != count)) {
        v->reveal.monster = monster;
        v->reveal.placed = placed;
        v->reveal.count = count;
    }
}
static void ngr_edible_event(Viewer *v, NetBuf *b)
{
    char as[256], move[256];
    int block = (int)net_r_u8(b), building = (int)net_r_u16(b), storey = (int)net_r_u8(b), column = (int)net_r_u8(b);
    int monster = (int)net_r_u8(b), eat = (int)net_r_u8(b);
    const char *as_type = ngr_r_str(b, as, sizeof as), *mm = ngr_r_str(b, move, sizeof move);
    if (b->err || block != v->block || !v->edibles)
        return;
    ngr_edible_replaying = 1;
    int r = edible_grab(v->edibles, building, storey, column, monster, as_type, eat, mm);
    ngr_edible_replaying = 0;
    net_log("edibles: the host's grab at building %d storey %d column %d %s", building, storey, column,
            r ? "made" : "not made here");
}

/* ---- the challenge's upgrade (NGP_CHALLENGE): the MonsterUpgrade object is the host's. Its state goes in a small
 * snapshot (the guest's effect follows it: ng_challenge_put); its taking (the banner, player 1's move) and its last
 * banner go as events. The guest's own touch and tick are off (main.c, net_guest_world) */
enum { NGC_TAKEN, NGC_BANNER };
static double ngr_chal_ev_at = -1e30;                /* the guest: the host time shown when an event last put it (an
                                                      * older snapshot is not put over it) */

static void ngr_challenge_w(Viewer *v, NetBuf *b)
{
    const Challenge *c = &v->challenge;
    net_w_u8(b, (uint32_t)v->block);
    net_w_u8(b, (uint32_t)c->up_state);
    net_w_u8(b, (uint32_t)c->up_made);
    net_w_f32(b, c->up_delay);
    net_w_f32(b, c->up_pulse);
    net_w_u32(b, c->upgrade);
    net_w_u32(b, c->up_taken);
}

static int ngr_challenge_r(Viewer *v, NetBuf *b)    /* 1: for this block, put */
{
    int block = (int)net_r_u8(b), st = (int)net_r_u8(b), made = (int)net_r_u8(b);
    float delay = net_r_f32(b), pulse = net_r_f32(b);
    uint32_t up = net_r_u32(b), taken = net_r_u32(b);
    if (b->err || block != v->block)
        return 0;
    ng_challenge_put(v, st, made, delay, pulse, up, taken);
    return 1;
}

static int ngr_challenge_write(Viewer *v, NetBuf *b)
{
    ngr_challenge_w(v, b);
    return 1;
}

static void ngr_challenge_apply(Viewer *v, const NetSnapSample *a, const NetSnapSample *s, float alpha, int how)
{
    (void)alpha;
    const NetSnapSample *at = how == 2 ? a : s;      /* (the one shown, not the next: its event comes at its time) */
    static double done;
    if (at->t == done || at->t <= ngr_chal_ev_at)
        return;
    done = at->t;
    NetBuf b = netbuf((void *)at->data, at->len);
    ngr_challenge_r(v, &b);
}

static void ngr_challenge_host(Viewer *v)           /* the host, every frame: the upgrade taken, its last banner */
{
    static int was_state = -1, was_block = -1;
    const Challenge *c = &v->challenge;
    int ev = -1;
    if (v->block == was_block && was_state == CU_WAITING && c->up_state == CU_TAKEN)
        ev = NGC_TAKEN;
    else if (v->block == was_block && was_state == CU_TAKEN && c->up_state == CU_NONE)
        ev = NGC_BANNER;
    was_state = c->up_state;
    was_block = v->block;
    if (ev < 0)
        return;
    uint8_t m[64];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, (uint32_t)ev);
    ngr_challenge_w(v, &b);
    ng_event(NGP_CHALLENGE, m, b.len);
    net_log("challenge: the upgrade %s (%s)", ev == NGC_TAKEN ? "taken" : "banner", challenge_upgrade_move(c->up_taken));
}

static void ngr_challenge_event(Viewer *v, NetBuf *b)
{
    int ev = (int)net_r_u8(b);
    if (b->err || !ngr_challenge_r(v, b))
        return;
    const NetSnapBuf *mb = net_snaps();
    ngr_chal_ev_at = mb->have_offset ? net_now() - mb->offset - ng_delay() : 1e30;
    const Challenge *c = &v->challenge;
    net_log("challenge: the host's upgrade %s (%s)", ev == NGC_TAKEN ? "taken" : "banner",
            challenge_upgrade_move(c->up_taken));
    if (ev == NGC_TAKEN) {                           /* as upgrade_touch: the banner, player 1's move */
        if (v->hud) {
            hud_hide_challenge_completion(v->hud);
            hud_banner_text(v->hud, c->up.text, c->up.colour, 1, "v_upgrade");
        }
        Mon *mo = &v->mon[0];
        if (mo->monster >= 0) {
            mo->power.abilities |= c->up_taken;
            v->upgrades[mo->monster & 63] |= c->up_taken;
        }
    } else if (ev == NGC_BANNER && v->hud) {         /* as its CUE_BANNER */
        static const float green[4] = { 0, 1, 0, 1 };
        hud_banner_text2(v->hud, challenge_upgrade_key(c->up_taken), challenge_upgrade_inst(c->up_taken), green, 1, 3, "");
    }
}

static void ngr_people_init(void)
{
    ng_pup[NGP_CHALLENGE] = (NgPuppet){ "challenge", 2, ngr_challenge_write, ngr_challenge_apply, ngr_challenge_event,
                                        NULL };
    ng_pup[NGP_PEOPLE] = (NgPuppet){ "people", 20, ngr_people_write, ngr_people_apply, ngr_people_event, ngr_people_reset };
    ng_pup[NGP_EDIBLES] = (NgPuppet){ "edibles", 4, ngr_edible_write, ngr_edible_apply, ngr_edible_event, NULL };
    people_net_gate = ngr_people_gate;
    people_net_tap = ngr_people_tap;
    people_net_eaten = ngr_people_eaten;
    edible_net_gate = ngr_edible_gate;
}

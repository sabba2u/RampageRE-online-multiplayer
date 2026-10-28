/* netrep_traffic.c - online play: vehicles, street objects, rail, parade (included by netgame.c; channels NGP_VEHICLES, NGP_STREET, NGP_RAIL, NGP_PARADE).
 * The host's system runs as offline; the guest's leaves its logic out (net_guest_world() in its step in main.c) and
 * takes each unit from the host's snapshots (ng_pup[...].write / .apply) and its effects and sounds from the host's
 * events (ng_event / .event). See netgame.c "puppets".
 *
 * The snapshots: the block, the host's count of objects, a table of the names they use (templates, shapes, clips:
 * the same strings on both, each sent once a snapshot and kept on the guest for good), then each object that is there
 * (vehicles, rail, parade: the ones not gone; street objects: every one, the matrix only for those that moved). A
 * matrix is its 3 x 3 in sixteenths of a thousandth (int16, up to 8 long) and its place in floats. A slot used again
 * (vehicles and parade by the REMOVED event's count, rail by its uid) is a new object: not blended from the last.
 * The guest's object takes the host's matrix between the two samples, unless one of the guest's monsters holds it:
 * then the step's *_hold puts it in that hand, as the host's.
 * The events: the effects and sounds only (the module's own event, its strings by name); the step's switch on the
 * guest plays them as the host's did. What they do to the monsters and the score stays the host's. */

#define NGT_STR_NONE 255                             /* a name not in the table (NULL, or the table full) */

/* ---- the guest's names: every string the host sent, kept for good (the drawing and the sounds keep the pointers) */
static const char *ngt_intern(const char *s, int len)
{
    static char *tab[4096];
    if (len <= 0)
        return NULL;
    uint32_t h = net_hash(s, len, 2166136261u);
    for (int k = 0; k < 4096; k++) {
        int j = (int)((h + (uint32_t)k) & 4095);
        if (!tab[j]) {
            if (!(tab[j] = malloc((size_t)len + 1)))
                return NULL;
            memcpy(tab[j], s, (size_t)len);
            tab[j][len] = 0;
            return tab[j];
        }
        if (!strncmp(tab[j], s, (size_t)len) && !tab[j][len])
            return tab[j];
    }
    return NULL;
}

static void ngt_w_str(NetBuf *b, const char *s)      /* a length (255 none) and the bytes */
{
    int n = s ? (int)strlen(s) : 0;
    if (!s || n > 254) {
        net_w_u8(b, 255);
        return;
    }
    net_w_u8(b, (uint32_t)n);
    net_w_bytes(b, s, n);
}

static const char *ngt_r_str(NetBuf *b)
{
    int n = (int)net_r_u8(b);
    if (n == 255 || b->err)
        return NULL;
    char t[256];
    net_r_bytes(b, t, n);
    return b->err ? NULL : n ? ngt_intern(t, n) : "";
}

/* a snapshot's table of names */
typedef struct { const char *s[NGT_STR_NONE]; int n; } NgtStrs;

static int ngt_str(NgtStrs *t, const char *s)
{
    if (!s)
        return NGT_STR_NONE;
    for (int k = 0; k < t->n; k++)
        if (t->s[k] == s || !strcmp(t->s[k], s))
            return k;
    if (t->n >= NGT_STR_NONE)
        return NGT_STR_NONE;
    t->s[t->n] = s;
    return t->n++;
}

static void ngt_w_strs(NetBuf *b, const NgtStrs *t)
{
    net_w_u8(b, (uint32_t)t->n);
    for (int k = 0; k < t->n; k++)
        ngt_w_str(b, t->s[k]);
}

/* ---- a matrix: its 3 x 3 (column-major, as the modules') and its place */
static void ngt_w_mat(NetBuf *b, const float m[16])
{
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) {
            float x = m[4 * c + r] * 4096.0f;
            x = x > 32767 ? 32767 : x < -32767 ? -32767 : x;
            net_w_u16(b, (uint32_t)(uint16_t)(int16_t)lrintf(x));
        }
    for (int r = 0; r < 3; r++)
        net_w_f32(b, m[12 + r]);
}

static void ngt_r_mat(NetBuf *b, float m[16])
{
    for (int c = 0; c < 3; c++) {
        for (int r = 0; r < 3; r++)
            m[4 * c + r] = (float)(int16_t)net_r_u16(b) / 4096.0f;
        m[4 * c + 3] = 0;
    }
    for (int r = 0; r < 3; r++)
        m[12 + r] = net_r_f32(b);
    m[15] = 1;
}

/* ---- a snapshot as the guest reads it */
enum { NGT_ALIVE = 1, NGT_HELD = 2, NGT_MOVED = 4, NGT_HIDDEN = 8, NGT_PUSH = 16, NGT_PICKUP = 32,
       NGT_MADE = 64 };                              /* (street: one a script made: its template and matrix) */
typedef struct {
    int i, phase, kind, flags;
    uint32_t gen, oflags;
    const char *name, *shape, *clip;
    float m[16];
    float x[4];                                      /* vehicles: the cannon's yaw, pitch; parade: scale, anim_time,
                                                      * anim_len, anim_loop */
} NgtObj;
#define NGT_MAX 256
typedef struct { double t; int ok, block, n, count; NgtObj o[NGT_MAX]; } NgtSnap;

enum { NGT_VEH, NGT_RAIL, NGT_PARADE, NGT_STREET, NGT_N };
static const int NGT_CH[NGT_N] = { NGP_VEHICLES, NGP_RAIL, NGP_PARADE, NGP_STREET };

static NgtSnap ngt_cache[NGT_N][2];                  /* the last two read (the apply's a and b, most frames the same) */
static uint32_t ngt_host_gen[NGT_N][NGT_MAX];        /* host: each slot's REMOVED events */
static int64_t ngt_guest_gen[NGT_N][NGT_MAX];        /* guest: the gen each slot shows (-1 none) */

static void ngt_read_obj(NetBuf *b, int sys, const NgtStrs *t, NgtObj *o);

/* sample s read (or as read before); keep: the one not to read over */
static const NgtSnap *ngt_read(int sys, const NetSnapSample *s, const NgtSnap *keep)
{
    if (!s)
        return NULL;
    for (int k = 0; k < 2; k++)
        if (ngt_cache[sys][k].ok && ngt_cache[sys][k].t == s->t)
            return &ngt_cache[sys][k];
    NgtSnap *o = &ngt_cache[sys][0];
    if (o == keep || (o->ok && ngt_cache[sys][1].ok && ngt_cache[sys][1].t < o->t && &ngt_cache[sys][1] != keep) ||
        (o->ok && !ngt_cache[sys][1].ok))
        o = &ngt_cache[sys][1];
    NetBuf b = netbuf((void *)s->data, s->len);
    static NgtStrs t;
    o->ok = 0;
    o->t = s->t;
    o->block = (int)net_r_u8(&b);
    o->n = (int)net_r_u16(&b);
    t.n = (int)net_r_u8(&b);
    for (int k = 0; k < t.n; k++)
        t.s[k] = ngt_r_str(&b);
    o->count = (int)net_r_u16(&b);
    if (o->count > NGT_MAX || o->n > NGT_MAX)
        return NULL;
    for (int k = 0; k < o->count && !b.err; k++)
        ngt_read_obj(&b, sys, &t, &o->o[k]);
    o->ok = !b.err;
    return o->ok ? o : NULL;
}

static const char *ngt_name(const NgtStrs *t, int k) { return k < t->n ? t->s[k] : NULL; }

static void ngt_read_obj(NetBuf *b, int sys, const NgtStrs *t, NgtObj *o)
{
    memset(o, 0, sizeof *o);
    o->i = (int)net_r_u16(b);
    o->flags = (int)net_r_u8(b);
    if (sys == NGT_STREET) {
        o->shape = ngt_name(t, (int)net_r_u8(b));
        if (o->flags & NGT_MADE)
            o->name = ngt_name(t, (int)net_r_u8(b));
        if (o->flags & (NGT_MOVED | NGT_MADE))
            ngt_r_mat(b, o->m);
        return;
    }
    o->phase = (int)net_r_u8(b);
    o->kind = (int)net_r_u8(b);
    o->gen = net_r_u32(b);
    o->oflags = net_r_u32(b);
    o->name = ngt_name(t, (int)net_r_u8(b));
    o->shape = ngt_name(t, (int)net_r_u8(b));
    ngt_r_mat(b, o->m);
    if (sys == NGT_VEH) {
        o->x[0] = net_r_f32(b);
        o->x[1] = net_r_f32(b);
    } else if (sys == NGT_PARADE) {
        o->clip = ngt_name(t, (int)net_r_u8(b));
        for (int k = 0; k < 4; k++)
            o->x[k] = net_r_f32(b);
    }
}

/* the host's: a snapshot's head, names and objects (the objects written twice: once for the names, then out) */
typedef struct { int i, phase, kind, flags; uint32_t gen, oflags; const char *name, *shape, *clip; const float *m;
                 float x[4]; } NgtOut;

static int ngt_write(Viewer *v, NetBuf *b, int sys, int n, const NgtOut *o, int count)
{
    static NgtStrs t;
    t.n = 0;
    for (int k = 0; k < count; k++) {
        ngt_str(&t, o[k].name);
        ngt_str(&t, o[k].shape);
        ngt_str(&t, o[k].clip);
    }
    net_w_u8(b, (uint32_t)v->block);
    net_w_u16(b, (uint32_t)n);
    ngt_w_strs(b, &t);
    net_w_u16(b, (uint32_t)count);
    for (int k = 0; k < count; k++) {
        const NgtOut *e = &o[k];
        net_w_u16(b, (uint32_t)e->i);
        net_w_u8(b, (uint32_t)e->flags);
        if (sys == NGT_STREET) {
            net_w_u8(b, (uint32_t)ngt_str(&t, e->shape));
            if (e->flags & NGT_MADE)
                net_w_u8(b, (uint32_t)ngt_str(&t, e->name));
            if (e->flags & (NGT_MOVED | NGT_MADE))
                ngt_w_mat(b, e->m);
            continue;
        }
        net_w_u8(b, (uint32_t)e->phase);
        net_w_u8(b, (uint32_t)e->kind);
        net_w_u32(b, e->gen);
        net_w_u32(b, e->oflags);
        net_w_u8(b, (uint32_t)ngt_str(&t, e->name));
        net_w_u8(b, (uint32_t)ngt_str(&t, e->shape));
        ngt_w_mat(b, e->m);
        if (sys == NGT_VEH) {
            net_w_f32(b, e->x[0]);
            net_w_f32(b, e->x[1]);
        } else if (sys == NGT_PARADE) {
            net_w_u8(b, (uint32_t)ngt_str(&t, e->clip));
            for (int j = 0; j < 4; j++)
                net_w_f32(b, e->x[j]);
        }
    }
    return 1;
}

/* the guest: the samples of a system about now, read; NULL when none is for this block */
static const NgtSnap *ngt_pair(Viewer *v, int sys, const NetSnapSample *a, const NetSnapSample *b, int how,
                               const NgtSnap **pa)
{
    const NgtSnap *sb = ngt_read(sys, b, NULL);
    if (!sb || sb->block != v->block)
        return NULL;
    const NgtSnap *sa = how == 2 ? ngt_read(sys, a, sb) : NULL;   /* (not over b's) */
    if (sa && (sa->block != v->block || sa == sb))
        sa = NULL;
    *pa = sa;
    return sb;
}

/* object i of a sample (the objects go by index), NULL not there; *at walks on */
static const NgtObj *ngt_find(const NgtSnap *s, int i, int *at)
{
    if (!s)
        return NULL;
    while (*at < s->count && s->o[*at].i < i)
        (*at)++;
    return *at < s->count && s->o[*at].i == i ? &s->o[*at] : NULL;
}

/* where the guest draws it: between a and b by alpha when both are the same object, else b's */
static void ngt_place(const NgtObj *oa, const NgtObj *ob, float alpha, float out[16])
{
    if (oa && oa->gen == ob->gen && alpha > 0 && alpha < 1) {
        float d[3] = { ob->m[12] - oa->m[12], ob->m[13] - oa->m[13], ob->m[14] - oa->m[14] };
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 20 * 20) {   /* (a jump: at b's) */
            collide_matrix_between(out, oa->m, ob->m, alpha);
            return;
        }
    }
    memcpy(out, ob->m, 16 * sizeof *out);
}

/* the guest: does one of its monsters hold object i of that kind (then its hand places it) */
static int ngt_held_here(const Viewer *v, int sys, int i)
{
    for (int k = 0; k < v->nmon; k++) {
        const Mon *mo = &v->mon[k];
        int h = sys == NGT_VEH ? mo->held_vehicle : sys == NGT_RAIL ? mo->held_rail : sys == NGT_PARADE ?
                mo->held_parade : mo->held_sobj;
        if (h == i)
            return 1;
    }
    return 0;
}

/* ---- the events: a module's event as bytes (the fields the four share), and back */
typedef struct { int type, id, obj, monster, level, node_id; const char *name, *node; float pos[3], dir[3], off[3], amount; }
    NgtEv;

static void ngt_send(int ch, const NgtEv *e)
{
    uint8_t m[640];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, (uint32_t)e->type);
    net_w_u32(&b, (uint32_t)e->id);
    net_w_u16(&b, (uint32_t)(uint16_t)(int16_t)e->obj);
    net_w_u8(&b, (uint32_t)(uint8_t)(int8_t)e->monster);
    net_w_u32(&b, (uint32_t)e->level);
    net_w_u32(&b, (uint32_t)e->node_id);
    ngt_w_str(&b, e->name);
    ngt_w_str(&b, e->node);
    for (int k = 0; k < 3; k++) net_w_f32(&b, e->pos[k]);
    for (int k = 0; k < 3; k++) net_w_f32(&b, e->dir[k]);
    for (int k = 0; k < 3; k++) net_w_f32(&b, e->off[k]);
    net_w_f32(&b, e->amount);
    if (!b.err)
        ng_event(ch, m, b.len);
}

static int ngt_recv(NetBuf *b, NgtEv *e)
{
    memset(e, 0, sizeof *e);
    e->type = (int)net_r_u8(b);
    e->id = (int)net_r_u32(b);
    e->obj = (int16_t)net_r_u16(b);
    e->monster = (int8_t)net_r_u8(b);
    e->level = (int)net_r_u32(b);
    e->node_id = (int)net_r_u32(b);
    e->name = ngt_r_str(b);
    e->node = ngt_r_str(b);
    for (int k = 0; k < 3; k++) e->pos[k] = net_r_f32(b);
    for (int k = 0; k < 3; k++) e->dir[k] = net_r_f32(b);
    for (int k = 0; k < 3; k++) e->off[k] = net_r_f32(b);
    e->amount = net_r_f32(b);
    return !b->err;
}

/* ================================================================ vehicles */

static void ngr_vehicle_out(const VehicleEvent *e)  /* (main.c's vehicle_step, every event the host's took) */
{
    if (e->type == VE_REMOVED && e->obj >= 0 && e->obj < NGT_MAX)
        ngt_host_gen[NGT_VEH][e->obj]++;             /* its slot's next object is another */
    if (!ng_host() || (e->type != VE_EFFECT && e->type != VE_EFFECT_STOP && e->type != VE_SOUND))
        return;
    NgtEv n = { e->type, e->id, e->obj, e->monster, e->level, 0, e->name, NULL, { 0 }, { 0 }, { 0 }, e->amount };
    memcpy(n.pos, e->pos, sizeof n.pos);
    memcpy(n.dir, e->dir, sizeof n.dir);
    ngt_send(NGP_VEHICLES, &n);
}

static void ngt_veh_event(Viewer *v, NetBuf *b)
{
    NgtEv n;
    if (!v->veh || !ngt_recv(b, &n))
        return;
    VehicleEvent e = { n.type, n.id, n.obj, n.monster, n.level, n.name, { 0 }, { 0 }, { 0 }, n.amount };
    memcpy(e.pos, n.pos, sizeof e.pos);
    memcpy(e.dir, n.dir, sizeof e.dir);
    vehicle_net_event(v->veh, &e);
}

static int ngt_veh_write(Viewer *v, NetBuf *b)
{
    if (!v->veh)
        return 0;
    static NgtOut out[NGT_MAX];
    int n = vehicle_count(v->veh), count = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const VehObj *o = vehicle_obj(v->veh, i);
        if (o->phase == VP_GONE)
            continue;
        out[count++] = (NgtOut){ i, o->phase, o->kind, o->hidden ? NGT_HIDDEN : 0, ngt_host_gen[NGT_VEH][i], o->oflags,
                                 o->name, o->shape, NULL, o->world, { o->tc.yaw, o->tc.pitch, 0, 0 } };
    }
    return ngt_write(v, b, NGT_VEH, n, out, count);
}

static void ngt_veh_apply(Viewer *v, const NetSnapSample *a, const NetSnapSample *b, float alpha, int how)
{
    const NgtSnap *sa, *sb = v->veh ? ngt_pair(v, NGT_VEH, a, b, how, &sa) : NULL;
    if (!sb)
        return;
    int n = vehicle_count(v->veh) > sb->n ? vehicle_count(v->veh) : sb->n, ia = 0, ib = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const NgtObj *ob = ngt_find(sb, i, &ib), *oa = ngt_find(sa, i, &ia);
        if (!ob) {                                   /* not there on the host: gone */
            const VehObj *g = vehicle_obj(v->veh, i);
            if (g && g->phase != VP_GONE) {
                VehObj *o = vehicle_net_obj(v->veh, i, 0);
                o->phase = VP_GONE;
                o->oflags = 0;
            }
            ngt_guest_gen[NGT_VEH][i] = -1;
            continue;
        }
        const VehObj *g = vehicle_obj(v->veh, i);
        int fresh = !g || g->phase == VP_GONE || ngt_guest_gen[NGT_VEH][i] != (int64_t)ob->gen;
        VehObj *o = vehicle_net_obj(v->veh, i, fresh);
        if (!o)
            continue;
        if (fresh || o->name != ob->name) {
            o->name = ob->name;
            o->tmpl = ob->name ? vehicle_net_template(v->veh, ob->name) : (Group){ 0 };
            o->cshape_ok = 0;
        }
        ngt_guest_gen[NGT_VEH][i] = ob->gen;
        o->kind = ob->kind;
        o->phase = ob->phase;
        o->hidden = (ob->flags & NGT_HIDDEN) != 0;
        o->oflags = ob->oflags;
        if (o->shape != ob->shape)
            o->cshape_ok = 0;
        o->shape = ob->shape;
        o->tc.yaw = ob->x[0];
        o->tc.pitch = ob->x[1];
        if (o->phase == VP_HELD && ngt_held_here(v, NGT_VEH, i))
            continue;                                /* in the guest's monster's hand (vehicle_hold) */
        ngt_place(oa, ob, alpha, o->world);
        memcpy(o->prev, o->world, sizeof o->prev);
        o->prev_ok = 0;
        for (int c = 0; c < 3; c++) {
            o->pos[c] = o->world[12 + c];
            for (int r = 0; r < 3; r++)
                o->rot[3 * c + r] = o->world[4 * c + r];
        }
    }
}

/* ================================================================ rail */

static void ngr_rail_out(const RailEvent *e)
{
    if (!ng_host() || (e->type != RE_EFFECT && e->type != RE_EFFECT_STOP && e->type != RE_SOUND &&
                       e->type != RE_SOUND_STOP))
        return;
    NgtEv n = { e->type, e->id, e->obj, e->monster, e->level, 0, e->name, NULL, { 0 }, { 0 }, { 0 }, e->amount };
    memcpy(n.pos, e->pos, sizeof n.pos);
    memcpy(n.dir, e->dir, sizeof n.dir);
    ngt_send(NGP_RAIL, &n);
}

static void ngt_rail_event(Viewer *v, NetBuf *b)
{
    NgtEv n;
    if (!v->rail || !ngt_recv(b, &n))
        return;
    RailEvent e = { n.type, n.id, n.obj, n.monster, n.level, n.name, { 0 }, { 0 }, { 0 }, n.amount };
    memcpy(e.pos, n.pos, sizeof e.pos);
    memcpy(e.dir, n.dir, sizeof e.dir);
    if (e.obj >= rail_count(v->rail))                /* (not here yet: where it was sent from) */
        e.obj = -1;
    rail_net_event(v->rail, &e);
}

static int ngt_rail_write(Viewer *v, NetBuf *b)
{
    if (!v->rail)
        return 0;
    static NgtOut out[NGT_MAX];
    int n = rail_count(v->rail), count = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const RailObj *o = rail_obj(v->rail, i);
        if (o->phase == RP_GONE)
            continue;
        out[count++] = (NgtOut){ i, o->phase, o->kind, o->hidden ? NGT_HIDDEN : 0, o->uid, o->oflags, o->name,
                                 o->shape, NULL, o->world, { 0 } };
    }
    return ngt_write(v, b, NGT_RAIL, n, out, count);
}

static void ngt_rail_apply(Viewer *v, const NetSnapSample *a, const NetSnapSample *b, float alpha, int how)
{
    const NgtSnap *sa, *sb = v->rail ? ngt_pair(v, NGT_RAIL, a, b, how, &sa) : NULL;
    if (!sb)
        return;
    int n = rail_count(v->rail) > sb->n ? rail_count(v->rail) : sb->n, ia = 0, ib = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const NgtObj *ob = ngt_find(sb, i, &ib), *oa = ngt_find(sa, i, &ia);
        const RailObj *g = rail_obj(v->rail, i);
        if (!ob) {
            if (g && g->phase != RP_GONE) {
                RailObj *o = rail_net_obj(v->rail, i, 0);
                o->phase = RP_GONE;
                o->oflags = 0;
            }
            continue;
        }
        int fresh = !g || g->phase == RP_GONE || g->uid != ob->gen;
        RailObj *o = rail_net_obj(v->rail, i, fresh);
        if (!o)
            continue;
        if (fresh || o->name != ob->name) {
            o->name = ob->name;
            o->tmpl = ob->name ? rail_net_template(v->rail, ob->name) : (Group){ 0 };
            o->cshape_ok = 0;
        }
        o->uid = ob->gen;                            /* (the flags' key, city.c rail_flags) */
        o->kind = ob->kind;
        o->phase = ob->phase;
        o->hidden = (ob->flags & NGT_HIDDEN) != 0;
        o->oflags = ob->oflags;
        if (o->shape != ob->shape)
            o->cshape_ok = 0;
        o->shape = ob->shape;
        if (o->phase == RP_HELD && ngt_held_here(v, NGT_RAIL, i))
            continue;                                /* in the guest's monster's hand (rail_hold) */
        ngt_place(oa, ob, alpha, o->world);
        memcpy(o->prev, o->world, sizeof o->prev);
        o->prev_ok = 0;
        for (int c = 0; c < 3; c++) {
            o->pos[c] = o->world[12 + c];
            for (int r = 0; r < 3; r++)
                o->rot[3 * c + r] = o->world[4 * c + r];
        }
    }
}

/* ================================================================ parade */

static void ngr_parade_out(const ParadeEvent *e)
{
    if (e->type == PAE_REMOVED && e->obj >= 0 && e->obj < NGT_MAX)
        ngt_host_gen[NGT_PARADE][e->obj]++;
    if (!ng_host() || (e->type != PAE_EFFECT && e->type != PAE_SOUND && e->type != PAE_MONSTER_SOUND &&
                       e->type != PAE_CHALLENGE_FX && e->type != PAE_FART))   /* (a fart: a food cart eaten) */
        return;
    NgtEv n = { e->type, e->id, e->obj, e->monster, e->level, 0, e->name, NULL, { 0 }, { 0 }, { 0 }, e->amount };
    memcpy(n.pos, e->pos, sizeof n.pos);
    memcpy(n.dir, e->v, sizeof n.dir);
    ngt_send(NGP_PARADE, &n);
}

static void ngt_parade_event(Viewer *v, NetBuf *b)
{
    NgtEv n;
    if (!v->parade || !ngt_recv(b, &n))
        return;
    ParadeEvent e;
    memset(&e, 0, sizeof e);
    e.type = n.type;
    e.obj = n.obj < parade_count(v->parade) ? n.obj : -1;
    e.monster = n.monster;
    e.level = n.level;
    e.name = n.name;
    e.id = n.id;
    e.amount = n.amount;
    memcpy(e.pos, n.pos, sizeof e.pos);
    memcpy(e.v, n.dir, sizeof e.v);
    if ((e.type == PAE_CHALLENGE_FX && e.obj < 0) || (e.type == PAE_FART && (e.monster < 0 || e.monster >= v->nmon)))
        return;
    parade_net_event(v->parade, &e);
}

static int ngt_parade_write(Viewer *v, NetBuf *b)
{
    if (!v->parade)
        return 0;
    static NgtOut out[NGT_MAX];
    int n = parade_count(v->parade), count = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const ParadeObj *o = parade_obj(v->parade, i);
        if (o->phase == PAP_GONE)
            continue;
        out[count++] = (NgtOut){ i, o->phase, o->kind, 0, ngt_host_gen[NGT_PARADE][i], o->oflags, o->name, o->shape,
                                 o->clip, o->world, { o->scale, o->anim_time, o->anim_len, (float)o->anim_loop } };
    }
    return ngt_write(v, b, NGT_PARADE, n, out, count);
}

static void ngt_parade_apply(Viewer *v, const NetSnapSample *a, const NetSnapSample *b, float alpha, int how)
{
    const NgtSnap *sa, *sb = v->parade ? ngt_pair(v, NGT_PARADE, a, b, how, &sa) : NULL;
    if (!sb)
        return;
    int n = parade_count(v->parade) > sb->n ? parade_count(v->parade) : sb->n, ia = 0, ib = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const NgtObj *ob = ngt_find(sb, i, &ib), *oa = ngt_find(sa, i, &ia);
        const ParadeObj *g = parade_obj(v->parade, i);
        if (!ob) {
            if (g && g->phase != PAP_GONE) {
                ParadeObj *o = parade_net_obj(v->parade, i, 0);
                o->phase = PAP_GONE;
                o->oflags = 0;
            }
            ngt_guest_gen[NGT_PARADE][i] = -1;
            continue;
        }
        int fresh = !g || g->phase == PAP_GONE || ngt_guest_gen[NGT_PARADE][i] != (int64_t)ob->gen;
        ParadeObj *o = parade_net_obj(v->parade, i, fresh);
        if (!o)
            continue;
        if (fresh || o->name != ob->name) {
            o->name = ob->name ? ob->name : "";
            o->tmpl = ob->name ? parade_net_template(v->parade, ob->name) : (Group){ 0 };
            o->cshape_ok = 0;
        }
        ngt_guest_gen[NGT_PARADE][i] = ob->gen;
        o->kind = ob->kind;
        o->phase = ob->phase;
        o->oflags = ob->oflags;
        o->shape = ob->shape;
        o->clip = ob->clip;
        o->scale = ob->x[0];
        o->anim_len = ob->x[2];
        o->anim_loop = ob->x[3] != 0;
        o->anim_time = ob->x[1];                     /* its play's time between the two (b's when it went round) */
        if (oa && oa->gen == ob->gen && oa->clip == ob->clip && ob->x[1] >= oa->x[1])
            o->anim_time = oa->x[1] + (ob->x[1] - oa->x[1]) * alpha;
        o->weapon_on = 0;                            /* (the biker's bottle is the host army's) */
        if (o->phase == PAP_HELD && ngt_held_here(v, NGT_PARADE, i))
            continue;                                /* in the guest's monster's hand (parade_hold) */
        ngt_place(oa, ob, alpha, o->world);
        memcpy(o->pos, o->world + 12, sizeof o->pos);
    }
}

/* ================================================================ street objects (never reused: by index) */

static void ngt_street_made(Viewer *v, const NgtSnap *s);

/* the street objects' effects: each has its own random numbers, seeded from the host's id and the block
 * (fx_net_seed), so the host's and the guest's copies are one (the searchlights' sweeps: their start age and turn are
 * drawn at the start, which offline come from the world's random numbers, each PC's own). The host keeps its live
 * ones; a guest that asks for the block's state (joined late, still loading when they started) gets those older than
 * 2.5 s again as late events with their age, and starts each one it has not got, run on to that age */
#define NGT_EV_LATE 0x80                             /* (in the type byte) a lasting one sent again: amount its age */
typedef struct { int id, block; double t; StreetEvent e; } NgtFx;
static int ngt_fx_live(Viewer *v, int id);
static NgtFx ngt_hfx[64];
static int ngt_nhfx;
static struct { int id, block; float age; } ngt_late[32];
static int ngt_nlate;

static uint32_t ngt_fx_seed(int block, int id)
{
    return ((uint32_t)id * 2654435761u) ^ ((uint32_t)block * 40503u) ^ 0x5EA2C417u;
}

static void ngt_fx_seed_next(Viewer *v, const StreetEvent *e)   /* both: before main.c starts the event's effect */
{
    if (!v || !v->fxw || e->type != SE_EFFECT || e->id <= 0)
        return;
    float skip = 0;
    for (int i = 0; i < ngt_nlate; i++)
        if (ngt_late[i].id == e->id && ngt_late[i].block == (int)v->block) {
            skip = ngt_late[i].age;
            ngt_late[i] = ngt_late[--ngt_nlate];
            break;
        }
    fx_net_seed(v->fxw, ngt_fx_seed((int)v->block, e->id), skip);
}

static void ngt_fx_keep(Viewer *v, const StreetEvent *e)   /* the host: its live effects, for a late guest */
{
    if (e->type == SE_EFFECT_STOP)
        for (int i = 0; i < ngt_nhfx; i++)
            if (ngt_hfx[i].id == e->id)
                ngt_hfx[i--] = ngt_hfx[--ngt_nhfx];
    if (e->type != SE_EFFECT || e->id <= 0)
        return;
    for (int i = 0; i < ngt_nhfx; i++)               /* (another block's, or played out) */
        if (ngt_hfx[i].block != (int)v->block || !ngt_fx_live(v, ngt_hfx[i].id))
            ngt_hfx[i--] = ngt_hfx[--ngt_nhfx];
    if (ngt_nhfx < 64)
        ngt_hfx[ngt_nhfx++] = (NgtFx){ e->id, (int)v->block, net_now(), *e };   /* (its name: the template's) */
}

static int ngt_fx_live(Viewer *v, int id)            /* main.c's effect of the street objects' id, still going */
{
    for (int j = 0; j < v->nsofx; j++)
        if (v->sofx[j].id == id)
            return v->fxw && fx_alive(v->fxw, v->sofx[j].fx);
    return 0;
}

static void ngr_street_out(const StreetEvent *e)
{
    if (net_role() == NET_HOST || ng_guest())        /* (the host's before its guest is in too) */
        ngt_fx_seed_next(g_ngv, e);
    if (net_role() == NET_HOST && g_ngv)
        ngt_fx_keep(g_ngv, e);
    if (!ng_host() || (e->type != SE_EFFECT && e->type != SE_EFFECT_STOP && e->type != SE_SOUND &&
                       e->type != SE_MONSTER_EFFECT))
        return;
    NgtEv n = { e->type, e->id, e->obj, e->monster, e->level, e->node_id, e->name, e->node, { 0 }, { 0 }, { 0 },
                e->amount };
    memcpy(n.pos, e->pos, sizeof n.pos);
    memcpy(n.dir, e->v, sizeof n.dir);
    memcpy(n.off, e->off, sizeof n.off);
    ngt_send(NGP_STREET, &n);
}

/* the host, answering a guest's NM_STATE: its street effects older than 2.5 s, as late events with their age */
static void ngt_street_lasting(Viewer *v)
{
    double now = net_now();
    int sent = 0;
    for (int i = 0; i < ngt_nhfx; i++) {
        if (ngt_hfx[i].block != (int)v->block || now - ngt_hfx[i].t < 2.5 || !ngt_fx_live(v, ngt_hfx[i].id))
            continue;
        const StreetEvent *e = &ngt_hfx[i].e;        /* the event again, as it was */
        NgtEv n = { SE_EFFECT | NGT_EV_LATE, e->id, e->obj, e->monster, e->level, e->node_id, e->name, e->node,
                    { 0 }, { 0 }, { 0 }, (float)(now - ngt_hfx[i].t) };
        memcpy(n.pos, e->pos, sizeof n.pos);
        memcpy(n.dir, e->v, sizeof n.dir);
        memcpy(n.off, e->off, sizeof n.off);
        ngt_send(NGP_STREET, &n);
        sent++;
    }
    if (sent)
        net_log("street objects: %d lasting effects sent again", sent);
}

static void ngt_street_event(Viewer *v, NetBuf *b)
{
    NgtEv n;
    if (!v->sobj || !ngt_recv(b, &n))
        return;
    if (n.type & NGT_EV_LATE) {                      /* a lasting one sent again: started here unless it was */
        n.type &= ~NGT_EV_LATE;
        for (int j = 0; j < v->nsofx; j++)
            if (v->sofx[j].id == n.id)
                return;
        if (ngt_nlate == 32)
            ngt_nlate--;
        ngt_late[ngt_nlate].id = n.id;
        ngt_late[ngt_nlate].block = (int)v->block;
        ngt_late[ngt_nlate++].age = n.amount;
        net_log("street objects: started the host's lasting effect %d (%s), %.1f s old", n.id, n.name, n.amount);
        n.amount = 0;
    }
    StreetEvent e;
    memset(&e, 0, sizeof e);
    e.type = n.type;
    e.id = n.id;
    e.obj = n.obj;
    e.monster = n.monster;
    e.level = n.level;
    e.node_id = n.node_id;
    e.name = n.name;
    e.node = n.node;
    e.amount = n.amount;
    memcpy(e.pos, n.pos, sizeof e.pos);
    memcpy(e.v, n.dir, sizeof e.v);
    memcpy(e.off, n.off, sizeof e.off);
    if (e.obj >= streetobj_count(v->sobj)) {         /* one the host's scripts made: from the newest snapshot */
        const NetSnapSample *s = netsnap_newest(&ng_pup_snaps[NGP_STREET]);
        const NgtSnap *ns = ngt_read(NGT_STREET, s, NULL);
        if (ns && ns->block == v->block)
            ngt_street_made(v, ns);
    }
    if (e.obj >= streetobj_count(v->sobj)) {         /* (still not here: where it started) */
        e.obj = -1;
        if (e.type == SE_EFFECT)
            e.level = 0;
    }
    streetobj_net_event(v->sobj, &e);
}

static int ngt_street_write(Viewer *v, NetBuf *b)
{
    if (!v->sobj)
        return 0;
    static NgtOut out[NGT_MAX];
    int n = streetobj_count(v->sobj), count = 0;
    for (int i = 0; i < n && i < NGT_MAX; i++) {
        const StreetObj *o = streetobj_get(v->sobj, i);
        int f = (o->alive ? NGT_ALIVE : 0) | (o->oflags & SF_HELD ? NGT_HELD : 0) | (o->moved ? NGT_MOVED : 0) |
                (o->oflags & SF_PUSH_OUT ? NGT_PUSH : 0) | (o->oflags & SF_PICKUP ? NGT_PICKUP : 0) |
                (o->made ? NGT_MADE : 0);
        out[count++] = (NgtOut){ i, 0, 0, f, 0, 0, o->made ? o->name : NULL, o->shape, NULL, o->world, { 0 } };
    }
    return ngt_write(v, b, NGT_STREET, n, out, count);
}

/* the guest: the objects the host's scripts made mid-block (a broken fountain's geyser, a water tower's puddle), made
 * here too as the host's snapshot s has them, so their indices, effects and digest are the host's */
static void ngt_street_made(Viewer *v, const NgtSnap *s)
{
    int at = 0;
    for (int i = streetobj_count(v->sobj); s && i < s->n && i < NGT_MAX && i < SCENE_STREET_MAX; i++) {
        const NgtObj *o = ngt_find(s, i, &at);
        Group g = o && (o->flags & NGT_MADE) && o->name ? rubble_tmpl(v, o->name) : (Group){ 0 };
        if (streetobj_net_make(v->sobj, i, g, o ? o->m : NULL) < 0) {
            static int said = -1;
            if (said != i)
                net_log("street objects: the host's %d (%s) not made here", i, o && o->name ? o->name : "?");
            said = i;
            return;
        }
        net_log("street objects: made the host's %d (%s)", i, o->name);
    }
}

static void ngt_street_apply(Viewer *v, const NetSnapSample *a, const NetSnapSample *b, float alpha, int how)
{
    const NgtSnap *sa, *sb = v->sobj ? ngt_pair(v, NGT_STREET, a, b, how, &sa) : NULL;
    if (!sb)
        return;
    ngt_street_made(v, sb);
    int ia = 0, ib = 0;
    for (int i = 0, n = streetobj_count(v->sobj); i < n && i < sb->n; i++) {
        const NgtObj *ob = ngt_find(sb, i, &ib), *oa = ngt_find(sa, i, &ia);
        StreetObj *o = streetobj_net_obj(v->sobj, i);
        if (!ob || !o)
            continue;
        o->alive = (ob->flags & NGT_ALIVE) != 0;
        o->oflags = (o->oflags & ~(uint32_t)(SF_HELD | SF_PUSH_OUT | SF_PICKUP)) | (ob->flags & NGT_HELD ? SF_HELD : 0) |
                    (ob->flags & NGT_PUSH ? SF_PUSH_OUT : 0) | (ob->flags & NGT_PICKUP ? SF_PICKUP : 0);
        if (ob->shape && (!o->shape || strcmp(o->shape, ob->shape))) {   /* setShape: its DamageShape */
            o->shape = ob->shape;
            scene_street_shape(&v->scene, v->street_of[i], ob->shape);
        }
        if (!(ob->flags & NGT_MOVED) || ((ob->flags & NGT_HELD) && ngt_held_here(v, NGT_STREET, i)))
            continue;                                /* where the city put it, or in the guest's hand (streetobj_hold) */
        float w[16];
        ngt_place(oa && (oa->flags & NGT_MOVED) ? oa : NULL, ob, alpha, w);
        streetobj_place(v->sobj, i, w);
        o->prev_ok = 0;
    }
}

/* ================================================================ both */

static void ngt_reset(Viewer *v)
{
    (void)v;
    memset(ngt_cache, 0, sizeof ngt_cache);
    for (int s = 0; s < NGT_N; s++)
        for (int i = 0; i < NGT_MAX; i++)
            ngt_guest_gen[s][i] = -1;
}

static void ngr_traffic_init(void)
{
    ng_pup[NGP_VEHICLES] = (NgPuppet){ "vehicles", 20, ngt_veh_write, ngt_veh_apply, ngt_veh_event, ngt_reset };
    ng_pup[NGP_RAIL] = (NgPuppet){ "rail", 20, ngt_rail_write, ngt_rail_apply, ngt_rail_event, NULL };
    ng_pup[NGP_PARADE] = (NgPuppet){ "parade", 20, ngt_parade_write, ngt_parade_apply, ngt_parade_event, NULL };
    ng_pup[NGP_STREET] = (NgPuppet){ "street objects", 15, ngt_street_write, ngt_street_apply, ngt_street_event, NULL };
    ngt_reset(NULL);
}

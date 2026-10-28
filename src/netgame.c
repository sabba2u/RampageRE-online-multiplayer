/* netgame.c - online play's side in the game (port extra; docs/superpowers/specs/2026-10-27-online-multiplayer-design.md):
 * included by main.c (it reads and writes the Viewer, the monsters and the buildings, which are main.c's own), so
 * every hook in main.c is one call. net.c is the session; this is what goes in it:
 *   the world   host -> guest, reliable, on change: city, block, play type, the players' monsters, the world's seeds;
 *               the guest loads the same (ng_follow) and the snapshots count once both are in it
 *   pads        guest -> host, unreliable, every frame: the guest's pad is the host's player 2's
 *   snapshots   host -> guest, unreliable, 20 a second: every monster (place, turn, its move state and clip time,
 *               hp, energy, rage), both players' pads, the game's state, block and clock
 *   buildings   host -> guest, reliable, as they happen: every building_hit / _hit_roof / _set_cell / _destroy the
 *               host's game made (building_net_gate), made again on the guest in order; the guest's own are not made
 *   digest      guest -> host, reliable, every 2 s: the buildings' states and HP, the game's state and block, as the
 *               guest shows them, for net.c's MISMATCH check
 * Who owns what: the host everything; the guest's own monster takes its local pad at once (the snapshots correct it).
 * People, the army, the traffic, bosses and edibles still run on each side by themselves (phase 2). */
#include "net.h"
#include "netui.h"
#include "netwire.h"

#define NG_SNAP_EVERY 0.05                           /* 20 snapshots a second */
#define NG_DIGEST_EVERY 2.0
#define NG_BUILDING_OPS 256                          /* a frame's building changes at most (more go next frame) */

static Viewer *g_ngv;
static int ng_replaying;                             /* the guest makes the host's building change: the gate is open */
static double ng_snap_at, ng_digest_at;
static uint32_t ng_followed;                         /* the host's world the guest last loaded */

/* ---- pads: Pad (main.c) <-> NetPad, the buttons MV_UP .. MV_L3 as bits 0 .. 17 */
static void ng_pad_out(const Pad *p, NetPad *o)
{
    memset(o, 0, sizeof *o);
    o->lx = p->lx; o->ly = p->ly; o->rx = p->rx; o->ry = p->ry;
    for (int k = MV_UP; k <= MV_L3; k++) {
        if (p->pressed[k]) o->pressed |= 1u << (k - MV_UP);
        if (p->held[k]) o->held |= 1u << (k - MV_UP);
    }
}

static void ng_pad_in(const NetPad *n, Pad *p, int with_pressed)
{
    memset(p, 0, sizeof *p);
    p->lx = n->lx; p->ly = n->ly; p->rx = n->rx; p->ry = n->ry;
    for (int k = MV_UP; k <= MV_L3; k++) {
        p->pressed[k] = with_pressed && (n->pressed >> (k - MV_UP) & 1);
        p->held[k] = n->held >> (k - MV_UP) & 1;
    }
}

/* ---- the world: what the guest needs to load the host's (NET_WORLD_INFO bytes at most) */
typedef struct {
    uint8_t city, block, play_type, nmon, bots, night;
    int16_t monster[MONSTERS];
    uint32_t seed[2], bot_seed;
} NgWorld;

static int ng_world_of(const Viewer *v, NgWorld *w)
{
    memset(w, 0, sizeof *w);
    int why = !v->play ? 1 : !v->have_scene ? 2 : !have_monster(v) ? 3 : !v->game ? 4 : v->shell ? 5 : v->nmon < 2 ? 6 : 0;
    static int said = -1;
    if (why != said && net_role() != NET_OFF) {
        static const char *const WHY[] = { "in a game", "not playing", "no city", "no monster", "no game", "in the menus",
                                           "fewer than 2 players" };
        net_log("this copy: %s", WHY[why]);
        said = why;
    }
    if (why)
        return 0;
    w->city = (uint8_t)v->city_index;
    w->block = (uint8_t)v->block;
    w->play_type = (uint8_t)v->play_type;
    w->nmon = (uint8_t)v->nmon;
    w->bots = (uint8_t)v->num_bots;
    w->night = (uint8_t)v->night;
    for (int k = 0; k < MONSTERS; k++)
        w->monster[k] = (int16_t)(k < v->nmon ? v->mon[k].monster : -1);
    w->seed[0] = v->prof.current.seed[0];
    w->seed[1] = v->prof.current.seed[1];
    return 1;
}

/* the id both compare: the world without its block (the game moves on to the next block by itself on both) */
static uint32_t ng_world_id(const NgWorld *w)
{
    NgWorld k = *w;
    k.block = 0;
    k.bot_seed = 0;
    uint32_t h = net_hash(&k, (int)sizeof k, 2166136261u);
    return h ? h : 1;
}

static void ng_set_world(Viewer *v)
{
    NgWorld w;
    if (!ng_world_of(v, &w)) {
        net_set_world(0, NULL, 0);
        return;
    }
    if (net_role() == NET_HOST)
        w.bot_seed = v->bot_seed;
    net_set_world(ng_world_id(&w), &w, (int)sizeof w);
}

/* the guest: the host's world loaded as the viewer's start would (--players, a city and block) */
static void ng_follow(Viewer *v)
{
    int len;
    const NgWorld *w = (const NgWorld *)net_host_world_info(&len);
    if (!w || len != (int)sizeof *w || net_host_world() == ng_followed)
        return;
    ng_followed = net_host_world();
    if (w->city >= NCITIES + 1 || w->nmon < 2 || w->nmon > MONSTERS) {
        net_log("cannot load the host's game (city %d, %d players)", w->city, w->nmon);
        return;
    }
    net_log("loading the host's game: %s block %d, %d players", CITIES[w->city], w->block, w->nmon);
    if (v->shell) {
        net_log("leave the menus first (phase 1 joins from a city)");
        return;
    }
    int pick[MONSTERS];
    for (int k = 0; k < MONSTERS; k++)
        pick[k] = w->monster[k];
    v->prof.current.seed[0] = w->seed[0];
    v->prof.current.seed[1] = w->seed[1];
    v->bot_seed = w->bot_seed;
    v->play_type = w->play_type;
    v->night = w->night;
    game_free(v->game);
    v->game = NULL;
    v->game_built = 0;
    setup_players(v, w->nmon, w->bots, pick);
    if (open_city(v, w->city, w->block, -1) != 0) {
        net_log("cannot open %s", CITIES[w->city]);
        return;
    }
    v->play = have_monster(v);
    for (int k = 0; v->play && k < v->nmon; k++)
        if (v->mon[k].have_graph)
            play_state(&v->mon[k]);
    net_snaps_clear();
}

/* ---- buildings */
static int ng_building_index(const Viewer *v, const Building *b)
{
    for (uint32_t i = 0; i < v->scene.nbuildings; i++)
        if (&v->scene.buildings[i].b == b)
            return (int)i;
    return -1;
}

static uint8_t ng_ops[16 + NG_BUILDING_OPS * 32];
static NetBuf ng_opbuf;
static int ng_nops;

static int ng_building_gate(Building *b, int op, const float *p, float r, int a0, int a1, int a2)
{
    if (net_role() == NET_GUEST && net_live())
        return !ng_replaying;                        /* the guest's own: not made */
    if (net_role() != NET_HOST || !net_live() || !g_ngv)
        return 0;
    int i = ng_building_index(g_ngv, b);
    if (i < 0 || ng_nops >= NG_BUILDING_OPS)
        return 0;
    if (!ng_nops) {
        ng_opbuf = netbuf(ng_ops, sizeof ng_ops);
        net_w_u8(&ng_opbuf, NM_BUILDING);
        net_w_u8(&ng_opbuf, (uint32_t)g_ngv->block);
    }
    net_w_u16(&ng_opbuf, (uint32_t)i);
    net_w_u8(&ng_opbuf, (uint32_t)op);
    for (int k = 0; k < 3; k++)
        net_w_f32(&ng_opbuf, p ? p[k] : 0);
    net_w_f32(&ng_opbuf, r);
    net_w_u32(&ng_opbuf, (uint32_t)a0);
    net_w_u32(&ng_opbuf, (uint32_t)a1);
    net_w_u32(&ng_opbuf, (uint32_t)a2);
    ng_nops++;
    return 0;
}

static void ng_building_flush(void)
{
    if (ng_nops && !ng_opbuf.err)
        net_send_msg(1, ng_ops, ng_opbuf.len);
    ng_nops = 0;
}

static void ng_on_building(Viewer *v, NetBuf *b)
{
    int block = (int)net_r_u8(b);
    if (block != v->block) {
        net_log("building changes for block %d, we are in %d: not made", block, v->block);
        return;
    }
    ng_replaying = 1;
    while (b->len < b->cap && !b->err) {
        uint32_t i = net_r_u16(b);
        int op = (int)net_r_u8(b);
        float p[3], r;
        for (int k = 0; k < 3; k++)
            p[k] = net_r_f32(b);
        r = net_r_f32(b);
        int a0 = (int)net_r_u32(b), a1 = (int)net_r_u32(b), a2 = (int)net_r_u32(b);
        if (b->err || i >= v->scene.nbuildings)
            break;
        Building *bb = &v->scene.buildings[i].b;
        switch (op) {
        case BNET_HIT: building_hit(bb, p, r); break;
        case BNET_ROOF: { int col; building_hit_roof(bb, p, r, &col); break; }
        case BNET_CELL: building_set_cell(bb, a0, a1, a2 & 0xFFFF, a2 >> 16); break;
        case BNET_DESTROY: building_destroy(bb); break;
        }
    }
    ng_replaying = 0;
}

/* ---- the messages netgame.c reads itself */
static void ng_handler(const uint8_t *data, int len)
{
    if (!g_ngv)
        return;
    NetBuf b = netbuf((void *)data, len);
    int type = (int)net_r_u8(&b);
    if (type == NM_BUILDING && net_role() == NET_GUEST && net_live())
        ng_on_building(g_ngv, &b);
}

/* ---- the digest: what both must agree on (buildings, game state, block) */
static uint32_t ng_digest(const Viewer *v)
{
    uint32_t h = 2166136261u;
    int gs = v->game ? game_state(v->game) : 0, blk = v->block;
    if (getenv("RAMPAGE_NET_TRACE")) {               /* what went into it, for a MISMATCH */
        int hp = 0, down = 0;
        for (uint32_t i = 0; i < v->scene.nbuildings; i++) {
            hp += v->scene.buildings[i].b.hp;
            down += v->scene.buildings[i].b.state != BUILDING_STANDING;
        }
        net_log("digest: state %d block %d, %u buildings, hp %d, %d not standing", gs, blk, v->scene.nbuildings, hp, down);
    }
    h = net_hash(&gs, 4, h);
    h = net_hash(&blk, 4, h);
    for (uint32_t i = 0; i < v->scene.nbuildings; i++) {
        const Building *b = &v->scene.buildings[i].b;
        int st[2] = { b->state >= BUILDING_CRACKING ? BUILDING_CRACKING : b->state, b->hp };
        h = net_hash(st, 8, h);
    }
    return h;
}

/* ---- snapshots */
typedef struct {
    float pos[3], heading, vel[3], time, hp, energy;
    int16_t state;
    uint8_t mode, mirror, defeated, rage;
} NgMon;

static void ng_send_snap(Viewer *v)
{
    uint8_t m[NET_SNAP_MAX];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, (uint32_t)v->nmon);
    for (int k = 0; k < v->nmon; k++) {
        const Mon *mo = &v->mon[k];
        net_w_f32(&b, mo->mx); net_w_f32(&b, mo->my); net_w_f32(&b, mo->mz); net_w_f32(&b, mo->heading);
        net_w_f32(&b, mo->vel[0]); net_w_f32(&b, mo->vel[1]); net_w_f32(&b, mo->vel[2]);
        net_w_f32(&b, mo->run.time); net_w_f32(&b, mo->hp); net_w_f32(&b, mo->power.energy);
        net_w_u16(&b, (uint32_t)(mo->have_graph ? mo->run.state : 0));
        net_w_u8(&b, (uint32_t)mo->mode); net_w_u8(&b, (uint32_t)mo->mirror);
        net_w_u8(&b, (uint32_t)mo->defeated); net_w_u8(&b, (uint32_t)mo->power.rage);
    }
    for (int k = 0; k < 2; k++) {                    /* both players' pads (the guest's monsters animate by them) */
        NetPad p;
        ng_pad_out(&v->mon[k].pad, &p);
        net_w_f32(&b, p.lx); net_w_f32(&b, p.ly); net_w_f32(&b, p.rx); net_w_f32(&b, p.ry);
        net_w_u32(&b, p.held);
    }
    net_w_u8(&b, (uint32_t)(v->game ? game_state(v->game) : 0));
    net_w_u8(&b, (uint32_t)v->block);
    net_w_f32(&b, v->game ? game_clock(v->game, NULL) : 0);
    GameNetSync gs;                                  /* the clock and the score (game_net_get) */
    game_net_get(v->game, &gs);
    net_w_u8(&b, (uint32_t)gs.running);
    net_w_f32(&b, gs.elapsed);
    for (int k = 0; k < 2; k++) {
        for (int c = 0; c < 7; c++) {
            net_w_u32(&b, (uint32_t)gs.points[k][c]);
            net_w_u32(&b, (uint32_t)gs.kills[k][c]);
        }
        net_w_u8(&b, (uint32_t)gs.lives[k]);
    }
    if (!b.err)
        net_send_snap(m, b.len);
}

typedef struct { int n; NgMon m[MONSTERS]; NetPad pad[2]; int game_state, block; float clock; GameNetSync gs; } NgSnap;

static int ng_read_snap(const NetSnapSample *s, NgSnap *o)
{
    NetBuf b = netbuf((void *)s->data, s->len);
    memset(o, 0, sizeof *o);
    o->n = (int)net_r_u8(&b);
    if (o->n > MONSTERS)
        return 0;
    for (int k = 0; k < o->n; k++) {
        NgMon *m = &o->m[k];
        for (int i = 0; i < 3; i++) m->pos[i] = net_r_f32(&b);
        m->heading = net_r_f32(&b);
        for (int i = 0; i < 3; i++) m->vel[i] = net_r_f32(&b);
        m->time = net_r_f32(&b); m->hp = net_r_f32(&b); m->energy = net_r_f32(&b);
        m->state = (int16_t)net_r_u16(&b);
        m->mode = (uint8_t)net_r_u8(&b); m->mirror = (uint8_t)net_r_u8(&b);
        m->defeated = (uint8_t)net_r_u8(&b); m->rage = (uint8_t)net_r_u8(&b);
    }
    for (int k = 0; k < 2; k++) {
        NetPad *p = &o->pad[k];
        p->lx = net_r_f32(&b); p->ly = net_r_f32(&b); p->rx = net_r_f32(&b); p->ry = net_r_f32(&b);
        p->held = net_r_u32(&b);
    }
    o->game_state = (int)net_r_u8(&b);
    o->block = (int)net_r_u8(&b);
    o->clock = net_r_f32(&b);
    o->gs.running = (int)net_r_u8(&b);
    o->gs.elapsed = net_r_f32(&b);
    for (int k = 0; k < 2; k++) {
        for (int c = 0; c < 7; c++) {
            o->gs.points[k][c] = (int)net_r_u32(&b);
            o->gs.kills[k][c] = (int)net_r_u32(&b);
        }
        o->gs.lives[k] = (int)net_r_u8(&b);
    }
    return !b.err;
}

static float ng_lerp_angle(float a, float b, float t)   /* degrees, the short way */
{
    float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    return a + d * t;
}

static NgSnap ng_last;                               /* the newest the guest read (its pads, its flow) */
static int ng_have_last;

/* the guest: the monsters as the host has them NET_SNAP_DELAY ago */
static void ng_apply_snap(Viewer *v)
{
    const NetSnapSample *a, *b;
    float alpha;
    int r = netsnap_sample(net_snaps(), net_now(), NET_SNAP_DELAY, &a, &b, &alpha);
    if (!r)
        return;
    static NgSnap sa, sb;
    if (!ng_read_snap(b, &sb) || (r == 2 && !ng_read_snap(a, &sa)))
        return;
    for (int k = 0; k < v->nmon && k < sb.n; k++) {
        Mon *mo = &v->mon[k];
        const NgMon *hi = &sb.m[k];
        float pos[3], heading;
        if (r == 2) {
            const NgMon *lo = &sa.m[k];
            for (int i = 0; i < 3; i++)
                pos[i] = lo->pos[i] + (hi->pos[i] - lo->pos[i]) * alpha;
            heading = ng_lerp_angle(lo->heading, hi->heading, alpha);
        } else {                                     /* past the newest: on along its velocity, alpha seconds */
            for (int i = 0; i < 3; i++)
                pos[i] = hi->pos[i] + hi->vel[i] * alpha;
            heading = hi->heading;
        }
        mo->mx = pos[0]; mo->my = pos[1]; mo->mz = pos[2];
        mo->heading = heading;
        memcpy(mo->vel, hi->vel, sizeof mo->vel);
        if (mo->have_graph && hi->state >= 0 && hi->state < mo->graph.n) {
            if (mo->run.state != hi->state) {
                moves_force(&mo->run, hi->state);
                mo->run.time = hi->time;
            } else if (fabsf(mo->run.time - hi->time) > 0.25f)
                mo->run.time = hi->time;
        }
        mo->mode = hi->mode;
        mo->mirror = hi->mirror;
        mo->defeated = hi->defeated;
        mo->hp = hi->hp;
        mo->power.energy = hi->energy;
        mo->power.rage = hi->rage;
    }
    const NetSnapSample *n = netsnap_newest(net_snaps());
    ng_have_last = n && ng_read_snap(n, &ng_last);
    if (r == 2 || r == 1)                            /* the host's clock and score, as of the sample shown */
        game_net_put(v->game, &sb.gs, v->nmon < 2 ? v->nmon : 2);
    if (ng_have_last && ng_last.block != v->block) {
        static int warned = -1;
        if (warned != ng_last.block)
            net_log("the host is in block %d, we are in %d", ng_last.block, v->block);
        warned = ng_last.block;
    }
    if (net_now() - ng_digest_at >= NG_DIGEST_EVERY && n) {
        ng_digest_at = net_now();
        net_send_digest(n->t, ng_digest(v));
    }
}

/* ---- the hooks main.c calls */

/* at the start: --host [port], --join ip[:port]; the panel's window */
static void netgame_init(Viewer *v, int argc, char **argv, void *hwnd, int hidden)
{
    g_ngv = v;
    net_set_handler(ng_handler);
    building_net_gate = ng_building_gate;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--host"))
            net_start_host(i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9' ? atoi(argv[++i]) : NET_PORT);
        else if (!strcmp(argv[i], "--join") && i + 1 < argc)
            net_start_join(argv[++i]);
    }
    if (!hidden && netui_init(hwnd) != 0)
        printf("net: the panel (Dear ImGui) did not start\n");
}

/* once a frame, before the input: the connection, the world, the guest following the host */
static void netgame_frame(Viewer *v, Window *w, double now)
{
    net_update(now);
    if (net_role() == NET_OFF)
        return;
    ng_set_world(v);
    if (net_role() == NET_GUEST && net_state() == NETS_CONNECTED && net_host_world() &&
        net_host_world() != net_world())
        ng_follow(v);
    if (netui_wants_input()) {                       /* the panel has the keys */
        memset(w->keys, 0, sizeof w->keys);
        memset(w->pressed, 0, sizeof w->pressed);
    }
}

/* the players' pads are set (keys_pad / pad_to_pad): the host's player 2 is the guest's pad; the guest sends its own
 * (player 1's keys and pad 0) and plays it on its player 2 at once, player 1 by the host's */
static void netgame_pads(Viewer *v, const Window *w)
{
    if (!net_live() || v->nmon < 2)
        return;
    if (net_role() == NET_HOST) {
        NetPad np;
        if (net_remote_pad(&np))
            ng_pad_in(&np, &v->mon[1].pad, 1);
        else
            memset(&v->mon[1].pad, 0, sizeof v->mon[1].pad);
        v->mon[1].drive = DRIVE_KEYS2;
        return;
    }
    Pad mine;
    keys_pad(w, DRIVE_KEYS1, &mine);
    if (v->pads_live && (v->pads_on & 1))
        pad_to_pad(&v->pads[0], &v->pads_prev[0], &mine);
    if (getenv("RAMPAGE_NET_TESTPAD")) {             /* tools/net_test.ps1 -TestPad: walk left and right 3 s each,
                                                      * a punch every 0.6 s */
        static double t0;
        static int was;
        if (!t0)
            t0 = net_now();
        double t = net_now() - t0;
        memset(&mine, 0, sizeof mine);
        mine.lx = fmod(t, 6.0) < 3.0 ? 1.0f : -1.0f;
        int on = fmod(t, 0.6) < 0.1;
        mine.held[MV_SQUARE] = (uint8_t)on;
        mine.pressed[MV_SQUARE] = (uint8_t)(on && !was);
        was = on;
    }
    NetPad np;
    ng_pad_out(&mine, &np);
    net_send_pad(&np);
    v->mon[1].pad = mine;
    if (ng_have_last)
        ng_pad_in(&ng_last.pad[0], &v->mon[0].pad, 0);
    else
        memset(&v->mon[0].pad, 0, sizeof v->mon[0].pad);
}

/* after the world's step, before the monsters are posed: the host's snapshot, or the guest's taking it */
static void netgame_after_step(Viewer *v)
{
    if (!net_live())
        return;
    if (net_role() == NET_HOST) {
        ng_building_flush();
        if (net_now() - ng_snap_at >= NG_SNAP_EVERY) {
            ng_snap_at = net_now();
            ng_send_snap(v);
            net_keep_digest(net_now(), ng_digest(v));
        }
    } else
        ng_apply_snap(v);
}

/* just before the swap: the panel */
static void netgame_draw(void) { netui_frame(); }


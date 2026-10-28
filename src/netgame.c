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
#include "nettransport.h"

/* the host's updates a second (the monsters' snapshot and every puppet channel's): 20, 30 or 60; --net-rate N,
 * RAMPAGE_NET_RATE or the panel. A channel's own rate is its share at 20 (street objects 15: 3/4 of the rate) */
static int ng_rate = 60;

int netgame_rate(void) { return ng_rate; }
void netgame_set_rate(int r) { ng_rate = r <= 20 ? 20 : r <= 30 ? 30 : 60; }

/* the guest's delay behind the host: two of the host's update gaps (measured from the snapshots' host times), so 100 ms
 * at 20 a second and about 35 ms at 60; at least 30 ms, at most 200 */
static double ng_delay(void)
{
    const NetSnapBuf *b = net_snaps();
    int n = b->n < 8 ? b->n : 8;
    if (n < 2)
        return NET_SNAP_DELAY;
    const NetSnapSample *hi = &b->s[b->head], *lo = &b->s[(b->head - (n - 1) + NET_SNAP_N) % NET_SNAP_N];
    double gap = (hi->t - lo->t) / (n - 1), d = 2 * gap;
    return d < 0.03 ? 0.03 : d > 0.2 ? 0.2 : d;
}
#define NG_DIGEST_EVERY 2.0
#define NG_BUILDING_OPS 256                          /* a frame's building changes at most (more go next frame) */

static Viewer *g_ngv;
static int ng_replaying;                             /* the guest makes the host's building change: the gate is open */
static double ng_snap_at, ng_digest_at;
static uint32_t ng_followed;                         /* the host's world the guest last loaded */
static int ng_hud_shown = -1;                        /* the host: the last GE_HUD's a (-1 none yet) */
static int ng_guest(void);
static int ng_host(void);

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
    int why = !v->play ? 1 : !v->have_scene ? 2 : !have_monster(v) ? 3 : !v->game ? 4 : v->shell ? 5 : v->nmon < 2 ? 6 :
              v->attract ? 7 : 0;
    static int said = -1;
    if (why != said && net_role() != NET_OFF) {
        static const char *const WHY[] = { "in a game", "not playing", "no city", "no monster", "no game", "in the menus",
                                           "fewer than 2 players", "the attract mode's demo" };
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

static void ng_puppets_reset(Viewer *v);

static void ng_set_world(Viewer *v)
{
    NgWorld w;
    if (!ng_world_of(v, &w)) {
        net_set_world(0, NULL, 0);
        return;
    }
    if (net_role() == NET_HOST)
        w.bot_seed = v->bot_seed;
    uint32_t id = ng_world_id(&w);
    static uint32_t was_id;
    static int was_block = -1;
    if (id != was_id || v->block != was_block)       /* a new world or block: the puppets start again */
        ng_puppets_reset(v);
    was_id = id;
    was_block = v->block;
    net_set_world(id, &w, (int)sizeof w);
}

static void ngr_menu_follow(Viewer *v);
static void ng_flow_reset(Viewer *v);

/* the guest: the host's world loaded as the viewer's start would (--players, a city and block); from the front end,
 * the front end left first as a game's start leaves it (shell_start_game) */
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
        movie_stop(v);
        gui3d_free(v);
        v->prof.profile_shown = shell_profile_shown(v->shell);
        shell_close(v->shell);
        v->shell = NULL;
        v->shell_go = 0;
    }
    ngr_menu_follow(v);                              /* (the guest's profile kept for after) */
    v->attract = v->attract_button = 0;
    v->exit_game = 0;
    v->clip = -1;
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
    ng_flow_reset(v);                                /* (the host's events of a game before this one: dropped) */
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
static uint32_t ng_bseq;                             /* host: the NM_BUILDING messages sent (each carries its number) */
/* guest: the block whose whole state the host sent (-1 none yet: the host's changes wait for it, they are in it) and
 * the last NM_BUILDING it reflects */
static int ng_bs_block = -1;
static uint32_t ng_bs_seq;

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
        net_w_u32(&ng_opbuf, ng_bseq + 1);           /* (its number: ng_building_flush counts it) */
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
    if (ng_nops && !ng_opbuf.err) {
        net_send_msg(1, ng_ops, ng_opbuf.len);
        ng_bseq++;
    }
    ng_nops = 0;
}

static void ng_on_building(Viewer *v, NetBuf *b)
{
    int block = (int)net_r_u8(b);
    if (block != v->block) {
        net_log("building changes for block %d, we are in %d: not made", block, v->block);
        return;
    }
    uint32_t seq = net_r_u32(b);
    if (ng_bs_block != block)
        return;                                      /* (the block's state is on its way, with these in it) */
    if (seq <= ng_bs_seq) {
        net_log("building changes %u came after the block's state (%u): not made", seq, ng_bs_seq);
        return;
    }
    ng_bs_seq = seq;
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

/* ---- the block's state, whole: for a guest that joins (or comes back, or starts a block) after the host's game made
 * changes it would never see. The guest asks (NST_ASK, its block) until it has it; the host sends, after that frame's
 * building changes: every building as it is (Building is plain data, the same build on both: packed), the edibles'
 * slots, the challenge's upgrade, then NST_DONE with the number of the last NM_BUILDING in it. All reliable and in
 * order, so the guest drops the building changes until NST_DONE (they are in the state) and makes every one after. */
enum { NST_ASK, NST_BUILDING, NST_EDIBLES, NST_CHALLENGE, NST_DONE };
static int ng_state_due = -1;                        /* host: the block the guest asked for */
static double ng_state_asked;                        /* guest: when it last asked */

static void ng_state_ask(Viewer *v)                  /* guest, every frame */
{
    static int was_live, was_block = -1;
    static uint32_t was_world;
    if (!(net_role() == NET_GUEST && net_live())) {
        was_live = 0;
        ng_bs_block = -1;
        return;
    }
    if (!was_live || was_world != net_host_world() || was_block != v->block) {
        ng_bs_block = -1;                            /* joined, back, or a new block: ask again */
        ng_state_asked = -100;
    }
    was_live = 1;
    was_world = net_host_world();
    was_block = v->block;
    if (ng_bs_block == v->block || net_now() - ng_state_asked < 1.0)
        return;
    ng_state_asked = net_now();
    uint8_t m[8];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_STATE);
    net_w_u8(&b, NST_ASK);
    net_w_u8(&b, (uint32_t)v->block);
    net_send_msg(1, m, b.len);
}

static void ng_state_send(Viewer *v)                 /* host, after the frame's building changes went */
{
    if (ng_state_due < 0 || !(net_role() == NET_HOST && net_live()))
        return;
    if (ng_state_due != v->block) {
        net_log("the guest asked for block %d's state, we are in %d", ng_state_due, v->block);
        ng_state_due = -1;
        return;
    }
    ng_state_due = -1;
    static uint8_t m[sizeof(Building) + sizeof(Building) / 32 + 64];
    int total = 0, biggest = 0;
    for (uint32_t i = 0; i < v->scene.nbuildings; i++) {
        NetBuf b = netbuf(m, sizeof m);
        net_w_u8(&b, NM_STATE);
        net_w_u8(&b, NST_BUILDING);
        net_w_u8(&b, (uint32_t)v->block);
        net_w_u16(&b, i);
        net_w_u16(&b, v->scene.nbuildings);
        net_w_u32(&b, (uint32_t)sizeof(Building));
        net_pack(&b, &v->scene.buildings[i].b, (int)sizeof(Building));
        if (b.err) {
            net_log("building %u's state does not fit", i);
            continue;
        }
        net_send_msg(1, m, b.len);
        total += b.len;
        biggest = b.len > biggest ? b.len : biggest;
    }
    int ns = v->edibles ? edible_slots(v->edibles) : 0;
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_STATE);
    net_w_u8(&b, NST_EDIBLES);
    net_w_u8(&b, (uint32_t)v->block);
    net_w_u16(&b, (uint32_t)ns);
    for (int i = 0; i < ns; i++) {
        const EdibleSlot *sl = edible_slot(v->edibles, i);
        const Edible *d = sl->item >= 0 ? edible_get(v->edibles, sl->item) : NULL;
        int out = d && d->phase == EP_WINDOW;
        net_w_u8(&b, (uint32_t)(uint8_t)(int8_t)(d && !out ? -1 : sl->kind));   /* (one picked or falling: gone) */
        net_w_u8(&b, (uint32_t)out);
        if (out)
            net_w_f32(&b, d->age);
    }
    if (!b.err) {
        net_send_msg(1, m, b.len);
        total += b.len;
    }
    const Challenge *c = &v->challenge;
    b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_STATE);
    net_w_u8(&b, NST_CHALLENGE);
    net_w_u8(&b, (uint32_t)v->block);
    net_w_u8(&b, (uint32_t)c->up_state);
    net_w_u8(&b, (uint32_t)c->up_made);
    net_w_f32(&b, c->up_delay);
    net_w_f32(&b, c->up_pulse);
    net_w_u32(&b, c->upgrade);
    net_w_u32(&b, c->up_taken);
    net_send_msg(1, m, b.len);
    b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_STATE);
    net_w_u8(&b, NST_DONE);
    net_w_u8(&b, (uint32_t)v->block);
    net_w_u32(&b, ng_bseq);
    net_send_msg(1, m, b.len);
    char line[400];                                  /* each building's state, HP and storeys standing, for the log */
    int len = 0;
    for (uint32_t i = 0; i < v->scene.nbuildings && len < (int)sizeof line - 32; i++) {
        const Building *bb = &v->scene.buildings[i].b;
        len += snprintf(line + len, sizeof line - (size_t)len, " %u:%d/%d/%d", i, bb->state, bb->hp, bb->n);
    }
    net_log("buildings (state/HP/storeys):%s", line);
    net_log("sent block %d's state: %u buildings (%d bytes each unpacked, %d packed at most), %d edible slots, %d bytes",
            v->block, v->scene.nbuildings, (int)sizeof(Building), biggest, ns, total);
}

/* guest: building i as the host's. Its drawing follows (dirty: the facade, the interiors, the light; building_place
 * places a fallen or falling one); what its cells' breaking would have told here already happened on the host (the
 * edibles come next, the people and the army are the host's), so the scene is told it has seen them */
static void ng_state_building(Viewer *v, NetBuf *b)
{
    static Building nb;
    uint32_t i = net_r_u16(b), n = net_r_u16(b), size = net_r_u32(b);
    if (b->err || size != sizeof(Building) || i >= v->scene.nbuildings) {
        net_log("the host's building %u of %u (%u bytes): not ours (%u buildings, %d bytes)", i, n, size,
                v->scene.nbuildings, (int)sizeof(Building));
        return;
    }
    if (net_unpack(b, &nb, (int)sizeof nb) != 0) {
        net_log("the host's building %u: broken", i);
        return;
    }
    SceneBuilding *sb = &v->scene.buildings[i];
    if (memcmp(sb->b.min, nb.min, sizeof nb.min) || sb->b.storeys != nb.storeys || sb->b.width != nb.width) {
        net_log("the host's building %u is another building", i);
        return;
    }
    sb->b = nb;
    Building *bb = &sb->b;
    bb->ndebris = bb->nhit_debris = bb->nscore = 0;  /* (the host played and paid those) */
    bb->pre_collapse = bb->drop_dust = 0;
    bb->dirty = 1;
    bb->ground_due = 1;
    sb->seen_falls = bb->falls;
    sb->seen_state = bb->state;
    for (int o = 0; o < BUILDING_STOREYS; o++)
        for (int col = 0; col < bb->width; col++) {
            int ci = building_cell_index(bb, FACE_FRONT, col);
            if (ci >= 0 && bb->cell[o][ci].state >= 2 && sb->broke[o][ci] < bb->cell[o][ci].state)
                sb->broke[o][ci] = bb->cell[o][ci].state;
        }
}

static void ng_state_edibles(Viewer *v, NetBuf *b)
{
    int ns = (int)net_r_u16(b), have = v->edibles ? edible_slots(v->edibles) : 0;
    if (ns != have)
        net_log("the host has %d edible slots, we have %d", ns, have);
    for (int i = 0; i < ns && !b->err; i++) {
        int kind = (int8_t)net_r_u8(b), out = (int)net_r_u8(b);
        float age = out ? net_r_f32(b) : 0;
        if (!b->err && i < have)
            edible_net_slot(v->edibles, i, kind, out, age);
    }
}

static void ng_state_challenge(Viewer *v, NetBuf *b)
{
    Challenge *c = &v->challenge;
    int st = (int)net_r_u8(b), made = (int)net_r_u8(b);
    float delay = net_r_f32(b), pulse = net_r_f32(b);
    uint32_t up = net_r_u32(b), taken = net_r_u32(b);
    if (b->err)
        return;
    if (st == CU_WAITING && c->up_state != CU_WAITING && v->fxw && c->up.effect && v->upgrade_fx < 0)
        v->upgrade_fx = fx_spawn(v->fxw, c->up.effect, c->up.pos, NULL, 1);   /* (as its CUE_SPAWN) */
    else if (st != CU_WAITING && v->upgrade_fx >= 0) {
        if (v->fxw)
            fx_stop(v->fxw, v->upgrade_fx);
        v->upgrade_fx = -1;
    }
    c->up_state = st;
    c->up_made = made;
    c->up_delay = delay;
    c->up_pulse = pulse;
    c->upgrade = up;
    c->up_taken = taken;
}

static void ng_on_state(Viewer *v, NetBuf *b)
{
    int kind = (int)net_r_u8(b), block = (int)net_r_u8(b);
    if (b->err)
        return;
    if (net_role() == NET_HOST) {
        if (kind == NST_ASK)
            ng_state_due = block;
        return;
    }
    if (block != v->block) {
        net_log("the host's state of block %d, we are in %d: not taken", block, v->block);
        return;
    }
    switch (kind) {
    case NST_BUILDING: ng_state_building(v, b); break;
    case NST_EDIBLES: ng_state_edibles(v, b); break;
    case NST_CHALLENGE: ng_state_challenge(v, b); break;
    case NST_DONE:
        ng_bs_block = block;
        ng_bs_seq = net_r_u32(b);
        net_log("took the host's block %d state (its building changes from %u on)", block, ng_bs_seq + 1);
        break;
    }
}

/* ---- the messages netgame.c reads itself (the puppets' and the menus' below) */
static void ng_on_puppet(NetBuf *b, int type);
static void ngr_menu_message(Viewer *v, NetBuf *b);
static void ng_on_flow(NetBuf *b);
static void ng_handler(const uint8_t *data, int len)
{
    if (!g_ngv)
        return;
    NetBuf b = netbuf((void *)data, len);
    int type = (int)net_r_u8(&b);
    if (type == NM_BUILDING && net_role() == NET_GUEST && net_live())
        ng_on_building(g_ngv, &b);
    else if ((type == NM_PUPPET || type == NM_PEVENT) && net_role() == NET_GUEST && net_live())
        ng_on_puppet(&b, type);
    else if (type == NM_EVENT && net_role() == NET_GUEST && net_live())
        ng_on_flow(&b);
    else if (type == NM_MENU)
        ngr_menu_message(g_ngv, &b);
    else if (type == NM_STATE && (net_role() == NET_HOST || net_live()))
        ng_on_state(g_ngv, &b);
}

/* ---- the digest: what both must agree on, a part a system (netwire.h NetDigest; net.c compares each). The host's
 * as its game is, the guest's as it shows it (after the snapshots and the puppets are applied); the counts and states
 * exactly, the places as a mean (within the system's tolerance: the guest's blending and its hands move them a bit) */
static uint32_t ng_h(uint32_t h, int x) { return net_hash(&x, 4, h); }

/* the mean place of n points summed in sum (0 0 0 none) into v[0 .. 2] */
static void ng_mean(float *v, const double sum[3], int n)
{
    for (int k = 0; k < 3; k++)
        v[k] = n ? (float)(sum[k] / n) : 0;
}

static void ng_digest(const Viewer *v, NetDigest *d)
{
    memset(d, 0, sizeof *d);
    const uint32_t H0 = 2166136261u;
    NetDSys *s = &d->s[NDS_GAME];                    /* the game: state, block, the score and lives, the clock */
    GameNetSync gs;
    memset(&gs, 0, sizeof gs);
    if (v->game)
        game_net_get(v->game, &gs);
    s->h = ng_h(ng_h(H0, v->game ? game_state(v->game) : 0), v->block);
    for (int k = 0; k < 2; k++) {
        s->h = ng_h(s->h, gs.lives[k]);
        for (int c = 0; c < 7; c++)
            s->h = ng_h(ng_h(s->h, gs.points[k][c]), gs.kills[k][c]);
    }
    s->v[0] = v->game ? game_clock(v->game, NULL) : 0;
    s = &d->s[NDS_MONSTERS];                         /* the monsters: defeated, raging; places and HP */
    s->h = ng_h(H0, v->nmon);
    for (int k = 0; k < v->nmon; k++) {
        const Mon *mo = &v->mon[k];
        s->h = ng_h(ng_h(s->h, mo->defeated), mo->power.rage);
        if (k < 2) {
            s->v[3 * k] = mo->mx;
            s->v[3 * k + 1] = mo->my;
            s->v[3 * k + 2] = mo->mz;
            s->v[6 + k] = mo->hp;
        }
    }
    s = &d->s[NDS_BUILDINGS];                        /* the buildings: state, HP, storeys, roof, cells, floors */
    s->h = ng_h(H0, (int)v->scene.nbuildings);
    for (uint32_t i = 0; i < v->scene.nbuildings; i++) {
        const Building *b = &v->scene.buildings[i].b;
        int cells = 0, floors = 0;
        for (int ls = 0; ls < b->n; ls++) {
            int o = b->live[ls];
            floors += b->floor_hp[o];
            for (int col = 0; col < b->width; col++) {
                int ci = building_cell_index(b, FACE_FRONT, col);
                cells += ci >= 0 ? b->cell[o][ci].state : 0;
            }
        }
        s->h = ng_h(ng_h(s->h, b->state >= BUILDING_CRACKING ? BUILDING_CRACKING : b->state), b->hp);
        s->h = ng_h(ng_h(ng_h(ng_h(s->h, b->n), b->roof), cells), floors);
    }
    s = &d->s[NDS_PEOPLE];                           /* people: how many, where (not those in a hand: the guest's own
                                                      * hand holds them) */
    double sum[3] = { 0, 0, 0 };
    int n = 0, live = 0;
    for (int who = 0; v->scene.folk && who < people_count(v->scene.folk); who++) {
        const Person *q = people_get(v->scene.folk, who);
        if (q->phase == PP_GONE)
            continue;
        live++;
        if (q->phase == PP_HELD)
            continue;
        for (int k = 0; k < 3; k++)
            sum[k] += q->pos[k];
        n++;
    }
    s->h = ng_h(H0, live);
    ng_mean(s->v, sum, n);
    s = &d->s[NDS_ARMY];                             /* the army: units, where, shots */
    memset(sum, 0, sizeof sum);
    n = live = 0;
    for (int i = 0; v->army && i < army_count(v->army); i++) {
        const ArmyUnit *u = army_unit(v->army, i);
        if (u->phase == AU_GONE)
            continue;
        live++;
        if (u->phase == AU_HELD)
            continue;
        for (int k = 0; k < 3; k++)
            sum[k] += u->world[12 + k];
        n++;
    }
    s->h = ng_h(H0, live);
    ng_mean(s->v, sum, n);
    for (int i = 0; v->army && i < army_shots(v->army); i++)
        s->v[3] += army_shot(v->army, i)->alive != 0;
    s = &d->s[NDS_BOSS];                             /* the bosses: parts, defeated, the main one's HP */
    live = 0;
    for (int i = 0; v->boss && i < boss_count(v->boss); i++)
        live += boss_part(v->boss, i)->alive != 0;
    s->h = ng_h(ng_h(H0, live), v->boss ? boss_defeated(v->boss) : 0);
    if (v->boss && boss_main(v->boss) >= 0 && boss_main(v->boss) < boss_count(v->boss))
        s->v[0] = boss_part(v->boss, boss_main(v->boss))->hp_frac;
    s = &d->s[NDS_TRAFFIC];                          /* vehicles (where), rail, parade */
    memset(sum, 0, sizeof sum);
    n = live = 0;
    for (int i = 0; v->veh && i < vehicle_count(v->veh); i++) {
        const VehObj *o = vehicle_obj(v->veh, i);
        if (o->phase == VP_GONE)
            continue;
        live++;
        if (o->phase == VP_HELD)
            continue;
        for (int k = 0; k < 3; k++)
            sum[k] += o->world[12 + k];
        n++;
    }
    s->h = ng_h(H0, live);
    ng_mean(s->v, sum, n);
    live = 0;
    for (int i = 0; v->rail && i < rail_count(v->rail); i++)
        live += rail_obj(v->rail, i)->phase != RP_GONE;
    s->h = ng_h(s->h, live);
    live = 0;
    for (int i = 0; v->parade && i < parade_count(v->parade); i++)
        live += parade_obj(v->parade, i)->phase != PAP_GONE;
    s->h = ng_h(s->h, live);
    s = &d->s[NDS_STREET];                           /* the street objects: which are still there */
    s->h = H0;
    for (int i = 0; v->sobj && i < streetobj_count(v->sobj); i++)
        s->h = ng_h(s->h, streetobj_get(v->sobj, i)->alive != 0);
    s = &d->s[NDS_EDIBLES];                          /* the edibles' slots: none left, hidden, out in their window */
    s->h = H0;
    for (int i = 0; v->edibles && i < edible_slots(v->edibles); i++) {
        const EdibleSlot *sl = edible_slot(v->edibles, i);
        const Edible *e = sl->item >= 0 ? edible_get(v->edibles, sl->item) : NULL;
        s->h = ng_h(s->h, sl->kind < 0 || (e && e->phase != EP_WINDOW) ? 0 : e ? 2 : 1);
        s->h = ng_h(s->h, sl->kind);                 /* and which kind: a donut is not a zebra */
    }
    s = &d->s[NDS_CHALLENGE];                        /* the challenge: its counts, met, the upgrade */
    s->h = ng_h(ng_h(ng_h(H0, v->challenge.ngoals), v->challenge.met), v->challenge.up_state);
    for (int g = 0; g < v->challenge.ngoals && g < CHALLENGE_GOALS; g++)
        s->h = ng_h(s->h, v->challenge.goal[g].collected);
    if (getenv("RAMPAGE_NET_TRACE")) {               /* what went into it, for a MISMATCH */
        static double said;
        if (net_now() - said >= 2) {
            said = net_now();
            char line[512];
            int len = 0;
            for (int k = 0; k < NET_DSYS && len < (int)sizeof line - 40; k++)
                len += snprintf(line + len, sizeof line - (size_t)len, " %s %08X", NET_DSYS_NAME[k], d->s[k].h);
            net_log("digest:%s", line);
        }
    }
}

/* ---- snapshots */
typedef struct {
    float pos[3], heading, vel[3], time, hp, energy;
    int16_t state;
    uint8_t mode, mirror, defeated, rage;
    int16_t held[6];                                 /* person, unit, vehicle, parade, rail, street object (-1 none) */
    uint8_t climbing;                                /* mount and hands below are the host's (else at rest) */
    Mount mount;                                     /* the wall it is on (climb.h): plain data, the same build */
    Hand hand[2];                                    /* its hands on the wall */
    int16_t column;
    uint8_t no_gravity;
    uint8_t human, dropping, hidden;                 /* its look: turned human (its streaker the host's person
                                                      * `streaker`), dropping in, not drawn */
    int16_t streaker;
    float scale, look_yaw, look_pitch;               /* the dynamic scale now, the head's angles */
    char face[32];                                   /* the face's expression playing (Expression_*), "" none */
    uint8_t face_head, face_loop;
} NgMon;

/* on a wall, getting on or off one, or a hand still on it: then the mount and the hands go in the snapshot */
static int ng_climbing(const Mon *mo)
{
    return mo->mount.mounted || mo->mount.move_left > 0 || mo->hand[0].valid || mo->hand[1].valid ||
           mo->hand[0].attaching || mo->hand[1].attaching || mo->hand[0].remaining > 0 || mo->hand[1].remaining > 0;
}

/* what a monster holds, and the puppet channel that must be live on the guest for the index to mean the same */
static int *ng_held_field(Mon *mo, int i)
{
    switch (i) {
    case 0: return &mo->held;
    case 1: return &mo->held_unit;
    case 2: return &mo->held_vehicle;
    case 3: return &mo->held_parade;
    case 4: return &mo->held_rail;
    default: return &mo->held_sobj;
    }
}
enum { NG_HELD_PEOPLE, NG_HELD_ARMY, NG_HELD_VEHICLES, NG_HELD_PARADE, NG_HELD_RAIL, NG_HELD_STREET };

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
        for (int i = 0; i < 6; i++)
            net_w_u16(&b, (uint32_t)(uint16_t)*ng_held_field((Mon *)mo, i));
        int climbing = ng_climbing(mo);              /* the climb: the guest's own would let go at other times */
        net_w_u8(&b, (uint32_t)climbing);
        net_w_u16(&b, (uint32_t)(uint16_t)mo->column);
        net_w_u8(&b, (uint32_t)mo->no_gravity);
        if (climbing) {
            net_w_bytes(&b, &mo->mount, (int)sizeof mo->mount);
            net_w_bytes(&b, mo->hand, (int)sizeof mo->hand);
        }
        net_w_u8(&b, (uint32_t)((mo->human ? 1 : 0) | (mo->dropping ? 2 : 0) | (mon_in(mo) && mon_in(mo)->hidden ? 4 : 0)));
        net_w_u16(&b, (uint32_t)(uint16_t)mo->streaker);
        net_w_f32(&b, mo->scale);
        net_w_f32(&b, mo->look_yaw);
        net_w_f32(&b, mo->look_pitch);
        const char *face = mo->face && mo->faces.n > 0 && mo->faces.list[0].name ? mo->faces.list[0].name : "";
        int fl = (int)strlen(face) < 31 ? (int)strlen(face) : 31;
        net_w_u8(&b, (uint32_t)fl);
        net_w_bytes(&b, face, fl);
        net_w_u8(&b, (uint32_t)(fl ? mo->faces.list[0].head : 0));
        net_w_u8(&b, (uint32_t)(fl ? mo->faces.list[0].loop : 0));
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
    GameNetFlow fl;                                  /* the game's flow (the guest's game follows it) */
    game_net_flow_get(v->game, &fl);
    net_w_bytes(&b, &fl, (int)sizeof fl);            /* (both are the same build: the handshake says so) */
    net_w_u8(&b, (uint32_t)(uint8_t)ng_hud_shown);
    net_w_u8(&b, (uint32_t)(v->hud && hud_results_showing(v->hud)));
    if (!b.err)
        net_send_snap(m, b.len);
}

typedef struct {
    int n; NgMon m[MONSTERS]; NetPad pad[2]; int game_state, block; float clock; GameNetSync gs;
    GameNetFlow flow; int hud, results_up;
} NgSnap;

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
        for (int i = 0; i < 6; i++)
            m->held[i] = (int16_t)net_r_u16(&b);
        m->climbing = (uint8_t)net_r_u8(&b);
        m->column = (int16_t)net_r_u16(&b);
        m->no_gravity = (uint8_t)net_r_u8(&b);
        if (m->climbing) {
            net_r_bytes(&b, &m->mount, (int)sizeof m->mount);
            net_r_bytes(&b, m->hand, (int)sizeof m->hand);
        }
        int look = (int)net_r_u8(&b);
        m->human = look & 1;
        m->dropping = look >> 1 & 1;
        m->hidden = look >> 2 & 1;
        m->streaker = (int16_t)net_r_u16(&b);
        m->scale = net_r_f32(&b);
        m->look_yaw = net_r_f32(&b);
        m->look_pitch = net_r_f32(&b);
        int fl = (int)net_r_u8(&b);
        if (fl > 31)
            return 0;
        net_r_bytes(&b, m->face, fl);
        m->face[fl] = 0;
        m->face_head = (uint8_t)net_r_u8(&b);
        m->face_loop = (uint8_t)net_r_u8(&b);
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
    net_r_bytes(&b, &o->flow, (int)sizeof o->flow);
    o->hud = (int)(int8_t)net_r_u8(&b);
    o->results_up = (int)net_r_u8(&b);
    return !b.err;
}

static float ng_lerp_angle(float a, float b, float t)   /* degrees, the short way */
{
    float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    return a + d * t;
}

/* the guest: is that held kind's system a puppet yet (its indices the host's)? */
static int ng_held_live(int i);

/* ---- the game's flow: the host's decides. The guest's game makes no decisions of its own (game_net_follow): every
 * event the host's game hands out (game_net_tap) comes as an NM_EVENT (reliable, in order, with the host's flow as it
 * was then) and the guest's game hands it out at its host time as its own, so main.c's game_step does what the host's
 * did (the block, the cameras, the HUD, the results screens, the revivals). The snapshots carry the flow too (a guest
 * that joined late, a lost moment). Not given to the guest: GE_EXIT (it leaves with the host, netrep_menu.c) */
static double ng_flow_t = -1e30;                     /* the guest: the host time of the flow it last took */
static double ng_results_t = -1e30;                  /* the guest: the host time its results screen was opened at */
static int ng_hud_applied = -1;                      /* the guest: the HUD's fade it last set from the host's */
static char ng_names[64][64];                        /* the guest: the events' names (the host's pointers mean nothing) */
static int ng_nname;

typedef struct NgFlowQ { struct NgFlowQ *next; double t; int len; uint8_t data[]; } NgFlowQ;
static NgFlowQ *ng_flowq, **ng_flowq_tail = &ng_flowq;

static int ng_flow_logged(int type)
{
    return type == GE_STATE || type == GE_RESULTS || type == GE_REVIVE || type == GE_BLOCK || type == GE_TIME_UP ||
           type == GE_EXIT;
}

static void ng_flow_tap(const GameEvent *e)
{
    if (e->type == GE_HUD)
        ng_hud_shown = e->a;
    if (!ng_host() || !g_ngv || !g_ngv->game)
        return;
    uint8_t m[64 + sizeof(GameNetFlow) + 64];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_EVENT);
    net_w_f64(&b, net_now());
    GameNetFlow f;
    game_net_flow_get(g_ngv->game, &f);
    net_w_bytes(&b, &f, (int)sizeof f);
    net_w_u8(&b, (uint32_t)e->type);
    net_w_u8(&b, (uint32_t)(uint8_t)(int8_t)e->slot);
    net_w_u32(&b, (uint32_t)e->a);
    net_w_u32(&b, (uint32_t)e->b);
    int nl = e->name ? (int)strlen(e->name) : 0;
    if (nl > 63)
        nl = 63;
    net_w_u8(&b, (uint32_t)nl);
    net_w_bytes(&b, e->name ? e->name : "", nl);
    for (int i = 0; i < 3; i++)
        net_w_f32(&b, e->pos[i]);
    for (int i = 0; i < 3; i++)
        net_w_f32(&b, e->v[i]);
    net_w_f32(&b, e->f);
    if (!b.err)
        net_send_msg(1, m, b.len);
    if (ng_flow_logged(e->type))
        net_log("flow: sent event %d a %d b %d slot %d %s", e->type, e->a, e->b, e->slot, e->name ? e->name : "");
}

static void ng_on_flow(NetBuf *b)                    /* the guest: one of the host's events, kept till its time */
{
    double t = net_r_f64(b);
    int len = b->cap - b->len;
    if (b->err || len <= 0)
        return;
    NgFlowQ *q = malloc(sizeof *q + (size_t)len);
    if (!q)
        return;
    q->next = NULL;
    q->t = t;
    q->len = len;
    memcpy(q->data, b->p + b->len, (size_t)len);
    *ng_flowq_tail = q;
    ng_flowq_tail = &q->next;
}

static void ng_flow_reset(Viewer *v)
{
    (void)v;
    while (ng_flowq) {
        NgFlowQ *q = ng_flowq;
        ng_flowq = q->next;
        free(q);
    }
    ng_flowq_tail = &ng_flowq;
    ng_hud_applied = -1;
    ng_results_t = -1e30;
}

/* the guest's results screen: down when the host's is (as of host time t), unless it opened since */
static void ng_results_follow(Viewer *v, int host_up, double t)
{
    if (!host_up && v->hud && hud_results_showing(v->hud) && t > ng_results_t + 0.05) {
        net_log("flow: the host's results screen is gone: ours closed");
        hud_results_close(v->hud);
    }
}

static void ng_flow_run(Viewer *v)                   /* the guest: the host's events whose time is shown now */
{
    const NetSnapBuf *mb = net_snaps();
    double shown = mb->have_offset ? net_now() - mb->offset - ng_delay() : 1e30;
    while (ng_flowq && ng_flowq->t <= shown) {
        NgFlowQ *q = ng_flowq;
        ng_flowq = q->next;
        if (!ng_flowq)
            ng_flowq_tail = &ng_flowq;
        NetBuf b = netbuf(q->data, q->len);
        GameNetFlow f;
        GameEvent e;
        memset(&e, 0, sizeof e);
        net_r_bytes(&b, &f, (int)sizeof f);
        e.type = (int)net_r_u8(&b);
        e.slot = (int)(int8_t)net_r_u8(&b);
        e.a = (int)net_r_u32(&b);
        e.b = (int)net_r_u32(&b);
        int nl = (int)net_r_u8(&b);
        char *name = ng_names[ng_nname++ & 63];
        if (nl > 63)
            nl = 63;
        net_r_bytes(&b, name, nl);
        name[nl] = 0;
        e.name = nl ? name : NULL;
        for (int i = 0; i < 3; i++)
            e.pos[i] = net_r_f32(&b);
        for (int i = 0; i < 3; i++)
            e.v[i] = net_r_f32(&b);
        e.f = net_r_f32(&b);
        if (!b.err && v->game) {
            game_net_flow_put(v->game, &f);
            ng_flow_t = q->t;
            if (ng_flow_logged(e.type))
                net_log("flow: host event %d a %d b %d slot %d %s", e.type, e.a, e.b, e.slot, e.name ? e.name : "");
            if (e.type == GE_HUD)
                ng_hud_applied = e.a;
            if (e.type == GE_RESULTS)
                ng_results_t = q->t;
            if (e.type != GE_EXIT)                   /* (the guest leaves with the host: netrep_menu.c) */
                game_net_push(v->game, &e);
        }
        free(q);
    }
}

/* the guest: the flow of a snapshot (host time t), unless an event already gave a newer one */
static void ng_flow_from_snap(Viewer *v, const NgSnap *s, double t)
{
    if (!v->game || t <= ng_flow_t)
        return;
    ng_flow_t = t;
    game_net_flow_put(v->game, &s->flow);
    if (s->hud >= 0 && s->hud != ng_hud_applied) {   /* (a guest that came in after the host's GE_HUD) */
        ng_hud_applied = s->hud;
        for (int k = 0; k < v->nmon; k++)
            if (v->mon[k].hud)
                hud_show(v->mon[k].hud, (float)s->hud);
    }
    ng_results_follow(v, s->results_up, t);
}

/* the guest: a monster's look as the host's (after scene_monster_place, which set its model's matrix afresh) */
static void ng_apply_look(Viewer *v, Mon *mo, const NgMon *hi, const NgMon *lo, float alpha)
{
    Instance *in = mon_in(mo);
    mo->human = hi->human;
    mo->dropping = hi->dropping;
    mo->streaker = hi->streaker;
    if (in)
        in->hidden = hi->hidden;
    float sc = lo ? lo->scale + (hi->scale - lo->scale) * alpha : hi->scale;
    if (!(sc > 0.01f && sc < 100))
        sc = 1;
    mo->scale = mo->scale_to = mo->scale_from = sc;
    mo->scale_left = 0;
    if (sc != 1 && in)                               /* (as mon_step's end scales it) */
        for (int k = 0; k < 11; k++)
            if ((k & 3) != 3)
                in->base.m[k] *= sc;
    mo->look_yaw = mo->head.yaw = hi->look_yaw;
    mo->look_pitch = mo->head.pitch = hi->look_pitch;
    v->scene.mon[mo->slot].look_yaw = hi->mirror ? -hi->look_yaw : hi->look_yaw;
    v->scene.mon[mo->slot].look_pitch = hi->look_pitch;
    const char *now = mo->face && mo->faces.n > 0 && mo->faces.list[0].name ? mo->faces.list[0].name : "";
    if (hi->face[0] && strcmp(now, hi->face))
        face_play(mo, hi->face, hi->face_head, hi->face_loop, 0);
}

static NgSnap ng_last;                               /* the newest the guest read (its pads, its flow) */
static int ng_have_last;

/* the guest: the monsters as the host has them NET_SNAP_DELAY ago */
static void ng_apply_snap(Viewer *v)
{
    const NetSnapSample *a, *b;
    float alpha;
    int r = netsnap_sample(net_snaps(), net_now(), ng_delay(), &a, &b, &alpha);
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
        /* the model too: mon_step placed it where the guest's own step had it (scene_monster_place at its end), and
         * drawn so the monster showed in two places by turns, the guest's step and the host's */
        scene_monster_place(&v->scene, k, mo->mx, mo->my, mo->mz, mo->heading);
        ng_apply_look(v, mo, hi, r == 2 ? &sa.m[k] : NULL, alpha);
        memcpy(mo->vel, hi->vel, sizeof mo->vel);
        if (mo->have_graph && hi->state >= 0 && hi->state < mo->graph.n) {
            if (mo->run.state != hi->state) {
                moves_force(&mo->run, hi->state);
                mo->run.time = hi->time;
            } else if (fabsf(mo->run.time - hi->time) > 0.25f)
                mo->run.time = hi->time;
        }
        if (mo->have_graph && mo->run.state >= 0 && mo->run.state < mo->graph.n) {
            /* the clip on screen is the host's state's: play_state starts a state's clip (scene_monster_play), and
             * moves_force above or the guest's own step may leave another one playing */
            const char *want = mo->graph.states[mo->run.state].clip, *now = v->scene.mon[k].clip;
            if (want && (!now || strcmp(want, now)))
                scene_monster_play(&v->scene, k, want);
        }
        if (mo->mirror != hi->mirror)
            scene_monster_mirror(&v->scene, k, hi->mirror);
        mo->mode = hi->mode;
        mo->mirror = hi->mirror;
        mo->mirror_queued = hi->mirror;
        mo->input.mirrored = hi->mirror;
        mo->defeated = hi->defeated;
        mo->hp = hi->hp;
        mo->power.energy = hi->energy;
        mo->power.rage = hi->rage;
        for (int i = 0; i < 6; i++)                  /* what it holds, once that system is a puppet here */
            if (ng_held_live(i))
                *ng_held_field(mo, i) = hi->held[i];
        mo->column = hi->column;                     /* the climb as the host's: the wall, the spot, the hands */
        mo->no_gravity = hi->no_gravity;
        if (hi->climbing) {
            mo->mount = hi->mount;
            memcpy(mo->hand, hi->hand, sizeof mo->hand);
        } else if (ng_climbing(mo)) {                /* off the wall on the host: let go here too */
            mo->mount.mounted = 0;
            mo->mount.move_left = 0;
            mo->mount.at.building = -1;
            for (int h = 0; h < 2; h++) {
                mo->hand[h].valid = mo->hand[h].attaching = mo->hand[h].planted = 0;
                mo->hand[h].remaining = mo->hand[h].target = 0;
            }
        }
    }
    const NetSnapSample *n = netsnap_newest(net_snaps());
    ng_have_last = n && ng_read_snap(n, &ng_last);
    if (r == 2 || r == 1) {                          /* the host's flow, clock and score, as of the sample shown */
        const NgSnap *at = r == 2 ? &sa : &sb;
        double t = r == 2 ? a->t : b->t;
        ng_flow_from_snap(v, at, t);
        game_net_put(v->game, &sb.gs, v->nmon < 2 ? v->nmon : 2);
    }
    if (ng_have_last && ng_last.block != v->block) {
        static int warned = -1;
        if (warned != ng_last.block)
            net_log("the host is in block %d, we are in %d", ng_last.block, v->block);
        warned = ng_last.block;
    }
}

/* the guest, every NG_DIGEST_EVERY: its digest as shown now, with the host time it shows (the snapshots' delay
 * behind) and the newest it has (the buildings') */
static void ng_digest_send(Viewer *v)
{
    const NetSnapBuf *mb = net_snaps();
    const NetSnapSample *n = netsnap_newest(mb);
    if (!n || !mb->have_offset || net_now() - ng_digest_at < NG_DIGEST_EVERY)
        return;
    ng_digest_at = net_now();
    NetDigest d;
    ng_digest(v, &d);
    net_send_digest(net_now() - mb->offset - ng_delay(), n->t, &d);
}

/* ---- puppets: the systems the host runs and the guest only shows (netrep_*.c fill them in). Each channel has its
 * snapshots (NM_PUPPET, unreliable, at its rate, into its own buffer and so shown NET_SNAP_DELAY behind as the
 * monsters are) and its events (NM_PEVENT, reliable and in order: effects, sounds, spawns, played on the guest when its
 * shown time reaches the host time they were sent at). The guest's own system makes no decisions: its step in main.c
 * asks net_guest_world() and leaves its logic out. */
enum { NGP_VEHICLES, NGP_STREET, NGP_RAIL, NGP_PARADE, NGP_ARMY, NGP_BOSS, NGP_PEOPLE, NGP_EDIBLES, NGP_N };
typedef struct {
    const char *name;
    float rate;                                      /* snapshots a second (0: events only) */
    int (*write)(Viewer *v, NetBuf *b);              /* host: the system's state into b; 0 nothing to send */
    /* guest, every frame after the step: how 2 between a and b by alpha (0 .. 1), how 1 b alone (the newest, alpha
     * the seconds past it, at most NET_SNAP_EXTRAP) */
    void (*apply)(Viewer *v, const NetSnapSample *a, const NetSnapSample *b, float alpha, int how);
    void (*event)(Viewer *v, NetBuf *b);             /* guest: one of the host's events (the bytes after its header) */
    void (*reset)(Viewer *v);                        /* both: a new world or block began */
} NgPuppet;

static NgPuppet ng_pup[NGP_N];
static NetSnapBuf ng_pup_snaps[NGP_N];
static double ng_pup_at[NGP_N];

static int ng_guest(void) { return net_role() == NET_GUEST && net_live(); }
static int ng_host(void) { return net_role() == NET_HOST && net_live(); }

/* the host: an event of channel ch, now (reliable, in order with every other channel's) */
static void ng_event(int ch, const void *data, int len)
{
    if (!ng_host() || len < 0 || len > 4000)
        return;
    uint8_t m[4096 + 16];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_PEVENT);
    net_w_u8(&b, (uint32_t)ch);
    net_w_f64(&b, net_now());
    net_w_bytes(&b, data, len);
    if (!b.err)
        net_send_msg(1, m, b.len);
}

/* the guest's queue of events not yet due */
typedef struct NgQueued { struct NgQueued *next; double t; int ch, len; uint8_t data[]; } NgQueued;
static NgQueued *ng_queue, **ng_queue_tail = &ng_queue;

static void ng_queue_clear(void)
{
    while (ng_queue) {
        NgQueued *q = ng_queue;
        ng_queue = q->next;
        free(q);
    }
    ng_queue_tail = &ng_queue;
}

static void ng_queue_push(int ch, double t, const uint8_t *data, int len)
{
    NgQueued *q = malloc(sizeof *q + (size_t)len);
    if (!q)
        return;
    q->next = NULL;
    q->t = t;
    q->ch = ch;
    q->len = len;
    memcpy(q->data, data, (size_t)len);
    *ng_queue_tail = q;
    ng_queue_tail = &q->next;
}

static void ng_queue_run(Viewer *v)                  /* the events whose host time is shown now */
{
    const NetSnapBuf *mb = net_snaps();
    double shown = mb->have_offset ? net_now() - mb->offset - ng_delay() : 1e30;
    while (ng_queue && ng_queue->t <= shown) {
        NgQueued *q = ng_queue;
        ng_queue = q->next;
        if (!ng_queue)
            ng_queue_tail = &ng_queue;
        if (q->ch >= 0 && q->ch < NGP_N && ng_pup[q->ch].event) {
            NetBuf b = netbuf(q->data, q->len);
            ng_pup[q->ch].event(v, &b);
        }
        free(q);
    }
}

static void ng_puppets_reset(Viewer *v)
{
    ng_queue_clear();
    for (int c = 0; c < NGP_N; c++) {
        netsnap_clear(&ng_pup_snaps[c]);
        ng_pup_at[c] = 0;
        if (ng_pup[c].reset)
            ng_pup[c].reset(v);
    }
}

static void ng_puppets_send(Viewer *v)
{
    static uint8_t m[NET_SNAP_MAX];
    for (int c = 0; c < NGP_N; c++) {
        float rate = ng_pup[c].rate * ng_rate / 20.0f;   /* (its share at 20 a second) */
        if (!ng_pup[c].write || rate <= 0 || net_now() - ng_pup_at[c] < 1.0 / rate - 0.002)
            continue;
        ng_pup_at[c] = net_now();
        NetBuf b = netbuf(m, sizeof m);
        net_w_u8(&b, NM_PUPPET);
        net_w_u8(&b, (uint32_t)c);
        net_w_f64(&b, net_now());
        int hdr = b.len;
        if (ng_pup[c].write(v, &b) && !b.err && b.len > hdr)
            net_send_msg(0, m, b.len);
        else if (b.err)
            net_log("%s: snapshot too big (over %d bytes)", ng_pup[c].name, NET_SNAP_MAX);
    }
}

static void ng_puppets_apply(Viewer *v)
{
    ng_queue_run(v);
    for (int c = 0; c < NGP_N; c++) {
        if (!ng_pup[c].apply)
            continue;
        const NetSnapSample *a, *b;
        float alpha;
        int how = netsnap_sample(&ng_pup_snaps[c], net_now(), ng_delay(), &a, &b, &alpha);
        if (how)
            ng_pup[c].apply(v, a, b, alpha, how);
    }
}

static void ng_on_puppet(NetBuf *b, int type)
{
    int ch = (int)net_r_u8(b);
    double t = net_r_f64(b);
    if (b->err || ch < 0 || ch >= NGP_N)
        return;
    if (type == NM_PUPPET)
        netsnap_push(&ng_pup_snaps[ch], t, net_now(), b->p + b->len, b->cap - b->len);
    else
        ng_queue_push(ch, t, b->p + b->len, b->cap - b->len);
}

static int ng_held_live(int i)
{
    static const int CH[6] = { NGP_PEOPLE, NGP_ARMY, NGP_VEHICLES, NGP_PARADE, NGP_RAIL, NGP_STREET };
    return i >= 0 && i < 6 && (ng_pup[CH[i]].apply || ng_pup[CH[i]].event);
}

/* each system's side, one file each */
#include "netrep_traffic.c"                          /* vehicles, street objects, rail, parade */
#include "netrep_army.c"                             /* the army and the bosses */
#include "netrep_people.c"                           /* people and edibles */
#include "netrep_menu.c"                             /* the menus online */

/* ---- the hooks main.c calls */

/* at the start: --host [port], --join ip[:port]; the panel's window */
static void netgame_init(Viewer *v, int argc, char **argv, void *hwnd, int hidden)
{
    g_ngv = v;
    net_set_handler(ng_handler);
    ngr_traffic_init();
    ngr_army_init();
    ngr_people_init();
    ngr_menu_init();
    nts_command_line(argc, argv);                    /* +connect_lobby <id>: started by a Steam invite */
    FILE *appid = fopen("steam_appid.txt", "rb");    /* (Steam opted in: steam_appid.txt beside the game, or an
                                                      * invite; else the panel's Use Steam) */
    int lobby = 0;
    for (int i = 1; i < argc; i++)
        lobby |= !strcmp(argv[i], "+connect_lobby");
    if (appid || lobby)
        nts_start();
    if (appid)
        fclose(appid);
    building_net_gate = ng_building_gate;
    game_net_tap = ng_flow_tap;
    hud_net_tap = ng_results_tap;
    const char *rate = getenv("RAMPAGE_NET_RATE");
    if (rate)
        netgame_set_rate(atoi(rate));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--net-rate") && i + 1 < argc)
            netgame_set_rate(atoi(argv[++i]));
        else if (!strcmp(argv[i], "--host"))
            net_start_host(i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9' ? atoi(argv[++i]) : NET_PORT);
        else if (!strcmp(argv[i], "--join") && i + 1 < argc)
            net_start_join(argv[++i]);
        else if (!strcmp(argv[i], "--steam-host"))
            net_start_host_steam();
    }
    if (!hidden && netui_init(hwnd) != 0)
        printf("net: the panel (Dear ImGui) did not start\n");
}

/* once a frame, before the input: the connection, the world, the guest following the host */
static void netgame_plain_title(const Viewer *v, Window *w);

static void netgame_frame(Viewer *v, Window *w, double now)
{
    net_update(now);
    if (!DEBUG_TITLE)                                /* (the developer build's title says it every half second) */
        netgame_plain_title(v, w);
    if (net_role() == NET_OFF) {
        ngr_menu_frame(v);                           /* (a guest that left the host's game: its menus again) */
        return;
    }
    ng_set_world(v);
    edible_net_no_spawn = ng_guest();                /* the guest's edibles: the host's alone (netrep_people.c) */
    if (net_role() == NET_GUEST && net_state() == NETS_CONNECTED && net_host_world() &&
        net_host_world() != net_world())
        ng_follow(v);
    ngr_menu_frame(v);                               /* the menus: the host's state told, the guest back to its own */
    if (netui_wants_input()) {                       /* the panel has the keys */
        memset(w->keys, 0, sizeof w->keys);
        memset(w->pressed, 0, sizeof w->pressed);
    }
}

/* the players' pads are set (keys_pad / pad_to_pad): the host's player 2 is the guest's pad; the guest sends its own
 * (player 1's keys and pad 0) and plays it on its player 2 at once, player 1 by the host's */
static void netgame_pads(Viewer *v, const Window *w)
{
    if (v->game && game_net_following(v->game) != ng_guest()) {   /* the guest's game follows the host's */
        net_log("flow: our game %s", ng_guest() ? "follows the host's" : "is our own again");
        game_net_follow(v->game, ng_guest());
    }
    if (net_role() == NET_HOST && v->play && have_monster(v) && v->have_scene && mon_in(&v->mon[0])) {
        static const char *hostdo;                   /* RAMPAGE_NET_HOSTDO (tools/net_test.ps1 -HostDo): --do's words
                                                      * played by the host's monster, guest or not (a late join's test) */
        static int hostdo_read;
        if (!hostdo_read) {
            hostdo_read = 1;
            hostdo = getenv("RAMPAGE_NET_HOSTDO");
        }
        if (hostdo && *hostdo)
            hostdo = play_script(v, hostdo, 0.75f);
    }
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
    if (getenv("RAMPAGE_NET_TESTPAD")) {             /* tools/net_test.ps1 -TestPad: up to the buildings 1.5 s, punch
                                                      * and kick 4.5 s, along the street 2 s (left and right in turn) */
        static double t0;
        static int was;
        if (!t0)
            t0 = net_now();
        double t = net_now() - t0, c = fmod(t, 8.0);
        memset(&mine, 0, sizeof mine);
        int on = 0, btn = fmod(t, 1.8) < 0.6 ? MV_SQUARE : fmod(t, 1.8) < 1.2 ? MV_TRIANGLE : MV_CIRCLE;   /* (circle:
                                                      * a window person grabbed, eaten) */
        if (c < 1.5)
            mine.ly = -1;
        else if (c < 6.0)
            on = fmod(t, 0.6) < 0.1;
        else {
            mine.lx = fmod(t, 16.0) < 8.0 ? 1.0f : -1.0f;
            mine.ly = fmod(t, 24.0) < 8.0 ? 1.0f : 0.0f;  /* (down a building now and then: its windows again) */
        }
        mine.held[btn] = (uint8_t)on;
        mine.pressed[btn] = (uint8_t)(on && !was);
        was = on;
    }
    if (ngm_host_paused())                           /* the host's Pause menu is up: the guest's pad is its (NM_MENU) */
        memset(&mine, 0, sizeof mine);
    NetPad np;
    ng_pad_out(&mine, &np);
    net_send_pad(&np);
    v->mon[1].pad = mine;
    if (ng_have_last)
        ng_pad_in(&ng_last.pad[0], &v->mon[0].pad, 0);
    else
        memset(&v->mon[0].pad, 0, sizeof v->mon[0].pad);
}

/* the game's flow as this copy has it, logged as it changes (both sides: build\nettest\host.out / guest.out) */
static void ng_flow_log(Viewer *v)
{
    static int was_state = -1, was_results = -1, was_result = -1, was_lives[2] = { -1, -1 };
    static int was_def[MONSTERS], was_human[MONSTERS], was_streaker[MONSTERS], was_hidden[MONSTERS];
    if (!v->game)
        return;
    int s = game_state(v->game);
    if (s != was_state)
        net_log("flow: state %d (block %d)", s, v->block);
    was_state = s;
    int res = v->hud && hud_results_showing(v->hud), r = game_result_state(v->game);
    if (res != was_results)
        net_log("flow: results screen %s", res ? "up" : "down");
    if (r != was_result && r)
        net_log("flow: result answered %d (state %d)", r, s);
    was_results = res;
    was_result = r;
    for (int k = 0; k < v->nmon && k < MONSTERS; k++) {
        const Mon *mo = &v->mon[k];
        int hidden = mon_in(mo) && mon_in(mo)->hidden;
        if (mo->defeated != was_def[k] || mo->human != was_human[k] || (mo->streaker >= 0) != was_streaker[k] ||
            hidden != was_hidden[k])
            net_log("flow: monster %d %s%s%s%s hp %.0f at %.1f %.1f %.1f", k, mo->defeated ? "defeated" : "up",
                    mo->human ? ", human" : "", mo->streaker >= 0 ? ", streaker" : "", hidden ? ", hidden" : "",
                    mo->hp, mo->mx, mo->my, mo->mz);
        was_def[k] = mo->defeated;
        was_human[k] = mo->human;
        was_streaker[k] = mo->streaker >= 0;
        was_hidden[k] = hidden;
        if (k < 2 && game_lives(v->game, k) != was_lives[k]) {
            net_log("flow: player %d lives %d", k + 1, game_lives(v->game, k));
            was_lives[k] = game_lives(v->game, k);
        }
    }
}

/* the test's deaths (RAMPAGE_NET_TESTDIE=period[,lives], the host alone; never set in play): every `period` seconds
 * of play both monsters lose all their HP; lives, if given, are each player's from the first */
static void ng_test_end(Viewer *v)
{
    /* RAMPAGE_NET_TESTEND=win,secs | time,secs (the host alone; never set in play): `secs` into each block's play every
     * building is brought down (building_destroy: the guest's made by NM_BUILDING), or the clock is run out */
    static const char *spec;
    static int read, was_state = -1, done;
    static double at;
    if (!read) {
        spec = getenv("RAMPAGE_NET_TESTEND");
        read = 1;
    }
    if (!spec || !v->game)
        return;
    int s = game_state(v->game);
    if (s != was_state && s == GS_PLAY) {
        const char *c = strchr(spec, ',');
        at = net_now() + (c ? atof(c + 1) : 10);
        done = 0;
    }
    was_state = s;
    if (s != GS_PLAY || done || net_now() < at)
        return;
    done = 1;
    if (!strncmp(spec, "win", 3)) {
        net_log("test: every building brought down");
        for (uint32_t i = 0; i < v->scene.nbuildings; i++)
            if (v->scene.buildings[i].b.state == BUILDING_STANDING)
                building_destroy(&v->scene.buildings[i].b);
    } else {
        GameNetSync gs;
        game_net_get(v->game, &gs);
        gs.elapsed = 100000;
        net_log("test: the clock run out");
        game_net_put(v->game, &gs, v->nmon < 2 ? v->nmon : 2);
    }
}

static void ng_test_die(Viewer *v)
{
    ng_test_end(v);
    static const char *spec;
    static int read;
    static double next;
    static int lives_set;
    if (!read) {
        spec = getenv("RAMPAGE_NET_TESTDIE");
        read = 1;
    }
    if (!spec || !v->game || game_state(v->game) != GS_PLAY)
        return;
    float period = (float)atof(spec);
    const char *c = strchr(spec, ',');
    if (period < 1)
        period = 1;
    if (!lives_set && c) {
        GameNetSync s;
        game_net_get(v->game, &s);
        for (int k = 0; k < 2; k++)
            s.lives[k] = atoi(c + 1);
        game_net_put(v->game, &s, 2);
        lives_set = 1;
        net_log("test: lives %d each", atoi(c + 1));
    }
    if (!next)
        next = net_now() + period;
    if (net_now() < next)
        return;
    next = net_now() + period;
    for (int k = 0; k < v->nmon && k < 2; k++) {
        Mon *mo = &v->mon[k];
        if (mo->defeated || mo->human || mo->dropping || mo->hp <= 0)
            continue;
        float d[2] = { 0, 1 };
        net_log("test: monster %d felled", k);
        monster_take(mo, "test", mo->hp + 1, 0, -1, d, 0);
    }
}

/* after the world's step, before the monsters are posed: the host's snapshot, or the guest's taking it */
static void netgame_after_step(Viewer *v)
{
    if (!net_live())
        return;
    ng_flow_log(v);
    if (net_role() == NET_HOST) {
        ng_test_die(v);
        ng_building_flush();
        ng_state_send(v);                            /* (after the frame's building changes: they are in it) */
        if (net_now() - ng_snap_at >= 1.0 / ng_rate - 0.002) {   /* (2 ms early: a 60 Hz frame's jitter) */
            ng_snap_at = net_now();
            ng_send_snap(v);
            NetDigest d;
            ng_digest(v, &d);
            net_keep_digest(net_now(), &d);
        }
        ng_puppets_send(v);
    } else {
        ng_state_ask(v);
        ng_apply_snap(v);
        ng_flow_run(v);
        ng_puppets_apply(v);
        ng_digest_send(v);
    }
}

/* the window title's start while online: who this window is ("HOST (player 1, George) "), else "" */
static const char *netgame_title(const Viewer *v)
{
    static char t[96];
    int role = net_role();
    if (role == NET_OFF)
        return "";
    int k = role == NET_HOST ? 0 : 1;
    const char *name = k < v->nmon && v->mon[k].monster >= 0 ? v->monsters[v->mon[k].monster] : NULL;
    snprintf(t, sizeof t, "%s (player %d%s%s) ", role == NET_HOST ? "HOST" : "GUEST", k + 1, name ? ", " : "",
             name ? name : "");
    return t;
}

/* the player build's plain title (no DEBUG_TITLE): set when who this window is changes */
static void netgame_plain_title(const Viewer *v, Window *w)
{
    static char was[96] = "-";
    const char *t = netgame_title(v);
    if (strcmp(t, was)) {
        char title[128];
        snprintf(title, sizeof title, "RampageRE%s%s", *t ? " - " : "", t);
        win_title(w, title);
        snprintf(was, sizeof was, "%s", t);
    }
}

/* just before the swap: the panel */
static void netgame_draw(void)
{
    ngr_menu_draw(g_ngv);                            /* the other player's menus, told */
    netui_frame();
}


/* netrep_menu.c - online play: the menus (included by netgame.c). NM_MENU messages, both ways:
 *   input    guest -> host, reliable, the frames it pressed something: its menu presses (its keys, keypad and pads,
 *            any of them), given to the host's front end as pad 1's, as a couch player 2's would be (the shell's own
 *            rules say what pad 1 may do: nothing in the main menu, its own card on Monster Select ...); in a game, to
 *            the host's Pause menu and results screens the same way, and its Start opens the host's Pause menu for pad 1
 *   state    host -> guest, reliable, on change and again to each guest that comes in: where the host is (its front
 *            end's screen as shell_net_view sees it: the main menu's box and lists, Monster Select's cards and readies,
 *            City Select's city; in a game paused or on a results screen)
 *   results  host -> guest, reliable: a press the host's results screen or Pause menu took (its own or the guest's)
 *   resend   guest -> host: "your results screen is up and mine is not" (a guest that came in while it was up); the
 *            host sends the results event again and every press its screen took since
 *   progress host -> guest, reliable: a campaign block won (the city, its flags, the next block, destroyed or not),
 *            for the guest's own profile
 *
 * The guest's front end shows the host's: each frame its own shell is made the host's (shell_net_mirror,
 * src/netshell.c), so the guest sees the host's main menu, both cards on Monster Select, the city the host turns the
 * globe to, drawn by its own shell with its own sounds; its presses are the host's (pad 1's), none go to its own shell
 * (but its own profile popup's: the guest picks or makes its own profile at the boot) and there is no attract mode.
 * What the mirror cannot show (the host's profile popup, the title screen, the loading screen) is told in words over
 * it (ngr_menu_draw). The host's game starting takes the guest out of it (ng_follow); the host's leaving its game (to
 * its menus, or the connection gone) brings the guest back to its menus. In the host's game the guest's Pause menu
 * opens and closes with the host's and moves as it does (its Options page stays the host's own); its results screens
 * are the host's (netgame.c's flow opens them) and take every press the host's take.
 *
 * The guest's profile: the host's game runs on the host's profile, and the guest's game, following it, changes its
 * own copy in ways that mean nothing for the guest (the host's city and checkpoint). So the guest's profile is kept
 * aside as the online game starts (ngm_prof) and given back when it ends, with what the guest earned in it merged in
 * (ngm_keep, docs/NETPLAY.md "The guest's profile"): every monster unlocked in front of it, the upgrades its own
 * monster took, and, in the campaign, each block won (the host's city opened, its stars, its next block, the next
 * city when it fell). Nothing of the host's profile itself (its other cities, its settings, its name) reaches it.
 * It is written then, at each block won, as the guest's own autosave would (allow_save: a profile loaded or made). */

enum { NGM_INPUT = 1, NGM_STATE = 2, NGM_RESULTS = 3, NGM_RESEND = 4, NGM_PROGRESS = 5 };
#define NGM_EVENTS 32
#define NGM_KEPT 64                                  /* the presses a results screen took, kept for a late guest */

typedef struct {
    uint8_t where;                                   /* 0 neither, 1 the front end, 2 a game */
    uint8_t mm_state, mm_players, ms_players, ms_ready, cs_locked;
    uint8_t paused;                                  /* 0 no, 1 by the host, 2 by the guest (its Start) */
    uint8_t results, world;                          /* a results screen is up; the host's game counts (2 players) */
    char focus[16], mon[2][24], city[24];
    ShellNetView view;                               /* the front end, for the guest's own (shell_net_mirror) */
} NgMenuState;

static NgMenuState ngm_host;                         /* guest: the host's, as last told */
static int ngm_have_host;
static int ngm_pause_by_guest;                       /* host: the Pause menu up is pad 1's */
static int ngm_in_host_game;                         /* guest: in a game ng_follow loaded */
static int ngm_auto;                                 /* guest: its HUD's results answer by themselves */
static Profile ngm_prof;                             /* guest: its own profile, the online game's earnings in it */
static ShellGame ngm_last_game;
static uint8_t ngm_ev[NGM_EVENTS][3];                /* guest: this frame's presses (ev low, high, button) */
static int ngm_nev;
static int ngm_mirrored;                             /* guest: its front end is the host's */
/* host: the results event of the screen up and the presses it took since (a late guest's) */
static uint8_t ngm_res_msg[256];
static int ngm_res_len;
static uint8_t ngm_res_press[NGM_KEPT][4];
static int ngm_res_n;
static int ngm_res_won;                              /* host: that screen is a campaign block won's (GR_SCREEN_RESULTS) */
/* guest: the host's results presses that came before its own screen was up, taken one a frame once it is */
static uint8_t ngm_pend[NGM_KEPT][4];
static int ngm_npend;
static double ngm_resend_at = -1e30;
static int ngm_got_progress, ngm_res_was;            /* guest: a block won told in this game; its results screen up */

static void ngr_menu_init(void)
{
    memset(&ngm_host, 0, sizeof ngm_host);
}

static int ngm_guest_on(void) { return net_role() == NET_GUEST && net_state() == NETS_CONNECTED; }
static int ngm_host_on(void) { return net_role() == NET_HOST && net_state() == NETS_CONNECTED; }

/* ---- the guest's presses */
static void ngm_press(int ev, int button)
{
    if (ngm_nev >= NGM_EVENTS)
        return;
    ngm_ev[ngm_nev][0] = (uint8_t)(ev & 255);
    ngm_ev[ngm_nev][1] = (uint8_t)(ev >> 8);
    ngm_ev[ngm_nev][2] = (uint8_t)button;
    ngm_nev++;
}

static void ngm_flush(void)
{
    if (!ngm_nev)
        return;
    uint8_t m[4 + NGM_EVENTS * 3];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_MENU);
    net_w_u8(&b, NGM_INPUT);
    net_w_u8(&b, (uint32_t)ngm_nev);
    for (int i = 0; i < ngm_nev; i++) {
        net_w_u16(&b, (uint32_t)(ngm_ev[i][0] | ngm_ev[i][1] << 8));
        net_w_u8(&b, ngm_ev[i][2]);
    }
    if (!b.err)
        net_send_msg(1, m, b.len);
    ngm_nev = 0;
}

/* the guest's keys and pads as menu presses (shell_frame's keys: Enter / Space accept, Backspace back, the arrows,
 * P start, C / V / R buttons; the keypad's; every controller's); only Start unless all */
static void ngm_collect(Viewer *v, const Window *w, float dt, int all)
{
    static const struct { int key, ev, button; } KEYS[] = {
        { VK_RETURN, HUD_EV_ACCEPT, 0 }, { VK_SPACE, HUD_EV_ACCEPT, 0 }, { VK_BACK, HUD_EV_BACK, 0 },
        { VK_UP, HUD_EV_UP, 0 }, { VK_DOWN, HUD_EV_DOWN, 0 }, { VK_LEFT, HUD_EV_LEFT, 0 }, { VK_RIGHT, HUD_EV_RIGHT, 0 },
        { 'P', HUD_EV_START, 0 }, { 'C', HUD_EV_BUTTON, 0 }, { 'V', HUD_EV_BUTTON, 1 }, { 'R', HUD_EV_BUTTON, 4 },
        { VK_NUMPAD0, HUD_EV_ACCEPT, 0 }, { VK_NUMPAD1, HUD_EV_BACK, 0 }, { VK_NUMPAD8, HUD_EV_UP, 0 },
        { VK_NUMPAD2, HUD_EV_DOWN, 0 }, { VK_NUMPAD4, HUD_EV_LEFT, 0 }, { VK_NUMPAD6, HUD_EV_RIGHT, 0 },
        { VK_NUMPAD7, HUD_EV_BUTTON, 0 }, { VK_NUMPAD9, HUD_EV_BUTTON, 1 }, { VK_NUMPAD3, HUD_EV_BUTTON, 4 } };
    for (size_t k = 0; k < sizeof KEYS / sizeof *KEYS; k++)
        if (w->pressed[KEYS[k].key] && (all || KEYS[k].ev == HUD_EV_START))
            ngm_press(KEYS[k].ev, KEYS[k].button);
    if (!all && w->pressed[VK_RETURN])               /* (in play Enter is Start, as P) */
        ngm_press(HUD_EV_START, 0);
    for (int k = 0; k < PAD_SLOTS; k++) {
        int m = pad_menu(v, k, dt);
        for (size_t i = 0; m && i < sizeof PAD_MENU_EV / sizeof *PAD_MENU_EV; i++)
            if ((m & PAD_MENU_EV[i].bit) && (all || PAD_MENU_EV[i].ev == HUD_EV_START))
                ngm_press(PAD_MENU_EV[i].ev, PAD_MENU_EV[i].button);
    }
}

/* shell_frame's hook: online, the guest's presses go to the host's front end, none to its own (1); but its own
 * profile popup (the boot's, or a profile to pick or make) is its own to answer */
static int netgame_shell_input(Viewer *v, const Window *w, float dt)
{
    if (!ngm_guest_on() || !v->shell || shell_popup_active(v->shell))
        return 0;
    ngm_collect(v, w, dt, 1);
    ngm_flush();
    shell_pad_held(v->shell, 0, 0);
    return 1;
}

/* the main loop's hook before a game's menus take the pad: in the host's game the guest's Pause menu and results
 * screens are the host's (1: its own take nothing) */
static int netgame_game_input(Viewer *v, const Window *w, float dt)
{
    if (!ngm_guest_on() || !ngm_in_host_game || v->shell || !v->hud)
        return 0;
    int menu = ngm_have_host && ngm_host.where == 2 && (ngm_host.paused || ngm_host.results);
    ngm_collect(v, w, dt, menu);
    ngm_flush();
    return 1;
}

/* the guest's own pad in play: still while the host's Pause menu is up */
static int ngm_host_paused(void) { return ngm_guest_on() && ngm_have_host && ngm_host.where == 2 && ngm_host.paused; }

/* the host: one of the guest's presses, pad 1's */
static void ngm_apply(Viewer *v, int ev, int button)
{
    if (v->shell) {
        shell_input_pad(v->shell, ev, button, 1);
        return;
    }
    if (!v->hud || !v->game)
        return;
    if (hud_pause_showing(v->hud) || hud_results_showing(v->hud)) {
        if (!shell_popup_active(v->popup))           /* (the SaveGame popup answers player 1) */
            hud_input_pad(v->hud, ev, button, 1);
    } else if (ev == HUD_EV_START && v->play && !v->attract && pause_open(v, 1))
        ngm_pause_by_guest = 1;
}

/* ---- the host's state */
static void ngm_state_of(Viewer *v, NgMenuState *s)
{
    memset(s, 0, sizeof *s);
    if (v->shell) {
        s->where = 1;
        snprintf(s->focus, sizeof s->focus, "%s", shell_focus(v->shell) ? shell_focus(v->shell) : "");
        ShellAutoView av;
        shell_auto_view(v->shell, &av);
        s->mm_state = (uint8_t)av.mm_state;
        s->mm_players = (uint8_t)(av.mm_players + 1);
        s->ms_players = (uint8_t)av.ms_players;
        s->ms_ready = (uint8_t)av.ms_ready;
        s->cs_locked = (uint8_t)av.cs_locked;
        if (!strcmp(s->focus, "MonsterSelect") || !strcmp(s->focus, "MonsterSheet"))
            for (int k = 0; k < 2; k++)
                snprintf(s->mon[k], sizeof s->mon[k], "%s", av.ms_name[k] ? av.ms_name[k] : "");
        if (!strcmp(s->focus, "CitySelect") && av.cs_index >= 0 && av.cs_index < v->prof.ncities &&
            v->prof.city_name[av.cs_index])
            snprintf(s->city, sizeof s->city, "%s", v->prof.city_name[av.cs_index]);
        shell_net_view(v->shell, &s->view);
    } else if (v->game) {
        s->where = 2;
        s->paused = (uint8_t)(v->hud && hud_pause_showing(v->hud) ? (ngm_pause_by_guest ? 2 : 1) : 0);
        s->results = (uint8_t)(v->hud && hud_results_showing(v->hud));
    }
    s->world = net_world() != 0;
}

static void ngm_send_state(Viewer *v, int again)
{
    static NgMenuState sent;
    static int was_on;
    NgMenuState s;
    ngm_state_of(v, &s);
    if (again)                                       /* (a guest that just came in: told at once) */
        was_on = 0;
    if (was_on && !memcmp(&s, &sent, sizeof s))
        return;
    was_on = 1;
    sent = s;
    uint8_t m[4 + sizeof s];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_MENU);
    net_w_u8(&b, NGM_STATE);
    net_w_bytes(&b, &s, (int)sizeof s);              /* (both are the same build: the handshake says so) */
    if (!b.err)
        net_send_msg(1, m, b.len);
}

static void ngm_send_press(int ev, int button, int pad)
{
    uint8_t m[8];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_MENU);
    net_w_u8(&b, NGM_RESULTS);
    net_w_u16(&b, (uint32_t)ev);
    net_w_u8(&b, (uint32_t)button);
    net_w_u8(&b, (uint32_t)pad);
    if (!b.err)
        net_send_msg(1, m, b.len);
}

/* the host: a press its results screen or Pause menu takes (hud_net_tap), for the guest's to take too */
static void ng_results_tap(Hud *h, int ev, int button, int pad)
{
    if (net_role() != NET_HOST || !g_ngv || h != g_ngv->hud || !net_world())
        return;
    if (hud_results_showing(h) && !hud_pause_showing(h) && ngm_res_len && ngm_res_n < NGM_KEPT) {
        uint8_t *k = ngm_res_press[ngm_res_n++];     /* (kept for a guest that comes in later) */
        k[0] = (uint8_t)(ev & 255); k[1] = (uint8_t)(ev >> 8); k[2] = (uint8_t)button; k[3] = (uint8_t)pad;
    }
    if (!ngm_host_on())
        return;
    ngm_send_press(ev, button, pad);
    net_log("flow: %s press %d (pad %d) sent", hud_pause_showing(h) ? "pause" : "results", ev, pad);
}

/* the host: its results screen's event as sent (ng_flow_tap), kept while the screen is up */
static void ngm_results_sent(const uint8_t *m, int len)
{
    if (len > (int)sizeof ngm_res_msg)
        return;
    memcpy(ngm_res_msg, m, (size_t)len);
    ngm_res_len = len;
    ngm_res_n = ngm_res_won = 0;
}

/* the guest: the host's snapshot (host time t) says its results screen is up, and ours is not, with no results event
 * of the host's in the last second (a guest that came in while it was up): ask for it */
static void ngm_results_missing(Viewer *v, double t)
{
    if (!ngm_guest_on() || !ngm_in_host_game || !v->hud || hud_results_showing(v->hud) || t < ng_results_t + 1.0 ||
        net_now() < ngm_resend_at + 2.0)
        return;
    ngm_resend_at = net_now();
    uint8_t m[3] = { NM_MENU, NGM_RESEND, 0 };
    net_send_msg(1, m, 3);
    net_log("flow: the host's results screen is up and ours is not: asked for it");
}

/* the host: a campaign block won (its results screen opens), the guest's profile told what it earned */
static void ngm_progress_send(Viewer *v)
{
    ngm_res_won = v->game && game_play_type(v->game) == GP_CAMPAIGN;
    if (!ngm_host_on() || !ngm_res_won || v->city_index < 0 || v->city_index >= NCITIES)
        return;
    uint8_t m[64];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_MENU);
    net_w_u8(&b, NGM_PROGRESS);
    const char *city = CITIES[v->city_index];
    int nl = (int)strlen(city);
    net_w_u8(&b, (uint32_t)nl);
    net_w_bytes(&b, city, nl);
    net_w_u32(&b, (uint32_t)(game_city_flags(v->game) | (v->challenges_lost ? 0 : CITY_FLAG_CHALLENGES) | PC_AVAILABLE));
    net_w_u32(&b, (uint32_t)game_block(v->game, NULL));
    net_w_u8(&b, (uint32_t)(game_city_destroyed(v->game) != 0));
    if (!b.err)
        net_send_msg(1, m, b.len);
}

/* ---- the guest's profile: what it earned online, into its own (see the top) */
static void ngm_keep(Viewer *v, const char *city, uint32_t flags, int next_block, int destroyed)
{
    Profile *p = &ngm_prof;
    int mine = v->nmon > 1 && v->mon[1].monster >= 0 ? profile_find_monster(&v->prof, v->monsters[v->mon[1].monster]) : -1;
    int changed = 0;
    ProfileStatus *recs[3] = { &p->block.status, &p->checkpoint, &p->current };
    for (int m = 0; m < p->nmonsters && m < PROFILE_MONSTERS; m++) {
        uint32_t seen = v->prof.current.monster_flags[m] | v->prof.checkpoint.monster_flags[m];
        uint32_t add = (seen & PM_UNLOCKED) | (m == mine ? seen & 0x1F : 0);   /* (the host's monster's upgrades are
                                                      * the host's) */
        for (int r = 0; r < 3; r++)
            if ((recs[r]->monster_flags[m] | add) != recs[r]->monster_flags[m]) {
                recs[r]->monster_flags[m] |= add;
                changed = 1;
            }
    }
    int c = city ? profile_find_city(p, city) : -1;
    if (c >= 0 && c < PROFILE_CITIES) {
        uint32_t was = p->block.city_flags[c];
        p->block.city_flags[c] |= flags | PC_AVAILABLE;
        int beaten = (p->block.city_flags[c] & PC_ALL_BLOCKS) != 0;
        if (destroyed || beaten)                     /* (profile_update: a city beaten starts at its first block) */
            p->block.start_block[c] = 0;
        else if (next_block > p->block.start_block[c])
            p->block.start_block[c] = next_block;
        int next = -1;
        if (destroyed)                               /* the next city of the campaign opens, as profile_update's */
            for (int k = 1; k <= p->ncities && next < 0; k++) {
                int i = (c + k) % p->ncities;
                if ((p->block.city_flags[i] & (PC_AVAILABLE | PC_ALL_BLOCKS)) != (PC_AVAILABLE | PC_ALL_BLOCKS))
                    next = i;
            }
        if (next >= 0)
            p->block.city_flags[next] |= PC_AVAILABLE;
        for (int r = 0; r < 3; r++)                  /* the guest's own campaign moves on only where it stood here */
            if (recs[r]->city == c) {
                if (next >= 0) {
                    recs[r]->city = next;
                    recs[r]->start_block = p->block.start_block[next];
                    recs[r]->city_flags = p->block.city_flags[next];
                } else {
                    recs[r]->start_block = p->block.start_block[c];
                    recs[r]->city_flags |= p->block.city_flags[c];
                }
            }
        changed |= was != p->block.city_flags[c] || destroyed || next_block > 0;
        if (profile_reveal_boosy(p))                 /* (every city's stars: Boosy, as the block's end does) */
            net_log("your profile: Boosy unlocked");
    }
    if (!changed)
        return;
    p->block.changed = 1;
    p->current.changed = 1;
    int wrote = p->allow_save && v->prof_write ? profile_write(p, v->prof_path, 0, (uint64_t)time(NULL) * 1000) : 0;
    net_log("your profile: the online game's %s kept%s", c >= 0 ? "block and unlocks" : "unlocks",
            wrote > 0 ? " and saved" : wrote < 0 ? " (could not write it)" : " (not saved: no profile loaded)");
}

static void ngm_pause_follow(Viewer *v);

static void ngr_menu_message(Viewer *v, NetBuf *b)
{
    int sub = (int)net_r_u8(b);
    if (sub == NGM_RESULTS && net_role() == NET_GUEST) {
        int ev = (int)net_r_u16(b), button = (int)net_r_u8(b), pad = (int)net_r_u8(b);
        if (b->err || !ngm_in_host_game || !v->hud)
            return;
        if (hud_pause_showing(v->hud)) {             /* (as pad 1's: its Options page never opens here) */
            hud_input_pad(v->hud, ev, button, 1);
            (void)hud_pause_result(v->hud);          /* (its Quit and settings are the host's to act on) */
        } else if (hud_results_showing(v->hud) && !ngm_npend) {
            net_log("flow: the host's results press %d (pad %d) taken", ev, pad);
            hud_input_pad(v->hud, ev, button, pad);
        } else if (!ngm_host.paused && ngm_npend < NGM_KEPT) {           /* (our screen is not up yet: taken once it is) */
            uint8_t *k = ngm_pend[ngm_npend++];
            k[0] = (uint8_t)(ev & 255); k[1] = (uint8_t)(ev >> 8); k[2] = (uint8_t)button; k[3] = (uint8_t)pad;
        }
        return;
    }
    if (sub == NGM_RESEND && net_role() == NET_HOST) {
        int only_won = (int)net_r_u8(b);             /* 1: the block won alone (its screen came by the flow) */
        if (!ngm_res_len || !v->hud || !hud_results_showing(v->hud))
            return;
        if (!b->err && only_won) {
            if (ngm_res_won)
                ngm_progress_send(v);
            return;
        }
        net_log("flow: the guest has no results screen: its event and %d presses again", ngm_res_n);
        net_send_msg(1, ngm_res_msg, ngm_res_len);
        if (ngm_res_won)                             /* (and the block won, for its profile) */
            ngm_progress_send(v);
        for (int i = 0; i < ngm_res_n; i++)
            ngm_send_press(ngm_res_press[i][0] | ngm_res_press[i][1] << 8, ngm_res_press[i][2], ngm_res_press[i][3]);
        return;
    }
    if (sub == NGM_PROGRESS && net_role() == NET_GUEST) {
        char city[32];
        int nl = (int)net_r_u8(b);
        if (nl > 31)
            return;
        net_r_bytes(b, city, nl);
        city[nl] = 0;
        uint32_t flags = net_r_u32(b);
        int next = (int)net_r_u32(b), destroyed = (int)net_r_u8(b);
        if (!b->err && ngm_in_host_game) {
            ngm_got_progress = 1;
            ngm_keep(v, city, flags, next, destroyed);
        }
        return;
    }
    if (sub == NGM_INPUT && net_role() == NET_HOST) {
        int n = (int)net_r_u8(b);
        for (int i = 0; i < n && !b->err; i++) {
            int ev = (int)net_r_u16(b), button = (int)net_r_u8(b);
            if (!b->err)
                ngm_apply(v, ev, button);
        }
    } else if (sub == NGM_STATE && net_role() == NET_GUEST) {
        NgMenuState s;
        net_r_bytes(b, &s, (int)sizeof s);
        if (!b->err) {
            s.focus[sizeof s.focus - 1] = s.mon[0][sizeof s.mon[0] - 1] = s.mon[1][sizeof s.mon[1] - 1] = 0;
            s.city[sizeof s.city - 1] = 0;
            ngm_host = s;
            ngm_have_host = 1;
            ngm_pause_follow(v);                     /* (at once: the presses after it are for the menu it opens) */
        }
    }
}

/* the guest: its Pause menu up while the host's is, down when it is gone */
static void ngm_pause_follow(Viewer *v)
{
    if (!ngm_guest_on() || !ngm_in_host_game || v->shell || !v->hud)
        return;
    int up = ngm_have_host && ngm_host.where == 2 && ngm_host.paused;
    if (up && !hud_pause_showing(v->hud) && !hud_results_showing(v->hud)) {
        if (pause_open(v, ngm_host.paused == 2 ? 1 : 0))
            net_log("flow: the host's Pause menu: ours opened");
    } else if (!up && hud_pause_showing(v->hud)) {
        hud_pause_close(v->hud);
        net_log("flow: the host's Pause menu is gone: ours closed");
    }
    (void)hud_pause_result(v->hud);                  /* (the guest's Pause menu decides nothing) */
}

/* ng_follow, as the guest loads the host's game: its own profile and status kept for after */
static void ngr_menu_follow(Viewer *v)
{
    ngm_npend = 0;
    ngm_resend_at = -1e30;
    ngm_got_progress = 0;
    if (ngm_in_host_game)
        return;
    ngm_prof = v->prof;
    ngm_last_game = v->last_game;
    ngm_in_host_game = 1;
}

/* the main loop's hook as a game goes back to the shell: the guest's from the host's game to its main menu (none of
 * the game's results screens, city select or movies of its own), its profile its own with what it earned kept */
static void netgame_shell_start(Viewer *v, ShellStart *st)
{
    if (!ngm_in_host_game)
        return;
    ngm_keep(v, NULL, 0, 0, 0);                      /* (the unlocks and upgrades since the last block won) */
    ngm_in_host_game = 0;
    ng_followed = 0;                                 /* (the host's game, if it still plays, is loaded again) */
    v->prof = ngm_prof;
    v->last_game = ngm_last_game;
    st->from = SHELL_FROM_GAME;
    st->quit = 1;
    st->choose_monster = st->city_destroyed = st->show_victory = 0;
    st->king_of_world = -1;
    st->attract_game = 0;
    st->idle = SHELL_IDLE_SECONDS;
    st->game = ngm_last_game;
}

/* once a frame (netgame_frame, online or not) */
static void ngr_menu_frame(Viewer *v)
{
    if (net_role() == NET_HOST) {
        static int was_on;
        if (!v->hud || !hud_pause_showing(v->hud))
            ngm_pause_by_guest = 0;
        if (!v->hud || !hud_results_showing(v->hud))
            ngm_res_len = ngm_res_n = 0;
        if (ngm_host_on()) {
            if (v->shell)
                shell_hold_idle(v->shell);           /* (player 2 waits: no attract mode) */
            ngm_send_state(v, !was_on);
        }
        was_on = ngm_host_on();
    }
    if (net_role() != NET_GUEST || !ngm_guest_on())
        ngm_have_host = 0;
    if (ngm_guest_on() && v->shell)
        shell_hold_idle(v->shell);
    /* the guest's front end: the host's, while the host is in its own */
    if (v->shell && ngm_guest_on() && ngm_have_host && ngm_host.where == 1) {
        if (shell_net_mirror(v->shell, &ngm_host.view))
            ngm_mirrored = 1;
    } else if (v->shell && ngm_mirrored && !(ngm_guest_on() && ngm_have_host && ngm_host.where == 2)) {
        ShellProfile sp;                             /* the host gone: the guest's own unlocks, its main menu */
        shell_profile(v, &sp);
        shell_set_profile(v->shell, &sp);
        shell_net_mirror(v->shell, NULL);
        ngm_mirrored = 0;
    }
    if (!v->shell)
        ngm_mirrored = 0;
    int auto_on = ngm_guest_on() && ngm_in_host_game && v->hud;
    if (auto_on && !ngm_auto)                        /* the guest's results screens answer nothing by themselves:
                                                      * the host's presses move them (NGM_RESULTS) */
        hud_results_auto(v->hud, 0);
    ngm_auto = auto_on;
    ngm_pause_follow(v);
    int res_up = ngm_guest_on() && ngm_in_host_game && v->hud && hud_results_showing(v->hud);
    if (res_up && !ngm_res_was && !ngm_got_progress) {   /* (a guest that came in after the host's block was won:
                                                      * that block for its profile, if it was one) */
        uint8_t m[3] = { NM_MENU, NGM_RESEND, 1 };
        net_send_msg(1, m, 3);
    }
    ngm_res_was = res_up;
    if (ngm_npend && v->hud && hud_results_showing(v->hud) && !hud_pause_showing(v->hud)) {
        uint8_t *k = ngm_pend[0];                    /* the host's presses our screen was not up for: one a frame */
        net_log("flow: the host's results press %d (pad %d) taken late", k[0] | k[1] << 8, k[3]);
        hud_input_pad(v->hud, k[0] | k[1] << 8, k[2], k[3]);
        memmove(ngm_pend[0], ngm_pend[1], sizeof ngm_pend[0] * (size_t)--ngm_npend);
    }
    if (ngm_npend && (!ngm_guest_on() || (ngm_have_host && !ngm_host.results && !(v->hud && hud_results_showing(v->hud)))))
        ngm_npend = 0;                               /* (the host's screen is gone too) */
    if (ngm_in_host_game && !v->shell && !v->exit_game &&
        (!ngm_guest_on() || !net_host_world())) {    /* the host left its game (or us): our menus */
        net_log(ngm_guest_on() ? "the host went back to the menus: so do we" : "offline: back to the menus");
        v->shell_mode = 1;
        v->exit_game = 1;
    }
}

/* ---- what the guest (and the host) are told on screen, every frame before the panel */
static void ngr_menu_draw(Viewer *v)
{
    char t[512];
    if (!v)
        return;
    int role = net_role();
    if (role == NET_GUEST && v->shell) {
        int st = net_state();
        if (st == NETS_CONNECTING || st == NETS_HANDSHAKE) {
            snprintf(t, sizeof t, "Connecting to %s ...", net_peer_text());
            netui_notice(1, "Joining the host", t);
            return;
        }
        if (st != NETS_CONNECTED) {
            snprintf(t, sizeof t, "%s. The menus are yours again; Insert opens the panel to join again.", net_state_text());
            netui_notice(0, "Not online", t);
            return;
        }
        if (shell_popup_active(v->shell)) {          /* its own profile: its keys are its own */
            netui_notice(0, NULL, "Online: pick or make your profile (your keys are yours); then you see the host's menus.");
            return;
        }
        const NgMenuState *h = &ngm_host;
        if (ngm_mirrored && h->where == 1 && h->view.focus && !h->view.popup) {
            const char *what = !strcmp(h->focus, "MonsterSelect") ?
                "Online, the host's menus: pick your monster with Left / Right, Cross (Enter) to be ready." :
                !strcmp(h->focus, "MainMenu") && !(h->mm_players >= 2 && (h->mm_state == 1 || h->mm_state == 2)) ?
                "Online, the host's menus: waiting for the host to pick a 2 Player game." :
                "Online: you see the host's menus; your keys and pad are its player 2.";
            netui_notice(0, NULL, what);
            return;
        }
        const char *title = "Online: you are player 2";
        if (!ngm_have_host)
            snprintf(t, sizeof t, "Connected. Waiting for the host ...");
        else if (h->where == 2 && h->world)
            snprintf(t, sizeof t, "The host's game is starting: loading it ...");
        else if (h->where == 2)
            snprintf(t, sizeof t, "The host is playing on its own (a 1 player game). You join their next 2 Player game.");
        else if (h->view.popup)
            snprintf(t, sizeof t, "The host is choosing a profile.");
        else if (!strcmp(h->focus, "AttractMode"))
            snprintf(t, sizeof t, "The host is on the title screen.");
        else if (!strcmp(h->focus, "Loading"))
            snprintf(t, sizeof t, "The host's game is loading ...");
        else
            snprintf(t, sizeof t, "The host is in the menus (%s).", h->focus[0] ? h->focus : "loading");
        size_t n = strlen(t);
        snprintf(t + n, sizeof t - n, "\n\nYour keys and pad are the host's player 2.");
        netui_notice(1, title, t);
        return;
    }
    if (role == NET_GUEST && ngm_in_host_game && ngm_host_paused()) {
        netui_notice(0, NULL, ngm_host.paused == 2 ? "You paused the host's game: your keys and pad work its Pause menu."
                                                   : "The host paused the game.");
        return;
    }
    if (v->shell && !strcmp(shell_focus(v->shell) ? shell_focus(v->shell) : "", "MainMenu")) {
        ShellAutoView av;
        shell_auto_view(v->shell, &av);
        int two = (av.mm_state == 1 || av.mm_state == 2) && av.mm_players >= 1;
        if (role == NET_OFF && two)
            netui_notice(0, NULL, "Player 2 on another PC? Press Insert: Host a game (or Host on Steam), then pick 2 Player.");
        else if (role == NET_HOST && net_state() == NETS_WAITING)
            netui_notice(0, NULL, "Hosting: waiting for player 2 to join (Insert: the panel).");
        else if (ngm_host_on())
            netui_notice(0, NULL, two ? "Player 2 is online: they pick their monster on Monster Select with their own pad."
                                      : "Player 2 is online: pick a 2 Player game.");
    } else if (ngm_host_on() && v->shell && !strcmp(shell_focus(v->shell) ? shell_focus(v->shell) : "", "MonsterSelect"))
        netui_notice(0, NULL, "Player 2 (online) picks their monster with their own pad.");
}

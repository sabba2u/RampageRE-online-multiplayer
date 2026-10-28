/* netrep_menu.c - online play: the menus (included by netgame.c). NM_MENU messages, both ways:
 *   input  guest -> host, reliable, the frames it pressed something: its menu presses (its keys, keypad and pads, any
 *          of them), given to the host's front end as pad 1's, as a couch player 2's would be (the shell's own rules
 *          say what pad 1 may do: nothing in the main menu, its own card on Monster Select ...); in a game, to the
 *          host's Pause menu and results screens the same way, and its Start opens the host's Pause menu for pad 1
 *   state  host -> guest, reliable, on change: where the host is (its front end's screen, the main menu's players,
 *          Monster Select's cards and readies, City Select's city; in a game paused or on a results screen), which
 *          the guest shows over its own screen (ngr_menu_draw)
 * The guest's own front end runs on under that notice with none of its presses (they are the host's) and no attract
 * mode. The host's game starting takes the guest out of it (ng_follow); the host's leaving its game (to its menus, or
 * the connection gone) brings the guest back to its main menu, its profile as it was before the online games (the
 * guest never writes it: save_profile, card_job). The guest's results screens are the host's (netgame.c's flow opens
 * them); they take none of the guest's presses (those go to the host's, as pad 1's) but every press the host's screen
 * takes (NGM_RESULTS, host -> guest), so they move as the host's do; its own Pause menu never opens.
 *   results  host -> guest, reliable: a press the host's results screen took (its own or the guest's) */

enum { NGM_INPUT = 1, NGM_STATE = 2, NGM_RESULTS = 3 };
#define NGM_EVENTS 32

typedef struct {
    uint8_t where;                                   /* 0 neither, 1 the front end, 2 a game */
    uint8_t mm_state, mm_players, ms_players, ms_ready, cs_locked;
    uint8_t paused;                                  /* 0 no, 1 by the host, 2 by the guest (its Start) */
    uint8_t results, world;                          /* a results screen is up; the host's game counts (2 players) */
    char focus[16], mon[2][24], city[24];
} NgMenuState;

static NgMenuState ngm_host;                         /* guest: the host's, as last told */
static int ngm_have_host;
static int ngm_pause_by_guest;                       /* host: the Pause menu up is pad 1's */
static int ngm_in_host_game;                         /* guest: in a game ng_follow loaded */
static int ngm_auto;                                 /* guest: its HUD's results answer by themselves */
static Profile ngm_prof;                             /* guest: its profile before the online games */
static ShellGame ngm_last_game;
static uint8_t ngm_ev[NGM_EVENTS][3];                /* guest: this frame's presses (ev low, high, button) */
static int ngm_nev;

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

/* shell_frame's hook: online, the guest's presses go to the host's front end, none to its own (1) */
static int netgame_shell_input(Viewer *v, const Window *w, float dt)
{
    if (!ngm_guest_on() || !v->shell)
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
    } else if (v->game) {
        s->where = 2;
        s->paused = (uint8_t)(v->hud && hud_pause_showing(v->hud) ? (ngm_pause_by_guest ? 2 : 1) : 0);
        s->results = (uint8_t)(v->hud && hud_results_showing(v->hud));
    }
    s->world = net_world() != 0;
}

static void ngm_send_state(Viewer *v)
{
    static NgMenuState sent;
    static int was_on;
    NgMenuState s;
    ngm_state_of(v, &s);
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

/* the host: a press its results screen takes (hud_net_tap), for the guest's to take too */
static void ng_results_tap(Hud *h, int ev, int button, int pad)
{
    if (!ngm_host_on() || !g_ngv || h != g_ngv->hud || !net_world())
        return;
    uint8_t m[8];
    NetBuf b = netbuf(m, sizeof m);
    net_w_u8(&b, NM_MENU);
    net_w_u8(&b, NGM_RESULTS);
    net_w_u16(&b, (uint32_t)ev);
    net_w_u8(&b, (uint32_t)button);
    net_w_u8(&b, (uint32_t)pad);
    if (!b.err)
        net_send_msg(1, m, b.len);
    net_log("flow: results press %d (pad %d) sent", ev, pad);
}

static void ngr_menu_message(Viewer *v, NetBuf *b)
{
    int sub = (int)net_r_u8(b);
    if (sub == NGM_RESULTS && net_role() == NET_GUEST) {
        int ev = (int)net_r_u16(b), button = (int)net_r_u8(b), pad = (int)net_r_u8(b);
        if (!b->err && ngm_in_host_game && v->hud && hud_results_showing(v->hud)) {
            net_log("flow: the host's results press %d (pad %d) taken", ev, pad);
            hud_input_pad(v->hud, ev, button, pad);
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
        }
    }
}

/* ng_follow, as the guest loads the host's game: its own profile and status kept for after */
static void ngr_menu_follow(Viewer *v)
{
    if (ngm_in_host_game)
        return;
    ngm_prof = v->prof;
    ngm_last_game = v->last_game;
    ngm_in_host_game = 1;
}

/* the main loop's hook as a game goes back to the shell: the guest's from the host's game to its main menu (none of
 * the game's results screens, city select or movies of its own), its profile as it was */
static void netgame_shell_start(Viewer *v, ShellStart *st)
{
    if (!ngm_in_host_game)
        return;
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
        if (!v->hud || !hud_pause_showing(v->hud))
            ngm_pause_by_guest = 0;
        if (ngm_host_on()) {
            if (v->shell)
                shell_hold_idle(v->shell);           /* (player 2 waits: no attract mode) */
            ngm_send_state(v);
        }
    }
    if (net_role() != NET_GUEST || !ngm_guest_on())
        ngm_have_host = 0;
    if (ngm_guest_on() && v->shell)
        shell_hold_idle(v->shell);
    int auto_on = ngm_guest_on() && ngm_in_host_game && v->hud;
    if (auto_on && !ngm_auto)                        /* the guest's results screens answer nothing by themselves:
                                                      * the host's presses move them (NGM_RESULTS) */
        hud_results_auto(v->hud, 0);
    ngm_auto = auto_on;
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
        const NgMenuState *h = &ngm_host;
        const char *title = "Online: you are player 2";
        if (!ngm_have_host)
            snprintf(t, sizeof t, "Connected. Waiting for the host ...");
        else if (h->where == 2 && h->world)
            snprintf(t, sizeof t, "The host's game is starting: loading it ...");
        else if (h->where == 2)
            snprintf(t, sizeof t, "The host is playing on its own (a 1 player game). You join their next 2 Player game.");
        else if (!strcmp(h->focus, "MonsterSelect"))
            snprintf(t, sizeof t, "Monster Select: pick your monster with Left / Right, then Cross (Enter) to be ready.\n\n"
                     "Your monster:  %s%s\nThe host's:      %s%s", h->mon[1][0] ? h->mon[1] : "-",
                     h->ms_ready & 2 ? "  (ready)" : "", h->mon[0][0] ? h->mon[0] : "-", h->ms_ready & 1 ? "  (ready)" : "");
        else if (!strcmp(h->focus, "MonsterSheet"))
            snprintf(t, sizeof t, "The host is looking at a monster's card.");
        else if (!strcmp(h->focus, "CitySelect"))
            snprintf(t, sizeof t, "The host is picking the city%s%s.", h->city[0] ? ": " : "", h->city);
        else if (!strcmp(h->focus, "MainMenu"))
            snprintf(t, sizeof t, "The host is in the main menu%s.",
                     h->mm_players >= 2 && (h->mm_state == 1 || h->mm_state == 2) ? " with 2 players"
                                                                                  : ": waiting for them to pick a 2 Player game");
        else if (!strcmp(h->focus, "Options"))
            snprintf(t, sizeof t, "The host is in the options.");
        else if (!strcmp(h->focus, "AttractMode"))
            snprintf(t, sizeof t, "The host is on the title screen.");
        else if (!strcmp(h->focus, "MemoryCard"))
            snprintf(t, sizeof t, "The host is choosing a profile.");
        else
            snprintf(t, sizeof t, "The host is in the menus (%s).", h->focus[0] ? h->focus : "loading");
        size_t n = strlen(t);
        snprintf(t + n, sizeof t - n, "\n\nYour keys and pad are the host's player 2.");
        netui_notice(1, title, t);
        return;
    }
    if (role == NET_GUEST && ngm_in_host_game && ngm_host_paused()) {
        netui_notice(0, "Paused", ngm_host.paused == 2 ? "You paused the game. The host's Pause menu takes your keys and pad: Cross (Enter) on Continue plays on."
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

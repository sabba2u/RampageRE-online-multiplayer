/* netshell.c - online play's front end on the guest (port extra; docs/NETPLAY.md "Through the menus"): included at
 * the end of shell.c (NET:), so the shell's own interfaces, faders and lists are at hand and shell.c itself keeps one
 * line. The host's shell tells where it stands (shell_net_view: the interface with the pad, the main menu's box, play
 * mode and lists, Monster Select's cards, readies and flipped backs, the card sheet's card, City Select's city, night
 * and stats, the profile's unlocks that those screens show); netrep_menu.c sends that whenever it changes, and the
 * guest's shell is made the same each frame (shell_net_mirror). It is the guest's own shell that draws, plays its
 * sounds and runs its fades, so the guest sees the host's front end as the game draws it, not a picture of it.
 *
 * The mirror moves the guest's interfaces the way their own scripts would (cr_show for the focus, the main menu's
 * faders between its boxes, ms_card_face / ms_ready / ms_flip for the cards, cs_selection for the city), never by
 * their Accept: the guest's shell never starts a game (SHELL_EVT_START) or writes a profile of its own while it
 * mirrors; the host's game starting is what takes the guest into it (netgame.c ng_follow). Not mirrored: the host's
 * profile popup and its loading screen and movies (the guest is told in words, netrep_menu.c), the cheat entry's
 * digits, the Options' sliders (the guest's own settings show), the card sheet's zoom. */

void shell_net_view(const Shell *s, ShellNetView *o)
{
    memset(o, 0, sizeof *o);
    if (!s || s->popup)
        return;
    o->state = (uint8_t)s->state;
    o->popup = s->mc.mode != MC_OFF;
    if (s->cr_on && s->state == 3 && !s->ld.on && !s->vi.on && s->focus > IF_ATTRACT && s->focus < IF_COUNT)
        o->focus = (uint8_t)(s->focus + 1);
    const MainMenu *m = &s->mm;
    o->mm_state = (uint8_t)m->state;
    o->mm_play_mode = (uint8_t)m->play_mode;
    o->mm_players_sel = (uint8_t)(m->lb_players && m->lb_players->sel > 0 ? m->lb_players->sel : 0);
    o->mm_bots_sel = (uint8_t)(m->lb_bots && m->lb_bots->sel > 0 ? m->lb_bots->sel : 0);
    o->mm_retro = (uint8_t)m->retro_game;
    o->play_type = (uint8_t)s->gs.play_type;
    o->num_players = (uint8_t)s->gs.num_players;
    o->num_bots = (uint8_t)s->gs.num_bots;
    const MonsterSel *ms = &s->ms;
    for (int k = 0; k < 4; k++) {
        o->ms_monster[k] = (int16_t)(k < ms->num_players ? ms_monster(ms, k) : -1);
        if (k < ms->num_players && ms->pl[k].ready)
            o->ms_ready |= (uint8_t)(1 << k);
        if (k < ms->num_players && ms->pl[k].spin >= 1 && ms->pl[k].spin <= 3)   /* (turning to, or on, its back) */
            o->ms_back |= (uint8_t)(1 << k);
    }
    o->ms_players = (uint8_t)ms->num_players;
    o->sheet_monster = (int16_t)s->sh.selection;
    o->sheet_player = (uint8_t)s->sh.map_controller;
    o->cs_index = (uint8_t)s->cs.index;
    o->cs_night = (uint8_t)s->cs.night;
    o->cs_stats = (uint8_t)s->cs.stats_visible;
    memcpy(o->monster_flags, s->prof.monster_flags, sizeof o->monster_flags);
    memcpy(o->city_flags, s->prof.city_flags, sizeof o->city_flags);
    o->cheats = s->cheats;
}

/* the main menu's boxes (MainMenu.scr's: 0 the play modes, 1 the players, 2 the bots, 4 the retro games, 5 the cheat
 * entry): the one shown faded in, the others out, as the script's own steps between them do */
static void nsh_box(Shell *s, const ShellNetView *h)
{
    MainMenu *m = &s->mm;
    if (h->mm_play_mode >= 2 && h->mm_play_mode <= 5 && m->play_mode != h->mm_play_mode) {
        m->play_mode = h->mm_play_mode;
        node_localize(&s->gui, m->playtype, PLAY_MODE_TITLES[m->play_mode - 2]);
        sound(s, "Rollover");
    }
    if (m->state != h->mm_state) {
        int to = h->mm_state;
        fader_go(&m->fd, m->icons, -1);
        fader_go(&m->fd, m->numplayers, -1);
        fader_go(&m->fd, m->numbots, -1);
        fader_go(&m->fd, m->retro, -1);
        fader_go(&m->fd, m->cheat, -1);
        sound(s, to > m->state ? "Click" : "Cancel");
        if (to == 1 || to == 2) {
            mm_play_mode_select(s);                      /* (the players' rows for the play mode; state 1) */
            if (to == 2) {                               /* processNumPlayerSelect's bots box (not its Monster Select) */
                list_set(m->lb_players, h->mm_players_sel);
                m->num_players = h->mm_players_sel + 1;
                fader_go(&m->fd, m->numplayers, -1);
                fader_go(&m->fd, m->numbots, 1);
                char key[32];
                snprintf(key, sizeof key, "MainMenu_%dPlayer", m->num_players);
                node_localize(&s->gui, find(ifc_root(s, IF_MAINMENU), "nb_title"), key);
                for (int i = 0; i < 4; i++) {
                    Node *item = list_item(m->lb_bots, i);
                    if (!item) continue;
                    show(item, i < m->num_players);
                    if (i < m->num_players) {
                        char t[8];
                        snprintf(t, sizeof t, "%d", m->num_players - i);
                        set_text(item, t);
                        snprintf(t, sizeof t, "%d", i);
                        set_text(find(item, "computer"), t);
                    }
                }
            }
        } else if (to == 4) {
            fader_go(&m->fd, m->retro, 1);
        } else if (to == 5) {
            fader_go(&m->fd, m->cheat, 1);
            mm_reset_cheat(s);
        } else
            fader_go(&m->fd, m->icons, 1);
        m->state = to;
    }
    if (m->state == 1 && m->lb_players && m->lb_players->sel != h->mm_players_sel) {
        list_set(m->lb_players, h->mm_players_sel);
        sound(s, "Rollover");
    }
    if (m->state == 2 && m->lb_bots && m->lb_bots->sel != h->mm_bots_sel) {
        list_set(m->lb_bots, h->mm_bots_sel);
        sound(s, "Rollover");
    }
    if (m->state == 4 && m->retro_game != h->mm_retro) {
        m->retro_game = h->mm_retro;
        mm_retro_selection(s);
        sound(s, "Rollover");
    }
}

/* Monster Select set up as the host's (setupFromMainMenu with the host's players and picks) */
static void nsh_ms_setup(Shell *s, const ShellNetView *h)
{
    s->gs.play_type = h->play_type;
    s->gs.num_players = h->num_players ? h->num_players : 1;
    s->gs.num_bots = h->num_bots;
    for (int k = 0; k < 4; k++)
        if (h->ms_monster[k] >= 0 && h->ms_monster[k] < s->nmonsters)
            s->gs.monster[k] = h->ms_monster[k];
    ms_setup_from_main_menu(s);
}

/* each card: the host's monster, ready or not, its front or back */
static void nsh_cards(Shell *s, const ShellNetView *h)
{
    MonsterSel *m = &s->ms;
    for (int k = 0; k < m->num_players && k < 4; k++) {
        MsPlayer *pl = &m->pl[k];
        int id = h->ms_monster[k], j = -1;
        if (id < 0 || id >= s->nmonsters)
            continue;
        for (int pass = 0; pass < 2 && j < 0; pass++) {
            if (pass)
                ms_fill(s, k, id);                       /* (the host's unlocks: the list again) */
            for (int i = 0; i < pl->nmon; i++)
                if (pl->monsters[i] == id)
                    j = i;
        }
        if (j < 0)
            continue;
        if (pl->selection != j) {
            pl->selection = j;
            ms_card_face(s, monster_name(s, id), k);
            sound(s, "Rollover");
        }
        int ready = h->ms_ready >> k & 1;
        if (pl->ready != ready) {
            ms_ready(s, k, ready);
            sound(s, ready ? "Click" : "Cancel");
        }
        int back = h->ms_back >> k & 1, ours = pl->spin >= 1 && pl->spin <= 3;
        if (back != ours && !pl->ready && ms_ready_for_button(m, k))
            ms_flip(s, k);
    }
}

static void nsh_city(Shell *s, const ShellNetView *h)
{
    CitySel *c = &s->cs;
    if (c->initialized != 2)
        return;
    if (h->cs_index < s->ncities && c->index != h->cs_index) {
        cs_selection(s, (int)h->cs_index - c->index);
        sound(s, "Rollover");
    }
    if (c->night != (h->cs_night != 0))
        cs_day_night(s, 1);
    if (c->stats_visible != (h->cs_stats != 0) && !c->locked && s->gs.play_type == 2) {
        c->stats_visible = h->cs_stats != 0;
        fader_go(&c->fd, c->stats, c->stats_visible ? 1 : -1);
    }
}

int shell_net_mirror(Shell *s, const ShellNetView *h)
{
    if (!s || s->popup || !s->cr_on)
        return 0;
    if (s->mc.mode != MC_OFF || s->ld.on || s->vi.on)    /* its own profile popup: the guest's to answer */
        return 0;
    if (!h) {                                            /* the host gone: the guest's own main menu */
        if (s->focus != IF_MAINMENU && s->state == 3) {
            s->mm.state = 0;
            cr_show(s, IF_MAINMENU);
        }
        return 0;
    }
    if (!h->focus || h->popup)
        return 0;
    /* the screens show the host's unlocks (the host's profile is the game's); the guest's own are put back with
     * shell_set_profile once the mirror ends (netrep_menu.c) */
    memcpy(s->prof.monster_flags, h->monster_flags, sizeof h->monster_flags);
    memcpy(s->prof.city_flags, h->city_flags, sizeof h->city_flags);
    s->cheats = h->cheats;
    if (s->state != 3) {                                 /* the boot's movies or the attract mode: to the menus */
        s->boot_completed = 1;
        s->mc.boot_done = 1;
        s->state = 3;
        music(s, "Oingo");                               /* (cr_show below takes the attract mode's focus) */
    }
    int want = h->focus - 1;
    s->idle = IDLE_SECONDS;
    if (want == IF_MAINMENU || (want == IF_OPTIONS && s->focus != IF_OPTIONS))
        nsh_box(s, h);
    if (s->focus != want) {
        if (want == IF_MONSTERSELECT) {
            nsh_ms_setup(s, h);
            cr_show(s, IF_MONSTERSELECT);
        } else if (want == IF_MONSTERSHEET) {
            if (s->ms.num_players != h->ms_players)
                nsh_ms_setup(s, h);
            sheet_set_controller(s, h->sheet_player, h->sheet_player);
            sheet_select_card(s, monster_name(s, h->sheet_monster >= 0 && h->sheet_monster < s->nmonsters ?
                                                     h->sheet_monster : 0));
            cr_show(s, IF_MONSTERSHEET);
        } else if (want == IF_CITYSELECT) {
            s->gs.play_type = h->play_type;
            s->gs.num_players = h->num_players ? h->num_players : 1;
            s->gs.num_bots = h->num_bots;
            s->gs.city_index = h->cs_index < s->ncities ? h->cs_index : 0;
            s->gs.night = h->cs_night;
            if (s->ms.num_players != h->ms_players)
                nsh_ms_setup(s, h);                      /* (City Select's Back shows the cards) */
            cr_show(s, IF_CITYSELECT);
        } else if (want == IF_OPTIONS && s->focus == IF_MAINMENU) {   /* MainMenu's Triangle: over the main menu */
            fader_go(&s->mm.fd, s->mm.icons, -1);
            fader_go(&s->cr_fd, ifc_root(s, IF_OPTIONS), 1);
            cr_focus(s, IF_OPTIONS);
            sound(s, "Click");
        } else
            cr_show(s, want);
        if (want == IF_MAINMENU)
            nsh_box(s, h);
    }
    if (s->focus == IF_MONSTERSELECT) {
        if (s->ms.num_players != h->ms_players && h->ms_players) {
            nsh_ms_setup(s, h);
            cr_focus(s, IF_MONSTERSELECT);
        }
        nsh_cards(s, h);
    } else if (s->focus == IF_MONSTERSHEET && s->sh.selection != h->sheet_monster && h->sheet_monster >= 0 &&
               h->sheet_monster < s->nmonsters) {
        sheet_select_card(s, monster_name(s, h->sheet_monster));
    } else if (s->focus == IF_CITYSELECT)
        nsh_city(s, h);
    return 1;
}

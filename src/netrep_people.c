/* netrep_people.c - online play: people (window occupants, walkers, held, thrown) and edibles (included by netgame.c; channels NGP_PEOPLE, NGP_EDIBLES).
 * The host's system runs as offline; the guest's leaves its logic out (net_guest_world() in its step in main.c) and
 * takes each unit from the host's snapshots (ng_pup[...].write / .apply) and its effects and sounds from the host's
 * events (ng_event / .event). See netgame.c "puppets". */

static void ngr_people_init(void)
{
}

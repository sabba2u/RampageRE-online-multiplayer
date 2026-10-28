/* netrep_army.c - online play: the army (units, shots) and the bosses (included by netgame.c; channels NGP_ARMY, NGP_BOSS).
 * The host's system runs as offline; the guest's leaves its logic out (net_guest_world() in its step in main.c) and
 * takes each unit from the host's snapshots (ng_pup[...].write / .apply) and its effects and sounds from the host's
 * events (ng_event / .event). See netgame.c "puppets". */

static void ngr_army_init(void)
{
}

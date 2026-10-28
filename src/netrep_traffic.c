/* netrep_traffic.c - online play: vehicles, street objects, rail, parade (included by netgame.c; channels NGP_VEHICLES, NGP_STREET, NGP_RAIL, NGP_PARADE).
 * The host's system runs as offline; the guest's leaves its logic out (net_guest_world() in its step in main.c) and
 * takes each unit from the host's snapshots (ng_pup[...].write / .apply) and its effects and sounds from the host's
 * events (ng_event / .event). See netgame.c "puppets". */

static void ngr_traffic_init(void)
{
}

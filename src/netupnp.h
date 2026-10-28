/* netupnp.h - online play's automatic port forwarding (port extra, docs/NETPLAY.md "Hosting over the internet"): while
 * the host waits for its guest on UDP (ENet, not Steam), the router is asked by UPnP IGD (miniupnpc,
 * src/net/third_party/miniupnpc) to forward that UDP port to this PC, and the forward is taken away again when hosting
 * stops or the game ends. The router's search takes up to two seconds, so it runs on a thread of its own; the game
 * reads where it stands once a frame (net_upnp_poll, by net_update) and the panel shows it. No router, a router with
 * UPnP off or one that says no: logged, shown, and hosting goes on as before (a LAN, a VPN or a forward by hand).
 * RAMPAGE_NET_UPNP=0 leaves the router alone. */
#ifndef RAMPAGE_NETUPNP_H
#define RAMPAGE_NETUPNP_H

#ifdef __cplusplus
extern "C" {
#endif

enum { NUP_OFF, NUP_WORKING, NUP_MAPPED, NUP_FAILED };

void net_upnp_open(int port);                        /* ask for UDP port -> this PC (in the background) */
void net_upnp_close(void);                           /* the forward taken away (waits for the search to end) */
void net_upnp_poll(void);                            /* once a frame: the thread's news into the log */
/* NUP_*; text: "203.0.113.7:27960 forwarded to 192.168.1.20" / "No UPnP router found" / "Asking the router ..." */
int net_upnp_state(const char **text);

#ifdef __cplusplus
}
#endif
#endif

/* netlobby.h - online play's game list (port extra, docs/NETPLAY.md "The lobby"): a host's game lists itself with a
 * lobby server (lobby/: Node, on a VPS or on the host's own PC) and keeps the listing alive; a guest's panel shows
 * the list and joins one with a click, or by its short join code. The lobby only hands out addresses: the guest
 * still joins the host straight over ENet (net.h), as typing the address would.
 *
 * All the HTTP happens on background threads (WinHTTP on Windows; plain sockets elsewhere, http:// only): nothing
 * here ever waits on the network in the game's frame. A lobby that is down only shows in the status lines. */
#ifndef RAMPAGE_NETLOBBY_H
#define RAMPAGE_NETLOBBY_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETLOBBY_DEFAULT_URL "http://localhost:19980"
#define NETLOBBY_GAMES 32                            /* the panel's list: the first this many */

/* one listed game, as GET /games.txt says it (lobby/src/http.ts): join is the address for us to use ("ip:port",
 * the lobby picks the one that reaches the host from where we are; "-" none), build net_build_id() as 8 hex */
typedef struct {
    char code[8], join[64], build[12], name[32], monster[32], city[40];
    int players, max;
} NetLobbyGame;

/* ---- the parts without a network (tests/net_test.c) ---- */
/* "http://host[:port][/path]" (https too): 0 ok, -1 not a URL this can use. path "" when none */
int netlobby_parse_url(const char *url, char *host, int hcap, int *port, char *path, int pcap, int *https);
/* games.txt: "rampage-lobby 1", then one line per game, tab-separated (code join players max build name monster
 * city). The games read (at most max), -1 when it is not a lobby's list */
int netlobby_parse_games(const char *text, NetLobbyGame *out, int max);
/* POST /games's answer to the game: "ok <code> <token> <heartbeat seconds>". 0 ok */
int netlobby_parse_registered(const char *text, char *code, int ccap, char *token, int tcap, int *heartbeat);
int netlobby_is_code(const char *s);                 /* 5 of ABCDEFGHJKLMNPQRSTUVWXYZ23456789, any case */
/* what --join, a rampage:// link or the panel's field gives: NLJ_CODE (out: the code, upper case), NLJ_ADDR (out:
 * "host" or "host:port"), NLJ_NONE (not something to join; out empty). rampage://join/<x>/ takes x */
enum { NLJ_NONE, NLJ_ADDR, NLJ_CODE };
int netlobby_join_kind(const char *arg, char *out, int cap);

/* ---- settings (netlobby.cfg, as the panel's others: lookfx.cfg). The URL: --lobby, else RAMPAGE_LOBBY, else the
 * saved one, else NETLOBBY_DEFAULT_URL ---- */
const char *netlobby_url(void);
void netlobby_set_url(const char *url, int save);    /* save 0: --lobby / RAMPAGE_LOBBY, this run only */
int netlobby_listing(void);                          /* "List my game": on unless turned off */
void netlobby_set_listing(int on);
const char *netlobby_name(void);                     /* the host's name in the list (the Windows user's at first) */
void netlobby_set_name(const char *name);

/* ---- the host's listing: made, kept alive (a heartbeat every ~10 s) and taken down by netlobby_update, while
 * hosting over ENet with listing on ---- */
void netlobby_set_game(const char *monster, const char *city);   /* netgame.c, as the world changes ("" menus) */
void netlobby_set_external(const char *addr);       /* our address on the internet, when known (UPnP), "" none */
const char *netlobby_my_code(void);                  /* our join code while listed, "" not */
const char *netlobby_host_status(void);              /* "Listed: code K7PQ2", "Lobby not reachable" ... */

/* ---- the guest's list ---- */
void netlobby_browse(void);                          /* each frame the panel shows the list: refreshed every 5 s */
void netlobby_refresh(void);                         /* now */
int netlobby_count(void);
const NetLobbyGame *netlobby_game(int i);
const char *netlobby_list_status(void);
int netlobby_same_build(const NetLobbyGame *g);      /* 1: our build, joinable */
/* a code asked of the lobby (GET /games/<code>.txt), then net_start_join with its address, from netlobby_update.
 * net_start_join hands codes and rampage:// links here itself (net.c) */
void netlobby_lookup(const char *code);

void netlobby_update(double now);                    /* net_update, once a frame: the finished requests, the next */

#ifdef __cplusplus
}
#endif
#endif

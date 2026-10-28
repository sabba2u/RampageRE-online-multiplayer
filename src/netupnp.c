/* netupnp.c - online play's automatic port forwarding (netupnp.h): UPnP IGD through miniupnpc 2.2.8
 * (src/net/third_party/miniupnpc, built into build\net\upnp.a by build.bat). One forward at a time: the host's UDP port
 * (27960, or --host's) to the same port on this PC's LAN address, "RampageRE" its description, no lease time (the
 * router keeps it until it is taken away; a router that wants one gets a day). The search (SSDP, 2 s), the router's
 * description, the external address and the AddPortMapping run on a thread of their own, so the game never waits for
 * the router; the removal at the stop runs where net_stop is called (one request to a router that was there a moment
 * ago). What the thread finds is told to the log by net_upnp_poll on the game's thread (net_log is not for two
 * threads), and to the panel by net_upnp_state. */
#include "netupnp.h"
#include "net.h"
#include "netlobby.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"

static CRITICAL_SECTION nup_lock;
static int nup_lock_ok;
static HANDLE nup_thread;
static volatile LONG nup_cancel;                     /* the stop came while the thread still searched */
static int nup_state = NUP_OFF;
static char nup_text[160];
static char nup_news[4][160];                        /* lines for the log, from the thread (net_upnp_poll) */
static int nup_nnews;
static char nup_ext[64];                             /* the router's address on the internet, once mapped */
static int nup_ext_told;                             /* (told to the lobby: netlobby_set_external) */
/* the router, kept by the thread for the removal (read by net_upnp_close once the thread is over) */
static struct UPNPUrls nup_urls;
static struct IGDdatas nup_data;
static int nup_have_urls, nup_mapped, nup_port;
static char nup_lan[64];

static void nup_init_lock(void)
{
    if (!nup_lock_ok) {
        InitializeCriticalSection(&nup_lock);
        nup_lock_ok = 1;
    }
}

/* the thread's news: the state and the panel's text, and a line for the log */
static void nup_set(int state, const char *fmt, ...)
{
    char t[160];
    va_list a;
    va_start(a, fmt);
    vsnprintf(t, sizeof t, fmt, a);
    va_end(a);
    EnterCriticalSection(&nup_lock);
    nup_state = state;
    snprintf(nup_text, sizeof nup_text, "%s", t);
    if (nup_nnews < 4)
        snprintf(nup_news[nup_nnews++], sizeof nup_news[0], "UPnP: %s", t);
    LeaveCriticalSection(&nup_lock);
}

static DWORD WINAPI nup_run(LPVOID arg)
{
    (void)arg;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);                /* (ENet's is there too; the count keeps both) */
    int err = 0;
    /* RAMPAGE_NET_UPNP_IF: the address of the network card to search from (the default: the system's choice); a
     * test of the no-router case is 127.0.0.1, where no router can answer */
    struct UPNPDev *devs = upnpDiscover(2000, getenv("RAMPAGE_NET_UPNP_IF"), NULL, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
    if (nup_cancel) {
        freeUPNPDevlist(devs);
        WSACleanup();
        return 0;
    }
    if (!devs) {
        nup_set(NUP_FAILED, "no UPnP router answered (none, or UPnP is off on it): forward UDP %d by hand, or use "
                "Steam / a LAN / a VPN", nup_port);
        WSACleanup();
        return 0;
    }
    char wan[64] = "";
    int r = UPNP_GetValidIGD(devs, &nup_urls, &nup_data, nup_lan, sizeof nup_lan, wan, sizeof wan);
    freeUPNPDevlist(devs);
    if (r <= 0 || r > 3) {                           /* 0 none, 4 a UPnP device that is no router */
        if (r > 0)
            FreeUPNPUrls(&nup_urls);
        nup_set(NUP_FAILED, "no UPnP router found on this network: forward UDP %d by hand, or use Steam / a LAN / a VPN",
                nup_port);
        WSACleanup();
        return 0;
    }
    nup_have_urls = 1;
    char ext[64] = "";
    if (UPNP_GetExternalIPAddress(nup_urls.controlURL, nup_data.first.servicetype, ext) != UPNPCOMMAND_SUCCESS || !*ext)
        snprintf(ext, sizeof ext, "%s", *wan ? wan : "?");
    char port[16];
    snprintf(port, sizeof port, "%d", nup_port);
    int e = UPNP_AddPortMapping(nup_urls.controlURL, nup_data.first.servicetype, port, port, nup_lan, "RampageRE", "UDP",
                                NULL, "0");
    if (e != UPNPCOMMAND_SUCCESS && e != 718)        /* a router that wants a lease time: a day */
        e = UPNP_AddPortMapping(nup_urls.controlURL, nup_data.first.servicetype, port, port, nup_lan, "RampageRE",
                                "UDP", NULL, "86400");
    if (e == 718) {                                  /* ConflictInMappingEntry: ours from a copy that did not stop
                                                      * cleanly is fine; another PC's is left alone */
        char client[64] = "", iport[16] = "", desc[96] = "", on[8] = "", lease[24] = "";
        if (UPNP_GetSpecificPortMappingEntry(nup_urls.controlURL, nup_data.first.servicetype, port, "UDP", NULL, client,
                                             iport, desc, on, lease) == UPNPCOMMAND_SUCCESS &&
            !strcmp(client, nup_lan) && atoi(iport) == nup_port)
            e = UPNPCOMMAND_SUCCESS;
        else {
            nup_set(NUP_FAILED, "the router forwards UDP %d to another PC (%s) already", nup_port, *client ? client : "?");
            return 0;
        }
    }
    if (e != UPNPCOMMAND_SUCCESS) {
        nup_set(NUP_FAILED, "the router said no to forwarding UDP %d: %s (%d)", nup_port,
                strupnperror(e) ? strupnperror(e) : "error", e);
        return 0;
    }
    nup_mapped = 1;
    EnterCriticalSection(&nup_lock);
    snprintf(nup_ext, sizeof nup_ext, "%s", ext);
    LeaveCriticalSection(&nup_lock);
    if (r == 2)                                      /* the router's own address is a private one: another router or
                                                      * the provider's NAT (CGNAT) is in front of it */
        nup_set(NUP_MAPPED, "%s:%d forwarded to %s, but that address is private (a second router, or the provider's "
                "NAT): friends outside may still not reach you", ext, nup_port, nup_lan);
    else
        nup_set(NUP_MAPPED, "%s:%d forwarded to %s; friends join %s", ext, nup_port, nup_lan, ext);
    return 0;
}

void net_upnp_open(int port)
{
    nup_init_lock();
    net_upnp_close();
    const char *env = getenv("RAMPAGE_NET_UPNP");
    if (env && !strcmp(env, "0")) {
        EnterCriticalSection(&nup_lock);
        nup_state = NUP_OFF;
        snprintf(nup_text, sizeof nup_text, "off (RAMPAGE_NET_UPNP=0)");
        LeaveCriticalSection(&nup_lock);
        return;
    }
    nup_port = port;
    nup_cancel = 0;
    nup_mapped = nup_have_urls = 0;
    nup_lan[0] = 0;
    EnterCriticalSection(&nup_lock);
    nup_state = NUP_WORKING;
    snprintf(nup_text, sizeof nup_text, "asking the router to forward UDP %d ...", port);
    LeaveCriticalSection(&nup_lock);
    net_log("UPnP: asking the router to forward UDP %d", port);
    nup_thread = CreateThread(NULL, 0, nup_run, NULL, 0, NULL);
    if (!nup_thread)
        nup_set(NUP_FAILED, "could not start");
}

void net_upnp_close(void)
{
    if (!nup_lock_ok)
        return;
    if (nup_thread) {
        InterlockedExchange(&nup_cancel, 1);
        WaitForSingleObject(nup_thread, 10000);      /* (the search: 2 s; a request: the library's own time-outs) */
        CloseHandle(nup_thread);
        nup_thread = NULL;
    }
    net_upnp_poll();                                 /* (what the thread said first) */
    if (nup_mapped) {
        char port[16];
        snprintf(port, sizeof port, "%d", nup_port);
        int e = UPNP_DeletePortMapping(nup_urls.controlURL, nup_data.first.servicetype, port, "UDP", NULL);
        if (e == UPNPCOMMAND_SUCCESS)
            net_log("UPnP: the router no longer forwards UDP %d", nup_port);
        else
            net_log("UPnP: the forward of UDP %d could not be taken away (%d); the router drops it itself later",
                    nup_port, e);
        nup_mapped = 0;
    }
    if (nup_have_urls) {
        FreeUPNPUrls(&nup_urls);
        nup_have_urls = 0;
        WSACleanup();                                /* (the thread's WSAStartup, kept for the removal) */
    }
    EnterCriticalSection(&nup_lock);
    nup_state = NUP_OFF;
    nup_text[0] = nup_ext[0] = 0;
    LeaveCriticalSection(&nup_lock);
    if (nup_ext_told)
        netlobby_set_external("");
    nup_ext_told = 0;
}

void net_upnp_poll(void)
{
    if (!nup_lock_ok)
        return;
    char news[4][160], ext[64];
    EnterCriticalSection(&nup_lock);
    snprintf(ext, sizeof ext, "%s", nup_state == NUP_MAPPED ? nup_ext : "");
    int n = nup_nnews;
    memcpy(news, nup_news, sizeof news);
    nup_nnews = 0;
    LeaveCriticalSection(&nup_lock);
    for (int i = 0; i < n; i++)
        net_log("%s", news[i]);
    if (*ext && !nup_ext_told) {                     /* the lobby lists the address friends join (netlobby.c) */
        netlobby_set_external(ext);
        nup_ext_told = 1;
    }
}

int net_upnp_state(const char **text)
{
    static char t[160];
    if (!nup_lock_ok) {
        if (text) *text = "";
        return NUP_OFF;
    }
    EnterCriticalSection(&nup_lock);
    int s = nup_state;
    snprintf(t, sizeof t, "%s", nup_text);
    LeaveCriticalSection(&nup_lock);
    if (text)
        *text = t;
    return s;
}

#else                                                /* (online play is Windows' for now: nothing to forward) */
void net_upnp_open(int port) { (void)port; }
void net_upnp_close(void) {}
void net_upnp_poll(void) {}
int net_upnp_state(const char **text) { if (text) *text = ""; return NUP_OFF; }
#endif

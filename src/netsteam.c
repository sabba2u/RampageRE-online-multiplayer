/* netsteam.c - online play over Steam (nettransport.h's other back end): friends-only lobbies, the overlay's invite,
 * "Join Game" from the friends list, and ISteamNetworkingMessages over Valve's relay (no ports, no addresses). The
 * Steamworks flat C API is read out of steam_api64.dll at run time (LoadLibrary): without the DLL, or without Steam
 * running, Steam is simply not offered and the ENet back end (IP) is all there is. Nothing of the SDK is needed to
 * build. For development the app is Spacewar (480): steam_appid.txt beside the exe, and both players own Spacewar
 * (free); invites then show as Spacewar.
 *
 * The join paths (Emberville's lessons): an invite accepted while the game runs (GameLobbyJoinRequested_t), "Join
 * Game" on a friend (GameRichPresenceJoinRequested_t, rich presence "connect" = "+connect_lobby <id>"), and the game
 * started by an invite with +connect_lobby <id> on its command line. The lobby's owner is the host. */
#include "nettransport.h"
#include "net.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>

typedef uint64_t SteamAPICall_t;
typedef int32_t HSteamPipe;
typedef struct { uint8_t raw[136]; } SteamNetworkingIdentity;   /* the SDK's, used only through its functions */
typedef struct {                                     /* steamnetworkingtypes.h, pack 8: the fields read here */
    void *data;
    int size;
    uint32_t conn;
    SteamNetworkingIdentity peer;
} SteamNetworkingMessage;
typedef struct { int32_t user; int callback; uint8_t *param; int param_size; } CallbackMsg;

enum {                                               /* callback ids */
    CB_LOBBY_ENTER = 504, CB_LOBBY_CREATED = 513, CB_LOBBY_JOIN_REQUESTED = 333, CB_RICH_JOIN_REQUESTED = 337,
    CB_CALL_COMPLETED = 703, CB_SESSION_REQUEST = 1251, CB_SESSION_FAILED = 1252
};
enum { SEND_UNRELIABLE_NO_NAGLE = 1, SEND_RELIABLE = 8, SEND_AUTO_RESTART = 32 };

#define F(ret, name, args) static ret (*name) args;
F(int, p_InitFlat, (char *err))
F(void, p_Shutdown, (void))
F(HSteamPipe, p_GetHSteamPipe, (void))
F(void, p_MD_Init, (void))
F(void, p_MD_RunFrame, (HSteamPipe))
F(int, p_MD_GetNext, (HSteamPipe, CallbackMsg *))
F(void, p_MD_FreeLast, (HSteamPipe))
F(int, p_MD_GetCallResult, (HSteamPipe, SteamAPICall_t, void *, int, int, int *))
F(void *, p_Messages, (void))
F(void *, p_Matchmaking, (void))
F(void *, p_Friends, (void))
F(void *, p_User, (void))
F(void *, p_Utils, (void))
F(int, p_SendToUser, (void *, const SteamNetworkingIdentity *, const void *, uint32_t, int, int))
F(int, p_Receive, (void *, int, SteamNetworkingMessage **, int))
F(int, p_Accept, (void *, const SteamNetworkingIdentity *))
F(int, p_CloseSession, (void *, const SteamNetworkingIdentity *))
F(void, p_MsgRelease, (SteamNetworkingMessage *))
F(void, p_IdSet64, (SteamNetworkingIdentity *, uint64_t))
F(uint64_t, p_IdGet64, (const SteamNetworkingIdentity *))
F(void, p_InitRelay, (void *))
F(SteamAPICall_t, p_CreateLobby, (void *, int, int))
F(SteamAPICall_t, p_JoinLobby, (void *, uint64_t))
F(void, p_LeaveLobby, (void *, uint64_t))
F(uint64_t, p_LobbyOwner, (void *, uint64_t))
F(void, p_InviteDialog, (void *, uint64_t))
F(int, p_SetRichPresence, (void *, const char *, const char *))
F(void, p_ClearRichPresence, (void *))
F(const char *, p_PersonaName, (void *))
F(uint64_t, p_UserSteamID, (void *))
#undef F

static struct {
    int loaded, running;                             /* the DLL's functions found; SteamAPI_InitFlat said yes */
    HSteamPipe pipe;
    void *msgs, *mm, *friends, *user;
    int role;                                        /* NET_HOST / NET_GUEST, 0 not in a Steam session */
    uint64_t lobby, peer, me;
    int connected, announced;
    SteamAPICall_t create_call, join_call;
    double last_heard, now;
    SteamNetworkingMessage *held;                    /* the message the last NT_DATA points into */
    unsigned out, in;
    char why[128];
    uint64_t pending_lobby;                          /* +connect_lobby from the command line, joined once Steam runs */
} s;

static int load(void)
{
    if (s.loaded)
        return s.loaded > 0;
    s.loaded = -1;
    HMODULE m = LoadLibraryA("steam_api64.dll");
    if (!m)
        return 0;
#define L(var, name) if (!(*(FARPROC *)&var = GetProcAddress(m, name))) return 0;
    L(p_InitFlat, "SteamAPI_InitFlat")
    L(p_Shutdown, "SteamAPI_Shutdown")
    L(p_GetHSteamPipe, "SteamAPI_GetHSteamPipe")
    L(p_MD_Init, "SteamAPI_ManualDispatch_Init")
    L(p_MD_RunFrame, "SteamAPI_ManualDispatch_RunFrame")
    L(p_MD_GetNext, "SteamAPI_ManualDispatch_GetNextCallback")
    L(p_MD_FreeLast, "SteamAPI_ManualDispatch_FreeLastCallback")
    L(p_MD_GetCallResult, "SteamAPI_ManualDispatch_GetAPICallResult")
    L(p_Messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002")
    L(p_Matchmaking, "SteamAPI_SteamMatchmaking_v009")
    L(p_Friends, "SteamAPI_SteamFriends_v017")
    L(p_User, "SteamAPI_SteamUser_v023")
    L(p_Utils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004")
    L(p_SendToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser")
    L(p_Receive, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel")
    L(p_Accept, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser")
    L(p_CloseSession, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser")
    L(p_MsgRelease, "SteamAPI_SteamNetworkingMessage_t_Release")
    L(p_IdSet64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64")
    L(p_IdGet64, "SteamAPI_SteamNetworkingIdentity_GetSteamID64")
    L(p_InitRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess")
    L(p_CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby")
    L(p_JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby")
    L(p_LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby")
    L(p_LobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner")
    L(p_InviteDialog, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog")
    L(p_SetRichPresence, "SteamAPI_ISteamFriends_SetRichPresence")
    L(p_ClearRichPresence, "SteamAPI_ISteamFriends_ClearRichPresence")
    L(p_PersonaName, "SteamAPI_ISteamFriends_GetPersonaName")
    L(p_UserSteamID, "SteamAPI_ISteamUser_GetSteamID")
#undef L
    s.loaded = 1;
    return 1;
}

int nts_start(void)
{
    if (s.running)
        return 0;
    if (!load()) {
        snprintf(s.why, sizeof s.why, "Steam is not here (no steam_api64.dll beside the game)");
        return -1;
    }
    char err[1024] = "";
    if (p_InitFlat(err) != 0) {
        snprintf(s.why, sizeof s.why, "Steam did not start: %.100s", err[0] ? err : "is Steam running?");
        net_log("%s", s.why);
        return -1;
    }
    p_MD_Init();
    s.pipe = p_GetHSteamPipe();
    s.msgs = p_Messages();
    s.mm = p_Matchmaking();
    s.friends = p_Friends();
    s.user = p_User();
    s.me = p_UserSteamID(s.user);
    p_InitRelay(p_Utils());
    s.running = 1;
    net_log("Steam: signed in as %s", p_PersonaName(s.friends));
    return 0;
}

int nts_available(void) { return s.running; }
const char *nts_why(void) { return s.why; }
const char *nts_name(void) { return s.running ? p_PersonaName(s.friends) : ""; }
uint64_t nts_lobby(void) { return s.lobby; }

static void identity(SteamNetworkingIdentity *id, uint64_t steam_id)
{
    memset(id, 0, sizeof *id);
    p_IdSet64(id, steam_id);
}

static void leave(void)
{
    if (s.held) {
        p_MsgRelease(s.held);
        s.held = NULL;
    }
    if (!s.running)
        return;
    if (s.peer) {
        SteamNetworkingIdentity id;
        identity(&id, s.peer);
        p_CloseSession(s.msgs, &id);
    }
    if (s.lobby)
        p_LeaveLobby(s.mm, s.lobby);
    p_ClearRichPresence(s.friends);
    s.role = 0;
    s.lobby = s.peer = 0;
    s.connected = s.announced = 0;
    s.create_call = s.join_call = 0;
}

int nts_host(void)
{
    if (nts_start() != 0)
        return -1;
    leave();
    s.role = NET_HOST;
    s.create_call = p_CreateLobby(s.mm, 1 /* friends only */, 2);
    net_log("Steam: making a lobby");
    return 0;
}

int nts_join_lobby(uint64_t lobby)
{
    if (nts_start() != 0) {
        s.pending_lobby = lobby;
        return -1;
    }
    leave();
    s.role = NET_GUEST;
    s.join_call = p_JoinLobby(s.mm, lobby);
    net_log("Steam: joining lobby %llu", (unsigned long long)lobby);
    return 0;
}

void nts_invite(void)
{
    if (s.running && s.lobby)
        p_InviteDialog(s.friends, s.lobby);
}

void nts_close(void) { leave(); }

/* +connect_lobby <id> on the command line (Steam starts the game so from an invite) */
void nts_command_line(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "+connect_lobby"))
            s.pending_lobby = strtoull(argv[i + 1], NULL, 10);
}

uint64_t nts_take_pending(void)
{
    uint64_t l = s.pending_lobby;
    s.pending_lobby = 0;
    return l;
}

static void lobby_entered(uint64_t lobby)
{
    s.lobby = lobby;
    char conn[64];
    snprintf(conn, sizeof conn, "+connect_lobby %llu", (unsigned long long)lobby);
    p_SetRichPresence(s.friends, "connect", conn);
    if (s.role == NET_GUEST) {
        s.peer = p_LobbyOwner(s.mm, lobby);
        if (!s.peer || s.peer == s.me) {
            net_log("Steam: the lobby has no host");
            s.role = 0;
            return;
        }
        s.connected = 1;                             /* the messages' session opens with the first one sent */
        s.last_heard = s.now;
    }
}

/* the callbacks; returns an event for nt_poll when one happened */
static int callbacks(NtEvent *ev)
{
    CallbackMsg m;
    int got = 0;
    p_MD_RunFrame(s.pipe);
    while (!got && p_MD_GetNext(s.pipe, &m)) {
        switch (m.callback) {
        case CB_CALL_COMPLETED: {
            SteamAPICall_t call;
            int which;
            memcpy(&call, m.param, 8);
            memcpy(&which, m.param + 8, 4);
            uint8_t buf[64];
            int failed = 0;
            if (which == CB_LOBBY_CREATED && call == s.create_call &&
                p_MD_GetCallResult(s.pipe, call, buf, sizeof buf, CB_LOBBY_CREATED, &failed) && !failed) {
                int result;
                uint64_t lobby;
                memcpy(&result, buf, 4);
                memcpy(&lobby, buf + 8, 8);
                if (result == 1) {
                    lobby_entered(lobby);
                    net_log("Steam: lobby ready; invite a friend (Shift+Tab, or the panel's Invite)");
                } else
                    net_log("Steam: no lobby (result %d)", result);
            }
            break;
        }
        case CB_LOBBY_ENTER: {
            uint64_t lobby;
            uint32_t response;
            memcpy(&lobby, m.param, 8);
            memcpy(&response, m.param + 16, 4);
            if (s.role == NET_GUEST && response == 1) {
                lobby_entered(lobby);
                ev->type = NT_CONNECT;
                got = 1;
            } else if (s.role == NET_GUEST)
                net_log("Steam: could not enter the lobby (%u)", response);
            break;
        }
        case CB_LOBBY_JOIN_REQUESTED: {              /* an invite accepted while the game runs */
            uint64_t lobby;
            memcpy(&lobby, m.param, 8);
            s.pending_lobby = lobby;
            break;
        }
        case CB_RICH_JOIN_REQUESTED: {               /* "Join Game" on a friend: its "connect" string */
            const char *c = (const char *)m.param + 8, *p = strstr(c, "+connect_lobby ");
            if (p)
                s.pending_lobby = strtoull(p + 15, NULL, 10);
            break;
        }
        case CB_SESSION_REQUEST: {                   /* a guest's first message: the host takes one */
            SteamNetworkingIdentity id;
            memcpy(&id, m.param, sizeof id);
            uint64_t who = p_IdGet64(&id);
            if (s.role == NET_HOST && (!s.peer || s.peer == who)) {
                p_Accept(s.msgs, &id);
                if (!s.peer) {
                    s.peer = who;
                    s.connected = 1;
                    s.last_heard = s.now;
                    ev->type = NT_CONNECT;
                    got = 1;
                }
            }
            break;
        }
        case CB_SESSION_FAILED:
            if (s.connected) {
                s.connected = 0;
                s.peer = s.role == NET_HOST ? 0 : s.peer;
                ev->type = NT_DISCONNECT;
                got = 1;
            }
            break;
        }
        p_MD_FreeLast(s.pipe);
    }
    return got;
}

int nts_poll(NtEvent *ev, double now)
{
    s.now = now;
    ev->type = NT_NONE;
    if (s.held) {
        p_MsgRelease(s.held);
        s.held = NULL;
    }
    if (!s.running)
        return 0;
    if (callbacks(ev))
        return 1;
    if (s.connected && s.peer) {
        SteamNetworkingMessage *m[1];
        if (p_Receive(s.msgs, 0, m, 1) == 1) {
            if (p_IdGet64(&m[0]->peer) != s.peer) {  /* someone else: not ours */
                p_MsgRelease(m[0]);
                return 0;
            }
            s.held = m[0];
            s.in += (unsigned)m[0]->size;
            s.last_heard = now;
            ev->type = NT_DATA;
            ev->data = (const unsigned char *)m[0]->data;
            ev->len = m[0]->size;
            return 1;
        }
        if (now - s.last_heard > 10.0) {             /* nothing for 10 s (the snapshots and pads never stop) */
            s.connected = 0;
            if (s.role == NET_HOST)
                s.peer = 0;
            ev->type = NT_DISCONNECT;
            return 1;
        }
    }
    return 0;
}

int nts_send(int reliable, const void *data, int len)
{
    if (!s.running || !s.connected || !s.peer)
        return -1;
    SteamNetworkingIdentity id;
    identity(&id, s.peer);
    s.out += (unsigned)len;
    return p_SendToUser(s.msgs, &id, data, (uint32_t)len,
                        (reliable ? SEND_RELIABLE : SEND_UNRELIABLE_NO_NAGLE) | SEND_AUTO_RESTART, 0) == 1 ? 0 : -1;
}

int nts_connected(void) { return s.connected; }
unsigned nts_bytes_out(void) { return s.out; }
unsigned nts_bytes_in(void) { return s.in; }

void nts_shutdown(void)
{
    leave();
    if (s.running)
        p_Shutdown();
    s.running = 0;
}

#else                                                /* no Steam outside Windows yet */
int nts_start(void) { return -1; }
int nts_available(void) { return 0; }
const char *nts_why(void) { return "Steam: Windows only for now"; }
const char *nts_name(void) { return ""; }
uint64_t nts_lobby(void) { return 0; }
int nts_host(void) { return -1; }
int nts_join_lobby(uint64_t lobby) { (void)lobby; return -1; }
void nts_invite(void) {}
void nts_close(void) {}
void nts_command_line(int argc, char **argv) { (void)argc; (void)argv; }
uint64_t nts_take_pending(void) { return 0; }
int nts_poll(NtEvent *ev, double now) { (void)now; ev->type = NT_NONE; return 0; }
int nts_send(int reliable, const void *data, int len) { (void)reliable; (void)data; (void)len; return -1; }
int nts_connected(void) { return 0; }
unsigned nts_bytes_out(void) { return 0; }
unsigned nts_bytes_in(void) { return 0; }
void nts_shutdown(void) {}
#endif

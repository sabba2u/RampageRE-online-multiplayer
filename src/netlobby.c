/* netlobby.c - online play's game list (netlobby.h): a host lists itself with the lobby (lobby/), a guest browses
 * the list or asks for a join code, and joins over ENet as typing the address would.
 *
 * The lobby speaks plain HTTP (lobby/src/http.ts): POST /games to list a game (a form body; with Accept: text/plain
 * it answers "ok <code> <token> <heartbeat>"), PUT /games/<code> with the token as the heartbeat (404: the lobby
 * forgot us, list again), DELETE /games/<code> to go; GET /games.txt and GET /games/<code>.txt for the guest. Each
 * request runs on a thread of its own (one of each kind at a time) and leaves its answer for netlobby_update, which
 * the game's frame calls through net_update: the frame only ever looks at finished answers.
 *
 * Hole punching (netstun.h): the host lists its STUN address too (stun=) and asks for its knocks every second
 * (GET /games/<code>/knocks?token=); a guest that joins by code knocks (POST /games/<code>/knock, its own STUN
 * address and ENet port) once STUN has answered, and the host punches toward it.
 *
 * Built with -DNETLOBBY_TEST (tests/build_net_test.bat) only the parsers are here: no network, no net.c. */
#include "netlobby.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char CODE_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   /* lobby/src/Games.ts: no 0 O 1 I */
#define CODE_LENGTH 5

static void copy(char *to, int cap, const char *from, int n)   /* n bytes of from (or less), always ended */
{
    if (cap <= 0)
        return;
    if (n > cap - 1)
        n = cap - 1;
    memcpy(to, from, (size_t)n);
    to[n] = 0;
}

/* ---- the parts without a network ---------------------------------------------------------------------------- */

int netlobby_parse_url(const char *url, char *host, int hcap, int *port, char *path, int pcap, int *https)
{
    const char *p = url;
    while (*p == ' ')
        p++;
    *https = 0;
    if (!strncmp(p, "http://", 7))
        p += 7;
    else if (!strncmp(p, "https://", 8)) {
        p += 8;
        *https = 1;
    } else if (strstr(p, "://"))
        return -1;                                   /* (no scheme: "host:port", taken as http) */
    const char *h = p;
    while (*p && *p != ':' && *p != '/' && *p != '?')
        p++;
    if (p == h || p - h >= hcap)
        return -1;
    for (const char *c = h; c < p; c++)
        if (!isalnum((unsigned char)*c) && *c != '.' && *c != '-')
            return -1;
    copy(host, hcap, h, (int)(p - h));
    *port = *https ? 443 : 80;
    if (*p == ':') {
        char *end;
        long n = strtol(p + 1, &end, 10);
        if (end == p + 1 || n < 1 || n > 65535)
            return -1;
        *port = (int)n;
        p = end;
    }
    if (*p && *p != '/')
        return -1;
    int n = (int)strlen(p);
    while (n > 0 && (p[n - 1] == '/' || p[n - 1] == ' ' || p[n - 1] == '\r' || p[n - 1] == '\n'))
        n--;                                         /* (requests add their own "/games...") */
    if (n >= pcap)
        return -1;
    copy(path, pcap, p, n);
    return 0;
}

int netlobby_is_code(const char *s)
{
    int n = 0;
    for (; s[n]; n++)
        if (!strchr(CODE_ALPHABET, toupper((unsigned char)s[n])) || n >= CODE_LENGTH)
            return 0;
    return n == CODE_LENGTH;
}

int netlobby_join_kind(const char *arg, char *out, int cap)
{
    out[0] = 0;
    if (!arg)
        return NLJ_NONE;
    while (*arg == ' ')
        arg++;
    char s[80];
    copy(s, sizeof s, arg, (int)strlen(arg));
    if (!strncmp(s, "rampage://", 10)) {             /* a link from the lobby's page (lobby/launcher/) */
        if (strncmp(s + 10, "join/", 5))
            return NLJ_NONE;
        memmove(s, s + 15, strlen(s + 15) + 1);
    }
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == '/' || s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    if (!n || n >= cap)
        return NLJ_NONE;
    if (netlobby_is_code(s)) {
        for (int i = 0; i < n; i++)
            out[i] = (char)toupper((unsigned char)s[i]);
        out[n] = 0;
        return NLJ_CODE;
    }
    /* a host name or IPv4, maybe a port: letters, digits, dots, dashes, one colon and digits after it */
    const char *colon = strchr(s, ':');
    for (const char *c = s; *c; c++) {
        if (c == colon) {
            if (!c[1] || c == s)
                return NLJ_NONE;
            for (const char *d = c + 1; *d; d++)
                if (!isdigit((unsigned char)*d))
                    return NLJ_NONE;
            break;
        }
        if (!isalnum((unsigned char)*c) && *c != '.' && *c != '-')
            return NLJ_NONE;
    }
    if (colon && (atoi(colon + 1) < 1 || atoi(colon + 1) > 65535))
        return NLJ_NONE;
    copy(out, cap, s, n);
    return NLJ_ADDR;
}

/* the next tab-separated field of line (up to its end), into out; the rest after it */
static const char *field(const char *p, const char *end, char *out, int cap)
{
    const char *f = p;
    while (p < end && *p != '\t')
        p++;
    copy(out, cap, f, (int)(p - f));
    return p < end ? p + 1 : p;
}

int netlobby_parse_games(const char *text, NetLobbyGame *out, int max)
{
    if (strncmp(text, "rampage-lobby 1", 15) || (text[15] && text[15] != '\n' && text[15] != '\r'))
        return -1;
    const char *p = strchr(text, '\n');
    int n = 0;
    while (p && *p && n < max) {
        p++;
        const char *end = strchr(p, '\n');
        if (!end)
            end = p + strlen(p);
        const char *e = end > p && end[-1] == '\r' ? end - 1 : end;
        NetLobbyGame g;
        memset(&g, 0, sizeof g);
        char num[3][12] = { "", "", "" };
        const char *q = p;
        q = field(q, e, g.code, sizeof g.code);
        q = field(q, e, g.join, sizeof g.join);
        q = field(q, e, num[0], sizeof num[0]);
        q = field(q, e, num[1], sizeof num[1]);
        q = field(q, e, g.build, sizeof g.build);
        q = field(q, e, g.name, sizeof g.name);
        q = field(q, e, g.monster, sizeof g.monster);
        q = field(q, e, g.city, sizeof g.city);
        field(q, e, num[2], sizeof num[2]);          /* (kept slots: a newer lobby's ninth field, else 0) */
        g.players = atoi(num[0]);
        g.max = atoi(num[1]);
        g.reserved = atoi(num[2]);
        if (netlobby_is_code(g.code) && g.max > 0)   /* (a line short of fields, a blank one: skipped) */
            out[n++] = g;
        p = *end ? end : NULL;
    }
    return n;
}

int netlobby_parse_registered(const char *text, char *code, int ccap, char *token, int tcap, int *heartbeat)
{
    char c[16], t[80];
    int hb = 0;
    if (sscanf(text, "ok %15s %79s %d", c, t, &hb) < 2 || !netlobby_is_code(c))
        return -1;
    copy(code, ccap, c, (int)strlen(c));
    copy(token, tcap, t, (int)strlen(t));
    *heartbeat = hb >= 2 && hb <= 60 ? hb : 10;
    return 0;
}

#ifndef NETLOBBY_TEST
/* ---- the network ---------------------------------------------------------------------------------------------- */
#include "net.h"
#include "netstun.h"
#include "nettransport.h"
#include "netwire.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

/* one HTTP request, blocking (a job's thread, or the exit's unlisting): the status (200 ...), -1 no answer. out the
 * body, ended, cut at cap. Sends body as a form (application/x-www-form-urlencoded), asks for text/plain */
#ifdef _WIN32
static int http_request(const char *method, const char *url, const char *body, char *out, int cap, int timeout_ms)
{
    char host[128], path[256];
    int port, https;
    out[0] = 0;
    if (netlobby_parse_url(url, host, sizeof host, &port, path, sizeof path, &https))
        return -1;
    wchar_t wh[128], wp[256], wm[8];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, wh, 128);
    MultiByteToWideChar(CP_UTF8, 0, path[0] ? path : "/", -1, wp, 256);
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wm, 8);
    int status = -1;
    HINTERNET s = WinHttpOpen(L"RampageRE", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, wh, (INTERNET_PORT)port, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, wm, wp, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         https ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r) {
        WinHttpSetTimeouts(r, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
        DWORD len = body ? (DWORD)strlen(body) : 0;
        static const wchar_t HEADERS[] = L"Accept: text/plain\r\nContent-Type: application/x-www-form-urlencoded\r\n";
        if (WinHttpSendRequest(r, HEADERS, (DWORD)-1, (void *)(body ? body : ""), len, len, 0) &&
            WinHttpReceiveResponse(r, NULL)) {
            DWORD code = 0, sz = sizeof code;
            if (WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX))
                status = (int)code;
            int got = 0;
            DWORD n;
            while (got < cap - 1 && WinHttpReadData(r, out + got, (DWORD)(cap - 1 - got), &n) && n > 0)
                got += (int)n;
            out[got] = 0;
        }
        WinHttpCloseHandle(r);
    }
    if (c)
        WinHttpCloseHandle(c);
    if (s)
        WinHttpCloseHandle(s);
    return status;
}
#else
/* elsewhere: HTTP/1.0 over a socket (the lobby is Node's http server: it takes that), http:// only */
static int http_request(const char *method, const char *url, const char *body, char *out, int cap, int timeout_ms)
{
    char host[128], path[256], ports[8];
    int port, https;
    out[0] = 0;
    if (netlobby_parse_url(url, host, sizeof host, &port, path, sizeof path, &https) || https)
        return -1;
    snprintf(ports, sizeof ports, "%d", port);
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;                       /* (the lobby listens on IPv4: lobby/src/index.ts) */
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, ports, &hints, &ai) || !ai)
        return -1;
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol), status = -1;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    if (fd >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);   /* (Linux: connect's too) */
    }
    if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
        static __thread char req[1024];
        int len = body ? (int)strlen(body) : 0;
        int n = snprintf(req, sizeof req, "%s %s HTTP/1.0\r\nHost: %s:%d\r\nAccept: text/plain\r\nContent-Type: "
                         "application/x-www-form-urlencoded\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                         method, path[0] ? path : "/", host, port, len, body ? body : "");
        if (n > 0 && n < (int)sizeof req && send(fd, req, (size_t)n, 0) == n) {
            static __thread char buf[16384 + 1024];
            int got = 0, r;
            while (got < (int)sizeof buf - 1 && (r = (int)recv(fd, buf + got, sizeof buf - 1 - (size_t)got, 0)) > 0)
                got += r;
            buf[got] = 0;
            int code;
            char *b = strstr(buf, "\r\n\r\n");
            if (sscanf(buf, "HTTP/%*d.%*d %d", &code) == 1 && b) {
                status = code;
                copy(out, cap, b + 4, (int)strlen(b + 4));
            }
        }
    }
    if (fd >= 0)
        close(fd);
    freeaddrinfo(ai);
    return status;
}
#endif

/* this PC's address on its network (the one its default route leaves by: a UDP socket "connected" anywhere sends
 * nothing, and getsockname says which address it would use), for guests on the host's own network. "" none */
static void local_ip(char *out, int cap)
{
    out[0] = 0;
    struct sockaddr_in to, me;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(0xC0000201u);         /* 192.0.2.1: TEST-NET, never reached */
    socklen_t len = sizeof me;
#ifdef _WIN32
    WSADATA wsa;                                     /* (ENet's is up while hosting; counted, so asking again is fine) */
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
        return;
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return;
#endif
    if (connect(s, (struct sockaddr *)&to, sizeof to) == 0 && getsockname(s, (struct sockaddr *)&me, &len) == 0) {
        uint32_t a = ntohl(me.sin_addr.s_addr);
        int priv = (a >> 24) == 10 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8 || (a >> 22) == (100u << 2 | 1);
        if (priv)                                    /* (10/8, 172.16/12, 192.168/16, 100.64/10; else no LAN one) */
            snprintf(out, (size_t)cap, "%u.%u.%u.%u", a >> 24, a >> 16 & 255, a >> 8 & 255, a & 255);
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

/* ---- the jobs: one request of each kind at a time, on a thread of its own ---- */
enum { J_LIST, J_LOOKUP, J_REGISTER, J_BEAT, J_UNLIST, J_KNOCKS, J_KNOCK, J_COUNT };
enum { JOB_IDLE, JOB_RUNNING, JOB_DONE };
typedef struct {
    int state;                                       /* JOB_*: the frame sets RUNNING, the thread DONE */
    char method[8], url[384], body[768];
    int status, register_lan;
    char out[16384];
} Job;
static Job g_jobs[J_COUNT];

static void job_run(Job *j)
{
    if (j->register_lan) {                           /* (found here: a socket call, off the frame) */
        char ip[32];
        local_ip(ip, sizeof ip);
        if (ip[0]) {
            size_t n = strlen(j->body);
            snprintf(j->body + n, sizeof j->body - n, "&lan=%s", ip);
        }
    }
    j->status = http_request(j->method, j->url, j->body, j->out, sizeof j->out, 4000);
    __atomic_store_n(&j->state, JOB_DONE, __ATOMIC_RELEASE);
}

#ifdef _WIN32
static DWORD WINAPI job_thread(LPVOID p) { job_run((Job *)p); return 0; }
#else
static void *job_thread(void *p) { job_run((Job *)p); return NULL; }
#endif

static int job_idle(int k) { return __atomic_load_n(&g_jobs[k].state, __ATOMIC_ACQUIRE) == JOB_IDLE; }

/* starts job k (idle): method, the lobby URL + path, the form body */
static void job_start(int k, const char *method, const char *base, const char *path, const char *body, int lan)
{
    Job *j = &g_jobs[k];
    snprintf(j->method, sizeof j->method, "%s", method);
    snprintf(j->url, sizeof j->url, "%s%s", base, path);
    snprintf(j->body, sizeof j->body, "%s", body ? body : "");
    j->register_lan = lan;
    j->out[0] = 0;
    j->status = -1;
    __atomic_store_n(&j->state, JOB_RUNNING, __ATOMIC_RELEASE);
#ifdef _WIN32
    HANDLE t = CreateThread(NULL, 0, job_thread, j, 0, NULL);
    if (t)
        CloseHandle(t);
    else
        job_run(j);                                  /* (no thread: answered now, -1 or not) */
#else
    pthread_t t;
    if (pthread_create(&t, NULL, job_thread, j) == 0)
        pthread_detach(t);
    else
        job_run(j);
#endif
}

/* job k finished: 1, its status and body in g_jobs[k] until the next job_start (the job is idle again) */
static int job_done(int k)
{
    if (__atomic_load_n(&g_jobs[k].state, __ATOMIC_ACQUIRE) != JOB_DONE)
        return 0;
    g_jobs[k].state = JOB_IDLE;
    return 1;
}

/* the lobby's "error <why>" (lobby/src/http.ts), else what went wrong on the way */
static void why(char *to, int cap, const char *what, const Job *j)
{
    if (j->status < 0)
        snprintf(to, (size_t)cap, "%s: the lobby is not reachable", what);
    else if (!strncmp(j->out, "error ", 6)) {
        int n = (int)strcspn(j->out + 6, "\r\n");
        snprintf(to, (size_t)cap, "%s: %.*s", what, n > 100 ? 100 : n, j->out + 6);
    } else
        snprintf(to, (size_t)cap, "%s: the lobby answered %d", what, j->status);
}

/* form values: letters, digits and -._~ as they are, the rest %XX */
static void form_add(char *b, int cap, const char *key, const char *val)
{
    size_t n = strlen(b);
    n += (size_t)snprintf(b + n, (size_t)cap - n, "%s%s=", n ? "&" : "", key);
    for (const unsigned char *c = (const unsigned char *)val; *c && n + 4 < (size_t)cap; c++) {
        if (isalnum(*c) || strchr("-._~", *c))
            b[n++] = (char)*c;
        else
            n += (size_t)snprintf(b + n, (size_t)cap - n, "%%%02X", *c);
    }
    b[n] = 0;
}

/* ---- the state ---- */
static struct {
    int init, url_fixed, listing;
    char url[256], name[32], monster[32], city[40], external[80];
    /* the host's listing */
    char code[8], token[80], listed_url[256], host_status[160], sent_stun[24];
    int heartbeat, sent_players;
    double beat_at, retry_at, knocks_at;
    /* the guest's knock: the code it joins, until when it waits for STUN's answer before knocking without it */
    char knock[8];
    double knock_until;
    /* the guest's list */
    NetLobbyGame games[NETLOBBY_GAMES];
    int ngames, browsing, list_ok;
    double list_at, browse_at;
    char list_status[160], lookup[8];
} L;

static void save(void)
{
    FILE *f = fopen("netlobby.cfg", "w");
    if (!f)
        return;
    fprintf(f, "url=%s\nlist=%d\nname=%s\n", L.url, L.listing, L.name);
    fclose(f);
}

static void unlist_now(int timeout_ms);
static void at_exit(void) { unlist_now(1500); }      /* (a quit while listed: gone from the list at once) */

static void init(void)
{
    if (L.init)
        return;
    L.init = 1;
    L.listing = 1;
    L.list_at = -1e9;
    char saved[256] = NETLOBBY_DEFAULT_URL;
    const char *user = getenv("USERNAME");
    if (!user)
        user = getenv("USER");
    snprintf(L.name, sizeof L.name, "%s", user ? user : "Rampage host");
    FILE *f = fopen("netlobby.cfg", "r");
    if (f) {
        char line[300];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "url=", 4) && line[4])
                snprintf(saved, sizeof saved, "%s", line + 4);
            else if (!strncmp(line, "list=", 5))
                L.listing = atoi(line + 5) != 0;
            else if (!strncmp(line, "name=", 5) && line[5])
                snprintf(L.name, sizeof L.name, "%s", line + 5);
        }
        fclose(f);
    }
    if (!L.url_fixed) {
        const char *env = getenv("RAMPAGE_LOBBY");
        snprintf(L.url, sizeof L.url, "%s", env && *env ? env : saved);
    }
    atexit(at_exit);
}

const char *netlobby_url(void) { init(); return L.url; }
int netlobby_listing(void) { init(); return L.listing; }
const char *netlobby_name(void) { init(); return L.name; }
const char *netlobby_my_code(void) { return L.code; }
const char *netlobby_host_status(void) { return L.host_status; }
const char *netlobby_list_status(void) { return L.list_status; }
int netlobby_count(void) { return L.ngames; }
const NetLobbyGame *netlobby_game(int i) { return i >= 0 && i < L.ngames ? &L.games[i] : NULL; }

void netlobby_set_url(const char *url, int save_it)
{
    if (!save_it)                                    /* --lobby: before init, which then leaves it */
        L.url_fixed = 1;
    init();
    char s[256];
    snprintf(s, sizeof s, "%s", url ? url : "");
    size_t n = strlen(s);
    while (n && (s[n - 1] == '/' || s[n - 1] == ' '))
        s[--n] = 0;
    if (!strcmp(s, L.url))
        return;
    snprintf(L.url, sizeof L.url, "%s", s);
    L.ngames = 0;
    L.list_at = 0;
    L.list_status[0] = 0;
    if (save_it)
        save();
}

void netlobby_set_listing(int on) { init(); L.listing = on != 0; save(); }

void netlobby_set_name(const char *name)
{
    init();
    snprintf(L.name, sizeof L.name, "%s", name ? name : "");
    L.beat_at = 0;                                   /* (the next heartbeat says it) */
    save();
}

void netlobby_set_game(const char *monster, const char *city)
{
    if (strcmp(monster ? monster : "", L.monster) || strcmp(city ? city : "", L.city))
        L.beat_at = 0;
    snprintf(L.monster, sizeof L.monster, "%s", monster ? monster : "");
    snprintf(L.city, sizeof L.city, "%s", city ? city : "");
}

void netlobby_set_external(const char *addr)
{
    snprintf(L.external, sizeof L.external, "%s", addr ? addr : "");
    if (L.code[0])
        L.beat_at = 0;
}

int netlobby_same_build(const NetLobbyGame *g)
{
    char b[12];
    snprintf(b, sizeof b, "%08X", (unsigned)net_build_id());
    return !strcmp(g->build, b);
}

void netlobby_browse(void) { L.browsing = 1; }
void netlobby_refresh(void) { L.list_at = -1e9; L.browsing = 1; }

void netlobby_lookup(const char *code)
{
    init();
    snprintf(L.lookup, sizeof L.lookup, "%s", code);
    snprintf(L.list_status, sizeof L.list_status, "Looking up code %s...", code);
    net_log("asking the lobby (%s) for code %s", L.url, code);
    if (!job_idle(J_LOOKUP))
        return;                                      /* (one at a time: the newest asked when it is back) */
    char path[32];
    snprintf(path, sizeof path, "/games/%s.txt", code);
    job_start(J_LOOKUP, "GET", L.url, path, NULL, 0);
    L.lookup[0] = 0;
}

/* the listing's players: the host, its guests and its local players (net.h's slots); a kept slot counts apart
 * ("1 / 2 (1 rejoining)") and is not open to others. Changed numbers: a heartbeat at once */
static int players_now(void)
{
    return net_slot_count(NSL_HOST) + net_slot_count(NSL_ONLINE) + net_slot_count(NSL_LOCAL) +
           16 * net_slot_count(NSL_RESERVED);        /* (packed with the reserved, for "changed") */
}

static void game_fields(char *b, int cap)
{
    char n[8];
    form_add(b, cap, "name", L.name);
    form_add(b, cap, "monster", L.monster);
    form_add(b, cap, "city", L.city);
    snprintf(n, sizeof n, "%d", players_now() % 16);
    form_add(b, cap, "players", n);
    snprintf(n, sizeof n, "%d", net_slots());
    form_add(b, cap, "max", n);
    snprintf(n, sizeof n, "%d", players_now() / 16);
    form_add(b, cap, "reserved", n);
    if (L.external[0])
        form_add(b, cap, "external", L.external);
    if (netstun_public()[0])
        form_add(b, cap, "stun", netstun_public());
    snprintf(L.sent_stun, sizeof L.sent_stun, "%s", netstun_public());
}

/* the DELETE: on a thread (the frame's), or at once (the exit's, timeout_ms > 0) */
static void unlist(int timeout_ms)
{
    if (!L.code[0])
        return;
    char path[200], body[128] = "";
    snprintf(path, sizeof path, "/games/%s", L.code);
    form_add(body, sizeof body, "token", L.token);
    if (timeout_ms > 0) {
        char out[256];
        char url[512];
        snprintf(url, sizeof url, "%s%s", L.listed_url, path);
        http_request("DELETE", url, body, out, sizeof out, timeout_ms);
    } else if (job_idle(J_UNLIST))
        job_start(J_UNLIST, "DELETE", L.listed_url, path, body, 0);
    net_log("lobby: unlisted %s", L.code);
    L.code[0] = L.token[0] = 0;
    L.host_status[0] = 0;
}

static void unlist_now(int timeout_ms) { unlist(timeout_ms); }

static void host_frame(double now)
{
    int want = net_role() == NET_HOST && !net_is_steam() && net_state() != NETS_FAILED && L.listing && L.url[0];
    if (job_done(J_REGISTER)) {
        Job *j = &g_jobs[J_REGISTER];
        int hb;
        if (j->status == 200 && !netlobby_parse_registered(j->out, L.code, sizeof L.code, L.token, sizeof L.token, &hb)) {
            L.heartbeat = hb;
            L.beat_at = now;
            L.sent_players = players_now();
            snprintf(L.listed_url, sizeof L.listed_url, "%s", j->url);
            L.listed_url[strlen(L.listed_url) - strlen("/games")] = 0;
            snprintf(L.host_status, sizeof L.host_status, "Listed in the lobby: join code %s", L.code);
            net_log("lobby: listed as %s at %s", L.code, L.listed_url);
        } else {
            static char said[sizeof L.host_status];
            why(L.host_status, sizeof L.host_status, "Not listed", j);
            if (strcmp(said, L.host_status))         /* (said once, not at every try) */
                net_log("lobby: %s", L.host_status);
            memcpy(said, L.host_status, sizeof said);
            L.retry_at = now + 10;
        }
    }
    if (job_done(J_BEAT)) {
        Job *j = &g_jobs[J_BEAT];
        if (j->status == 404 && L.code[0]) {         /* the lobby forgot us (restarted, or we were silent) */
            net_log("lobby: %s was dropped, listing again", L.code);
            L.code[0] = 0;
            L.retry_at = 0;
        } else if (j->status != 200 && j->status != 204 && L.code[0])
            why(L.host_status, sizeof L.host_status, "The lobby did not hear the heartbeat", j);
        else if (L.code[0])
            snprintf(L.host_status, sizeof L.host_status, "Listed in the lobby: join code %s", L.code);
    }
    job_done(J_UNLIST);
    if (job_done(J_KNOCKS) && g_jobs[J_KNOCKS].status == 200) {
        char knocks[8][24];
        int n = netstun_parse_knocks(g_jobs[J_KNOCKS].out, knocks, 8);
        for (int i = 0; i < n; i++)
            netstun_punch(knocks[i]);
    }
    if (!want) {
        unlist(0);
        L.retry_at = 0;
        return;
    }
    if (!L.code[0]) {
        if (job_idle(J_REGISTER) && now >= L.retry_at) {
            char body[700] = "", n[16];
            game_fields(body, sizeof body);
            snprintf(n, sizeof n, "%d", net_port());
            form_add(body, sizeof body, "port", n);
            snprintf(n, sizeof n, "%08X", (unsigned)net_build_id());
            form_add(body, sizeof body, "build", n);
            job_start(J_REGISTER, "POST", L.url, "/games", body, 1);
            if (!L.host_status[0])                   /* (a failure stays up while it tries again) */
                snprintf(L.host_status, sizeof L.host_status, "Listing the game with %s...", L.url);
            L.retry_at = now + 10;
        }
        return;
    }
    /* a heartbeat every few seconds, and at once when something shown changed (a guest came or went) */
    if (players_now() != L.sent_players || strcmp(netstun_public(), L.sent_stun))
        L.beat_at = 0;
    if (job_idle(J_KNOCKS) && now - L.knocks_at >= 1) {   /* a guest knocking: punched toward within a second */
        char path[160];
        snprintf(path, sizeof path, "/games/%s/knocks?token=%s", L.code, L.token);
        job_start(J_KNOCKS, "GET", L.listed_url, path, NULL, 0);
        L.knocks_at = now;
    }
    if (job_idle(J_BEAT) && now - L.beat_at >= L.heartbeat) {
        char body[700] = "", path[32];
        form_add(body, sizeof body, "token", L.token);
        size_t k = strlen(body);
        body[k] = '&';
        body[k + 1] = 0;
        game_fields(body + k + 1, (int)(sizeof body - k - 1));
        snprintf(path, sizeof path, "/games/%s", L.code);
        job_start(J_BEAT, "PUT", L.listed_url, path, body, 0);
        L.beat_at = now;
        L.sent_players = players_now();
    }
}

static void guest_frame(double now)
{
    if (job_done(J_LIST)) {
        Job *j = &g_jobs[J_LIST];
        int n = j->status == 200 ? netlobby_parse_games(j->out, L.games, NETLOBBY_GAMES) : -1;
        if (n >= 0) {
            L.ngames = n;
            snprintf(L.list_status, sizeof L.list_status, "%d %s", n, n == 1 ? "game" : "games");
        } else {
            L.ngames = 0;
            if (j->status == 200)
                snprintf(L.list_status, sizeof L.list_status, "That is not a RampageRE lobby");
            else
                why(L.list_status, sizeof L.list_status, "No list", j);
        }
    }
    if (job_done(J_LOOKUP)) {
        Job *j = &g_jobs[J_LOOKUP];
        NetLobbyGame g;
        int n = j->status == 200 ? netlobby_parse_games(j->out, &g, 1) : 0;
        if (n == 1 && strcmp(g.join, "-")) {
            snprintf(L.list_status, sizeof L.list_status, "Code %s: joining %s at %s", g.code, g.name, g.join);
            if (!netlobby_same_build(&g))
                net_log("lobby: %s runs another build (%s): the host will likely refuse us", g.code, g.build);
            net_start_join(g.join);
            snprintf(L.knock, sizeof L.knock, "%s", g.code);   /* (knocked once STUN answers: guest_frame) */
            L.knock_until = now + 2;
        } else {
            if (j->status == 404)
                snprintf(L.list_status, sizeof L.list_status, "No game with that code in the lobby");
            else
                why(L.list_status, sizeof L.list_status, "Code not found", j);
            net_log("lobby: %s", L.list_status);
        }
        if (L.lookup[0]) {                           /* asked again meanwhile */
            char again[8];
            snprintf(again, sizeof again, "%s", L.lookup);
            netlobby_lookup(again);
        }
    }
    job_done(J_KNOCK);
    if (L.knock[0] && (net_role() != NET_GUEST || net_state() == NETS_FAILED))
        L.knock[0] = 0;                              /* (stopped meanwhile) */
    if (L.knock[0] && job_idle(J_KNOCK) && (netstun_done() || now > L.knock_until)) {
        char body[128] = "", path[32], n[8];
        if (netstun_public()[0])
            form_add(body, sizeof body, "stun", netstun_public());
        snprintf(n, sizeof n, "%d", nt_local_port());
        form_add(body, sizeof body, "port", n);
        snprintf(path, sizeof path, "/games/%s/knock", L.knock);
        job_start(J_KNOCK, "POST", L.url, path, body, 0);
        net_log("lobby: knocking on %s (%s)", L.knock, netstun_public()[0] ? netstun_public() : "no STUN answer");
        L.knock[0] = 0;
    }
    if (L.browsing && L.url[0] && job_idle(J_LIST) && now - L.list_at >= 5) {
        job_start(J_LIST, "GET", L.url, "/games.txt", NULL, 0);
        L.list_at = now;
        if (!L.list_status[0])
            snprintf(L.list_status, sizeof L.list_status, "Asking %s...", L.url);
    }
    L.browsing = 0;
}

void netlobby_update(double now)
{
    init();
    host_frame(now);
    guest_frame(now);
}
#endif

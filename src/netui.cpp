/* netui.cpp - online play's panel (netui.h), the only C++ of the port: Dear ImGui over the game's frame */
#include "netui.h"
#include "net.h"
#include "netwire.h"
#include "nettransport.h"
#include "netupnp.h"
#include "lookfx.h"
#include "netlobby.h"
#ifdef _WIN32
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static int g_ready, g_open;
static ImFont *g_bold;                               /* the chat's and the labels' font (NULL: the panel's) */
extern "C" void *netui_bold_font(void) { return g_bold; }
static ImFont *g_big;                                /* the big labels' (NULL: g_bold scaled) */
static struct { float x, y; char text[48], sub[32]; int big; uint32_t rgb; } g_labels[8];
static int g_nlabels;
static char g_toast[128];
static double g_toast_at = -100;

extern "C" void netui_label(float x, float y, const char *text)
{
    if (g_nlabels < 8) {
        g_labels[g_nlabels].x = x;
        g_labels[g_nlabels].y = y;
        snprintf(g_labels[g_nlabels].text, sizeof g_labels[0].text, "%s", text);
        g_labels[g_nlabels].sub[0] = 0;
        g_labels[g_nlabels].big = 0;
        g_nlabels++;
    }
}

extern "C" void netui_label_big(float x, float y, const char *text, const char *sub, uint32_t rgb)
{
    if (g_nlabels < 8) {
        g_labels[g_nlabels].x = x;
        g_labels[g_nlabels].y = y;
        snprintf(g_labels[g_nlabels].text, sizeof g_labels[0].text, "%s", text);
        snprintf(g_labels[g_nlabels].sub, sizeof g_labels[0].sub, "%s", sub ? sub : "");
        g_labels[g_nlabels].big = 1;
        g_labels[g_nlabels].rgb = rgb;
        g_nlabels++;
    }
}

extern "C" void netui_toast(const char *text)
{
    snprintf(g_toast, sizeof g_toast, "%s", text);
    g_toast_at = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0;
}

/* the toast: 4 s in the middle of the screen, fading over its last second */
static void toast(void)
{
    double age = ImGui::GetTime() - g_toast_at;
    if (!g_toast[0] || age < 0 || age > 4)
        return;
    float a = age > 3 ? (float)(4 - age) : 1;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImFont *f = g_bold ? g_bold : ImGui::GetFont();
    float fs = (g_bold ? g_bold->FontSize : ImGui::GetFontSize()) * 1.4f;
    ImVec2 ds = ImGui::GetIO().DisplaySize, sz = f->CalcTextSizeA(fs, 1e9f, 0, g_toast);
    ImVec2 p(ds.x * 0.5f - sz.x / 2, ds.y * 0.38f - sz.y / 2);
    dl->AddRectFilled(ImVec2(p.x - 18, p.y - 10), ImVec2(p.x + sz.x + 18, p.y + sz.y + 10),
                      IM_COL32(15, 15, 25, (int)(215 * a)), 8);
    dl->AddRect(ImVec2(p.x - 18, p.y - 10), ImVec2(p.x + sz.x + 18, p.y + sz.y + 10), IM_COL32(140, 240, 150, (int)(230 * a)),
                8, 0, 2);
    dl->AddText(f, fs, p, IM_COL32(235, 255, 235, (int)(255 * a)), g_toast);
}

/* a big one: its colour, a black outline under the text, the second line smaller and white under it */
static void label_big(ImDrawList *dl, int i)
{
    ImFont *f = g_big ? g_big : g_bold ? g_bold : ImGui::GetFont();
    float fs = g_big ? g_big->FontSize : (g_bold ? g_bold->FontSize : ImGui::GetFontSize()) * 2;
    ImFont *f2 = g_bold ? g_bold : ImGui::GetFont();
    float fs2 = (g_bold ? g_bold->FontSize : ImGui::GetFontSize()) * 1.15f;
    const char *t = g_labels[i].text, *sub = g_labels[i].sub;
    ImVec2 sz = f->CalcTextSizeA(fs, 1e9f, 0, t), sz2 = sub[0] ? f2->CalcTextSizeA(fs2, 1e9f, 0, sub) : ImVec2(0, 0);
    float w = sz.x > sz2.x ? sz.x : sz2.x, h = sz.y + (sub[0] ? sz2.y + 2 : 0);
    ImVec2 p(g_labels[i].x - w / 2, g_labels[i].y - h);
    uint32_t c = g_labels[i].rgb;
    ImU32 col = IM_COL32(c >> 16 & 255, c >> 8 & 255, c & 255, 255);
    dl->AddRectFilled(ImVec2(p.x - 14, p.y - 6), ImVec2(p.x + w + 14, p.y + h + 6), IM_COL32(10, 10, 15, 210), 8);
    dl->AddRect(ImVec2(p.x - 14, p.y - 6), ImVec2(p.x + w + 14, p.y + h + 6), col, 8, 0, 3);
    ImVec2 tp(g_labels[i].x - sz.x / 2, p.y);
    for (int dx = -2; dx <= 2; dx += 2)              /* (an outline: readable over any sky on a stream) */
        for (int dy = -2; dy <= 2; dy += 2)
            if (dx || dy)
                dl->AddText(f, fs, ImVec2(tp.x + dx, tp.y + dy), IM_COL32(0, 0, 0, 230), t);
    dl->AddText(f, fs, tp, col, t);
    if (sub[0]) {
        ImVec2 sp(g_labels[i].x - sz2.x / 2, p.y + sz.y + 2);
        dl->AddText(f2, fs2, ImVec2(sp.x + 1.5f, sp.y + 1.5f), IM_COL32(0, 0, 0, 230), sub);
        dl->AddText(f2, fs2, sp, IM_COL32(255, 255, 255, 255), sub);
    }
}

static void labels(void)                             /* "IN MENU" over a player's head (netgame.c) */
{
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImFont *f = g_bold ? g_bold : ImGui::GetFont();
    float fs = g_bold ? g_bold->FontSize : ImGui::GetFontSize();
    for (int i = 0; i < g_nlabels; i++) {
        if (g_labels[i].big) {
            label_big(dl, i);
            continue;
        }
        ImVec2 sz = f->CalcTextSizeA(fs, 1e9f, 0, g_labels[i].text), p(g_labels[i].x - sz.x / 2, g_labels[i].y - sz.y);
        dl->AddRectFilled(ImVec2(p.x - 8, p.y - 4), ImVec2(p.x + sz.x + 8, p.y + sz.y + 4), IM_COL32(20, 20, 30, 200), 6);
        dl->AddRect(ImVec2(p.x - 8, p.y - 4), ImVec2(p.x + sz.x + 8, p.y + sz.y + 4), IM_COL32(140, 240, 150, 230), 6);
        dl->AddText(f, fs, ImVec2(p.x + 1.5f, p.y + 1.5f), IM_COL32(0, 0, 0, 220), g_labels[i].text);
        dl->AddText(f, fs, p, IM_COL32(140, 240, 150, 255), g_labels[i].text);
    }
    g_nlabels = 0;
}
static char g_addr[64] = "127.0.0.1";

static const ImVec4 ON = ImVec4(0.55f, 0.95f, 0.6f, 1), OFF = ImVec4(0.7f, 0.7f, 0.72f, 1),
                    BAD = ImVec4(1.0f, 0.5f, 0.45f, 1);

extern "C" int netui_init(void *hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle &st = ImGui::GetStyle();
    st.WindowRounding = 6;
    st.FrameRounding = 4;
    if (GetFileAttributesA("C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    if (GetFileAttributesA("C:\\Windows\\Fonts\\segoeuib.ttf") != INVALID_FILE_ATTRIBUTES)   /* the chat's and the
                                                      * labels': bold, a size up, easy to read over the game */
        g_bold = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 21.0f);
    if (g_bold)                                      /* the player slots' big labels: the same, at 44 px */
        g_big = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 44.0f);
    if (!ImGui_ImplWin32_InitForOpenGL(hwnd) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        ImGui::DestroyContext();
        return -1;
    }
    g_ready = 1;
    return 0;
}

extern "C" void netui_shutdown(void)
{
    if (!g_ready)
        return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_ready = 0;
}

extern "C" void netui_toggle(void) { g_open = !g_open; }
extern "C" int netui_panel_open(void) { return g_ready && g_open; }
extern "C" int netui_wants_input(void) { return g_ready && (g_open || netui_chat_open() || ImGui::GetIO().WantCaptureKeyboard); }
extern "C" int netui_wants_mouse(void) { return g_ready && ImGui::GetIO().WantCaptureMouse; }

extern "C" int netui_wndproc(void *hwnd, unsigned msg, uintptr_t wp, intptr_t lp)
{
    if (!g_ready)
        return 0;
    if (msg == WM_KEYDOWN && wp == VK_INSERT && !(lp & (1 << 30))) {   /* (not the key's repeats) */
        netui_toggle();
        return 1;
    }
    if (!g_open && netui_chat_wndproc(msg, wp, lp))   /* the chat's keys (Enter opens it in play) */
        return 1;
    ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wp, (LPARAM)lp);
    return 0;
}

static void status_line(void)
{
    int st = net_state();
    ImVec4 c = st == NETS_FAILED ? BAD : net_live() ? ON : OFF;
    ImGui::TextColored(c, "%s", net_state_text());
}

/* ---- the player slots (net.h): who plays each, and the host's two buttons: Open for online (a slot a player on this
 * PC took, given back: waiting for a player again, this PC's keys for it ignored until pressed again) and Kick (an
 * online player let go: the slot open at once, no minute kept for them, their rejoin token void) ---- */
static void slots_section(int role)
{
    ImGui::Separator();
    for (int k = 0; k < net_slots(); k++) {
        double left = 0;
        int st = net_slot_state(k, &left);
        const char *name = netgame_slot_name(k);
        char what[96];
        switch (st) {
        case NSL_HOST: snprintf(what, sizeof what, "host%s%s", name[0] ? ", " : "", name); break;
        case NSL_ONLINE: snprintf(what, sizeof what, "%s%s%s", k == net_my_slot() && role == NET_GUEST ? "you, online" :
                                  "online", name[0] ? ", " : "", name); break;
        case NSL_LOCAL: snprintf(what, sizeof what, "local controller%s%s", name[0] ? ", " : "", name); break;
        case NSL_RESERVED: snprintf(what, sizeof what, "disconnected, rejoin %d:%02d", (int)(left + 0.99) / 60,
                                    (int)(left + 0.99) % 60); break;
        default: snprintf(what, sizeof what, "waiting for a player"); break;
        }
        ImGui::PushID(k);
        ImGui::TextColored(st == NSL_RESERVED ? BAD : st == NSL_OPEN ? OFF : ON, "Player %d: %s", k + 1, what);
        if (role == NET_HOST && st == NSL_LOCAL) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Open for online"))
                net_slot_local(k, 0);
        } else if (role == NET_HOST && st == NSL_ONLINE) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Kick"))
                net_slot_kick(k);
        }
        ImGui::PopID();
    }
}

/* ---- the lobby (netlobby.h): the list of games online, join codes, the host's listing ---- */
static char g_code[8];

static void lobby_join_code(void)                    /* offline: a join code from the lobby, said or pasted */
{
    ImGui::SetNextItemWidth(200);
    bool go = ImGui::InputTextWithHint("##code", "K7PQ2", g_code, sizeof g_code,
                                       ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    go |= ImGui::Button("Join code");
    ImGui::SameLine();
    ImGui::TextColored(OFF, "a code from the lobby");
    if (go && g_code[0])
        net_start_join(g_code);                      /* (net.c hands a code to the lobby, then joins its address) */
}

static void lobby_section(void)
{
    int role = net_role();
    if (role == NET_HOST && !net_is_steam()) {       /* the host's listing: its code to pass on */
        ImGui::Separator();
        const char *code = netlobby_my_code();
        if (code[0]) {
            ImGui::TextColored(ON, "Listed in the lobby. Join code:");
            ImGui::SameLine();
            if (g_bold)
                ImGui::PushFont(g_bold);
            ImGui::TextColored(ON, "%s", code);
            if (g_bold)
                ImGui::PopFont();
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
                ImGui::SetClipboardText(code);
        } else
            ImGui::TextColored(OFF, "%s", !netlobby_listing() ? "Not listed in the lobby"
                               : netlobby_host_status()[0] ? netlobby_host_status() : "Listing the game...");
        bool list = netlobby_listing() != 0;
        if (ImGui::Checkbox("List my game in the lobby", &list))
            netlobby_set_listing(list);
    }
    if (!ImGui::CollapsingHeader("Games online", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    static char url[256], name[32];
    static int have;
    if (!have) {                                     /* (the fields start from the settings, once) */
        snprintf(url, sizeof url, "%s", netlobby_url());
        snprintf(name, sizeof name, "%s", netlobby_name());
        have = 1;
    }
    ImGui::SetNextItemWidth(230);
    ImGui::InputText("Lobby", url, sizeof url);
    if (ImGui::IsItemDeactivatedAfterEdit())
        netlobby_set_url(url, 1);
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        netlobby_refresh();
    ImGui::SetNextItemWidth(230);
    ImGui::InputText("Your name", name, sizeof name);
    if (ImGui::IsItemDeactivatedAfterEdit())
        netlobby_set_name(name);
    if (role != NET_OFF && net_state() != NETS_FAILED)
        return;                                      /* (online already: the list would only tempt) */
    netlobby_browse();                               /* shown: kept fresh, every 5 s */
    const char *st = netlobby_list_status();
    ImGui::TextColored(strstr(st, "not") || strstr(st, "No ") ? BAD : OFF, "%s", st[0] ? st : "Asking the lobby...");
    if (!netlobby_count())
        return;
    if (ImGui::BeginTable("games", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Host");
        ImGui::TableSetupColumn("Monster");
        ImGui::TableSetupColumn("City");
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (int i = 0; i < netlobby_count(); i++) {
            const NetLobbyGame *g = netlobby_game(i);
            int same = netlobby_same_build(g), full = g->players + g->reserved >= g->max, none = !strcmp(g->join, "-");
            const char *why = !same ? "Another build of RampageRE: both players need the same one"
                              : full ? (g->reserved ? "A player who dropped out has their place kept (60 s)"
                                                    : "Full: every player slot is taken")
                              : none ? "The lobby has no address for it" : NULL;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            if (why)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g->name);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Code %s, %s%s%s", g->code, g->join, why ? "\n" : "", why ? why : "");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g->monster[0] ? g->monster : "-");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g->city[0] ? g->city : "in the menus");
            ImGui::TableNextColumn();
            if (g->reserved)
                ImGui::Text("%d / %d (%d rejoining)", g->players, g->max, g->reserved);
            else
                ImGui::Text("%d / %d", g->players, g->max);
            ImGui::TableNextColumn();
            if (why) {
                ImGui::PopStyleColor();
                ImGui::TextDisabled("%s", !same ? "other build" : full ? "full" : "no address");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", why);
            } else if (ImGui::SmallButton("Join"))
                net_start_join(g->code);             /* by its code: looked up, joined, knocked (netlobby.c) */
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

static void panel(void)
{
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 420), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Play online (Insert)", &open)) {
        ImGui::End();
        g_open = open;
        return;
    }
    g_open = open;
    int role = net_role();
    ImGui::Text("You are:");
    ImGui::SameLine();
    ImGui::TextColored(role == NET_OFF ? OFF : ON, "%s",
                       role == NET_HOST ? "the host (player 1)" : role == NET_GUEST ? "the guest (player 2)" : "offline");
    status_line();
    ImGui::Separator();
    if (role == NET_OFF || net_state() == NETS_FAILED) {
        if (ImGui::Button("Host a game"))
            net_start_host(NET_PORT);
        ImGui::SameLine();
        ImGui::TextColored(OFF, "Your friend joins you. Port %d (UDP; the router is asked to forward it).", NET_PORT);
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##addr", g_addr, sizeof g_addr);
        ImGui::SameLine();
        if (ImGui::Button("Join"))
            net_start_join(g_addr);
        ImGui::SameLine();
        ImGui::TextColored(OFF, "the host's address");
        lobby_join_code();
        ImGui::Separator();
        if (nts_available()) {
            ImGui::Text("Steam: %s", nts_name());
            if (ImGui::Button("Host on Steam"))
                net_start_host_steam();
            ImGui::SameLine();
            ImGui::TextColored(OFF, "then invite a friend; they join from the invite");
        } else if (ImGui::Button("Use Steam")) {
            if (nts_start() != 0)
                net_log("%s", nts_why());
        }
    } else {
        if (role == NET_GUEST)
            ImGui::Text("Host: %s", net_peer_text());
        if (role == NET_HOST && !net_is_steam()) {   /* the router's forward (netupnp.c): where friends join */
            const char *t;
            int u = net_upnp_state(&t);
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(u == NUP_MAPPED ? ON : u == NUP_FAILED ? BAD : OFF, "Port forward (UPnP): %s",
                               t && *t ? t : "-");
            ImGui::PopTextWrapPos();
        }
        if (role == NET_HOST && net_is_steam()) {
            if (nts_lobby() && ImGui::Button("Invite a friend (Steam)"))
                nts_invite();
            else if (!nts_lobby())
                ImGui::TextColored(OFF, "Making the Steam lobby...");
        }
        NetStats s = net_stats();
        ImGui::Text("Ping: %d ms", s.ping_ms);
        slots_section(role);
        if (net_live() && ImGui::Button(role == NET_HOST ? "Re-synchronize players" : "Ask host to re-sync"))
            netgame_resync();                        /* (every guest takes the host's whole world again: netgame.c) */
        if (net_live()) {
            ImGui::SameLine();
            ImGui::TextColored(OFF, role == NET_HOST ? "every guest takes your whole world again (/resync)"
                                                     : "if your screen and the host's differ (/resync)");
        }
        if (role == NET_GUEST) {                     /* the guest's own monster at once, the host's correcting it */
            bool p = netgame_predict() != 0;
            if (ImGui::Checkbox("Move my monster at once (no delay)", &p))
                netgame_set_predict(p);
        }
        if (role == NET_HOST) {                      /* the updates a second the host sends (netgame.c) */
            int r = netgame_rate();
            ImGui::Text("Updates a second:");
            for (int k = 0; k < 3; k++) {
                static const int R[3] = { 20, 30, 60 };
                ImGui::SameLine();
                char l[8];
                snprintf(l, sizeof l, "%d", R[k]);
                if (ImGui::RadioButton(l, r == R[k]))
                    netgame_set_rate(R[k]);
            }
        }
        ImGui::Text("Updates: %.0f a second, %d bytes each", s.snaps_per_s, s.snap_bytes);
        ImGui::Text("Sent %.1f KB/s, got %.1f KB/s", s.kb_out_per_s, s.kb_in_per_s);
        if (role == NET_HOST) {
            int m = net_mismatches();
            ImGui::TextColored(m ? BAD : ON, "In sync checks: %d, out of sync: %d", net_digest_checks(), m);
            char in[512] = "", out[512] = "", wait[256] = "";   /* each system by name, as the last check found it */
            for (int s = 0; s < net_dsys_count(); s++) {
                int times = 0, st = net_dsys_state(s, &times);
                char *to = st < 0 ? wait : st ? in : out, more[64] = "";
                size_t cap = to == wait ? sizeof wait : sizeof in, len = strlen(to);
                if (st == 1 && times)            /* in sync now, out of sync before */
                    snprintf(more, sizeof more, " (was out of sync %d %s)", times, times == 1 ? "time" : "times");
                else if (st == 0 && times)
                    snprintf(more, sizeof more, " (%d %s)", times, times == 1 ? "time" : "times");
                else if (st == 0)
                    snprintf(more, sizeof more, " (differs once, checking again)");
                snprintf(to + len, cap - len, "%s%s%s", len ? ", " : "", net_dsys_name(s), more);
            }
            ImGui::PushTextWrapPos(0);
            if (in[0])
                ImGui::TextColored(ON, "In sync: %s", in);
            if (out[0])
                ImGui::TextColored(BAD, "Out of sync: %s", out);
            if (wait[0])
                ImGui::TextColored(OFF, "Not checked yet: %s", wait);
            ImGui::PopTextWrapPos();
        } else if (role == NET_GUEST)
            ImGui::TextColored(OFF, "The host checks that both games agree (its panel says which parts)");
        ImGui::Text("Game: ours %08X, host's %08X", net_world(), net_host_world());
        if (ImGui::Button("Leave"))
            net_stop();
    }
    lobby_section();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Camera and look")) {   /* lookfx.c: kept in lookfx.cfg as each slider is let go */
        LookFx *fx = lookfx();
        int changed = 0;
        ImGui::SliderFloat("Background blur", &fx->dof, 0, 1, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Sharp distance", &fx->dof_range, 0, 1, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Tilt-shift", &fx->tilt, 0, 1, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Sharp band height", &fx->tilt_band, 0.05f, 0.9f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Sharp band place", &fx->tilt_center, 0.2f, 0.8f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Toy colours", &fx->tilt_color, 0, 1, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Zoom out", &fx->zoom, 0, 1, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Tilt camera down", &fx->pitch, 0, 35, "%.0f degrees");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Button("Miniature city")) {        /* a ready-made look: zoomed out, from above, tilt-shift */
            fx->tilt = 0.85f; fx->tilt_band = 0.3f; fx->tilt_center = 0.55f; fx->tilt_color = 0.6f;
            fx->zoom = 0.6f; fx->pitch = 20; fx->dof = 0;
            changed = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Soft background")) {
            fx->dof = 0.8f; fx->dof_range = 0.35f; fx->tilt = 0;
            changed = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("The game's look")) {
            LookFx off = { 0, 0.5f, 0, 0.55f, 0.3f, 0.5f, 0, 0 };
            *fx = off;
            changed = 1;
        }
        if (changed)
            lookfx_save();
    }
    ImGui::Separator();
    ImGui::Text("Log");
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < net_log_count(); i++)
        ImGui::TextUnformatted(net_log_line(i));
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

static void corner(void)                             /* online, the panel closed: one line in the corner */
{
    if (net_role() == NET_OFF)
        return;
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8, io.DisplaySize.y - 8), ImGuiCond_Always, ImVec2(1, 1));   /* the bottom right: the top's corners are the players' HUDs */
    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::Begin("##netcorner", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    status_line();
    if (net_state() == NETS_CONNECTED)
        ImGui::TextColored(OFF, "%s, ping %d ms", net_role() == NET_HOST ? "Host" : "Guest", net_stats().ping_ms);
    ImGui::End();
}

static int g_notice_big, g_have_notice;
static char g_notice_title[128], g_notice_text[1024];

extern "C" void netui_notice(int big, const char *title, const char *text)
{
    g_have_notice = 1;
    g_notice_big = big;
    snprintf(g_notice_title, sizeof g_notice_title, "%s", title ? title : "");
    snprintf(g_notice_text, sizeof g_notice_text, "%s", text ? text : "");
}

static void notice(void)
{
    if (!g_have_notice)
        return;
    g_have_notice = 0;
    ImGuiIO &io = ImGui::GetIO();
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                                ImGuiWindowFlags_NoSavedSettings;
    if (g_notice_big) {
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.88f);
    } else {
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 12), ImGuiCond_Always, ImVec2(0.5f, 0));
        ImGui::SetNextWindowBgAlpha(0.7f);
    }
    float wrap = io.DisplaySize.x * (g_notice_big ? 0.55f : 0.7f);
    ImGui::Begin("##netnotice", NULL, fl);
    if (g_notice_title[0]) {
        ImGui::SetWindowFontScale(g_notice_big ? 1.5f : 1.1f);
        ImGui::TextColored(ON, "%s", g_notice_title);
        ImGui::SetWindowFontScale(1.0f);
    }
    if (g_notice_text[0]) {
        if (g_notice_big)
            ImGui::Spacing();
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(g_notice_text);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

extern "C" void netui_frame(void)
{
    if (!g_ready)
        return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    labels();
    toast();
    netui_chat_draw();
    notice();
    if (g_open)
        panel();
    else
        corner();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

#else
extern "C" int netui_init(void *hwnd) { (void)hwnd; return -1; }
extern "C" void netui_shutdown(void) {}
extern "C" int netui_wndproc(void *hwnd, unsigned msg, uintptr_t wp, intptr_t lp) { (void)hwnd; (void)msg; (void)wp; (void)lp; return 0; }
extern "C" void netui_frame(void) {}
extern "C" int netui_wants_input(void) { return 0; }
extern "C" int netui_wants_mouse(void) { return 0; }
extern "C" void netui_toggle(void) {}
extern "C" void netui_notice(int big, const char *title, const char *text) { (void)big; (void)title; (void)text; }
extern "C" void netui_label(float x, float y, const char *text) { (void)x; (void)y; (void)text; }
extern "C" void netui_label_big(float x, float y, const char *text, const char *sub, uint32_t rgb)
{
    (void)x; (void)y; (void)text; (void)sub; (void)rgb;
}
extern "C" void netui_toast(const char *text) { (void)text; }
#endif

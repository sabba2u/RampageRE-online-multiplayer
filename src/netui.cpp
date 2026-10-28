/* netui.cpp - online play's panel (netui.h), the only C++ of the port: Dear ImGui over the game's frame */
#include "netui.h"
#include "net.h"
#include "netwire.h"
#include "nettransport.h"
#include "lookfx.h"
#ifdef _WIN32
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_win32.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static int g_ready, g_open;
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
extern "C" int netui_wants_input(void) { return g_ready && (g_open || ImGui::GetIO().WantCaptureKeyboard); }
extern "C" int netui_wants_mouse(void) { return g_ready && ImGui::GetIO().WantCaptureMouse; }

extern "C" int netui_wndproc(void *hwnd, unsigned msg, uintptr_t wp, intptr_t lp)
{
    if (!g_ready)
        return 0;
    if (msg == WM_KEYDOWN && wp == VK_INSERT && !(lp & (1 << 30))) {   /* (not the key's repeats) */
        netui_toggle();
        return 1;
    }
    ImGui_ImplWin32_WndProcHandler((HWND)hwnd, msg, (WPARAM)wp, (LPARAM)lp);
    return 0;
}

static void status_line(void)
{
    int st = net_state();
    ImVec4 c = st == NETS_FAILED ? BAD : net_live() ? ON : OFF;
    ImGui::TextColored(c, "%s", net_state_text());
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
        ImGui::TextColored(OFF, "Your friend joins you. Port %d (UDP).", NET_PORT);
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##addr", g_addr, sizeof g_addr);
        ImGui::SameLine();
        if (ImGui::Button("Join"))
            net_start_join(g_addr);
        ImGui::SameLine();
        ImGui::TextColored(OFF, "the host's address");
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
        if (role == NET_HOST && net_is_steam()) {
            if (nts_lobby() && ImGui::Button("Invite a friend (Steam)"))
                nts_invite();
            else if (!nts_lobby())
                ImGui::TextColored(OFF, "Making the Steam lobby...");
        }
        NetStats s = net_stats();
        ImGui::Text("Ping: %d ms", s.ping_ms);
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
        }
        ImGui::Text("Game: ours %08X, host's %08X", net_world(), net_host_world());
        if (ImGui::Button("Leave"))
            net_stop();
    }
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
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.65f);
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
#endif

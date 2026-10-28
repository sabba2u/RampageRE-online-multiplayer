/* netchat.cpp - online play's chat (netui.h), as Emberville's (EmbervilleGhost/Chat.cs): Enter opens the box in play,
 * Enter sends (an empty line just closes), Esc cancels; the last lines bottom left above the game's HUD, fading out
 * after 12 s (all of them shown while typing); the name in its player's colour (the host gold, the guest light blue),
 * system lines (joins, leaves, defeats) in their own. The box reads the window's characters itself (WM_CHAR), not an
 * ImGui text field: it can never lose the focus and leave the game's keys off with no box to type in. */
#include "netui.h"
#ifdef _WIN32
#include "imgui.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHAT_KEEP 50
#define CHAT_SHOW 8
#define CHAT_SHOW_SECONDS 12.0
#define CHAT_FADE_SECONDS 2.0
#define CHAT_WIDTH 440.0f
#define CHAT_MAX 200

typedef struct { double t; int who; char name[32], text[CHAT_MAX + 1]; } ChatLine;

static ChatLine g_lines[CHAT_KEEP];
static int g_nlines;
static int g_allowed, g_open;                        /* Enter may open it (online, in play); the box is open */
static double g_opened_at;
static char g_typing[CHAT_MAX + 1], g_sent[CHAT_MAX + 1];
static int g_have_sent;
static struct { char text[64]; double t; } g_said[2];   /* each player's last line, for the bubble over the head */

static double now_s(void) { return GetTickCount64() / 1000.0; }

/* a line of text with a dark shadow under it (readable over the game's bright buildings), wrapped at the window's edge
 * when wrap; then the cursor moves on as ImGui's own text would */
static void shadowed(ImVec4 c, const char *t, int wrap)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float ww = wrap ? ImGui::GetContentRegionAvail().x : 0;
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 1.5f, p.y + 1.5f),
                                        IM_COL32(0, 0, 0, (int)(230 * c.w)), t, NULL, ww);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    if (wrap)
        ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(t);
    if (wrap)
        ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

static ImVec4 color_of(int who)                      /* -1 system, -2 an error or a defeat, 0 the host, 1 the guest */
{
    switch (who) {
    case 0: return ImVec4(1, 0.8f, 0.3f, 1);
    case 1: return ImVec4(0.45f, 0.8f, 1, 1);
    case -2: return ImVec4(1, 0.4f, 0.4f, 1);
    default: return ImVec4(0.7f, 0.7f, 0.72f, 1);
    }
}

static void clean(char *dst, const char *src, int cap)   /* trimmed, no control characters, at most cap */
{
    int n = 0;
    while (*src == ' ')
        src++;
    for (; *src && n < cap; src++)
        if ((unsigned char)*src >= 32 && *src != 127)
            dst[n++] = *src;
    while (n > 0 && dst[n - 1] == ' ')
        n--;
    dst[n] = 0;
}

extern "C" void netui_chat_add(int who, const char *name, const char *text)
{
    if (g_nlines == CHAT_KEEP) {
        memmove(g_lines, g_lines + 1, sizeof g_lines[0] * (CHAT_KEEP - 1));
        g_nlines--;
    }
    ChatLine *l = &g_lines[g_nlines++];
    l->t = now_s();
    l->who = who;
    snprintf(l->name, sizeof l->name, "%s", name ? name : "");
    clean(l->text, text ? text : "", CHAT_MAX);
    if (who == 0 || who == 1) {
        snprintf(g_said[who].text, sizeof g_said[who].text, "%s", l->text);
        g_said[who].t = l->t;
    }
}

extern "C" const char *netui_chat_said(int who, float *age)
{
    if (who < 0 || who > 1 || !g_said[who].text[0])
        return NULL;
    *age = (float)(now_s() - g_said[who].t);
    return g_said[who].text;
}

extern "C" void netui_chat_allow(int on)
{
    g_allowed = on;
    if (!on)
        g_open = 0;
}

extern "C" int netui_chat_open(void) { return g_open; }

extern "C" int netui_chat_take(char *out, int cap)
{
    if (!g_have_sent)
        return 0;
    g_have_sent = 0;
    snprintf(out, (size_t)cap, "%s", g_sent);
    return 1;
}

/* the window's messages while the chat may open: 1 eaten */
extern "C" int netui_chat_wndproc(unsigned msg, uintptr_t wp, intptr_t lp)
{
    if (!g_allowed)
        return 0;
    if (!g_open) {
        if (msg == WM_KEYDOWN && wp == VK_RETURN && !(lp & (1 << 30))) {   /* (Enter in play: not the Pause menu) */
            g_open = 1;
            g_typing[0] = 0;
            g_opened_at = now_s();
            return 1;
        }
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            if (now_s() - g_opened_at > 0.1) {       /* not the Enter that opened it */
                clean(g_sent, g_typing, CHAT_MAX);
                g_have_sent = g_sent[0] != 0;
                g_open = 0;
            }
        } else if (wp == VK_ESCAPE)
            g_open = 0;
        else if (wp == VK_BACK) {
            size_t n = strlen(g_typing);
            if (n)
                g_typing[n - 1] = 0;
        } else if (wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && OpenClipboard(NULL)) {   /* Ctrl+V: paste */
            HANDLE h = GetClipboardData(CF_TEXT);
            const char *t = h ? (const char *)GlobalLock(h) : NULL;
            if (t) {
                size_t n = strlen(g_typing);
                for (; *t && n < CHAT_MAX; t++)
                    if ((unsigned char)*t >= 32 && *t != 127)
                        g_typing[n++] = *t;
                g_typing[n] = 0;
                GlobalUnlock(h);
            }
            CloseClipboard();
        }
        return 1;                                    /* (every key the box's while it is open) */
    }
    if (msg == WM_CHAR) {
        size_t n = strlen(g_typing);
        if (wp >= 32 && wp != 127 && wp < 256 && n < CHAT_MAX && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            g_typing[n] = (char)wp;
            g_typing[n + 1] = 0;
        }
        return 1;
    }
    if (msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSCHAR)
        return 1;
    return 0;
}

/* the box and the lines, bottom left, lifted a quarter of the window above the game's HUD */
extern "C" void netui_chat_draw(void)
{
    double now = now_s();
    int any = g_open;
    for (int i = g_nlines - CHAT_SHOW < 0 ? 0 : g_nlines - CHAT_SHOW; i < g_nlines && !any; i++)
        any = now - g_lines[i].t < CHAT_SHOW_SECONDS;
    if (!any)
        return;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12, vp->WorkPos.y + vp->WorkSize.y * 0.75f), ImGuiCond_Always,
                            ImVec2(0, 1));
    ImGui::SetNextWindowSizeConstraints(ImVec2(CHAT_WIDTH, 0), ImVec2(CHAT_WIDTH, vp->WorkSize.y * 0.5f));
    ImGui::SetNextWindowBgAlpha(g_open ? 0.7f : 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, g_open ? 1.0f : 0.0f);
    bool shown = ImGui::Begin("##chat", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::PopStyleVar();
    ImFont *bold = (ImFont *)netui_bold_font();
    if (bold)
        ImGui::PushFont(bold);
    if (shown) {
        for (int i = g_nlines - CHAT_SHOW < 0 ? 0 : g_nlines - CHAT_SHOW; i < g_nlines; i++) {
            const ChatLine *l = &g_lines[i];
            double a = g_open ? 1 : (CHAT_SHOW_SECONDS - (now - l->t)) / CHAT_FADE_SECONDS;
            float alpha = (float)(a > 1 ? 1 : a);
            if (alpha <= 0)
                continue;
            ImVec4 c = color_of(l->who);
            c.w = alpha;
            if (l->name[0]) {
                char who[40];
                snprintf(who, sizeof who, "%s:", l->name);
                shadowed(c, who, 0);
                ImGui::SameLine();
                c = ImVec4(1, 1, 1, alpha);
            }
            shadowed(c, l->text, 1);                 /* (never a format: a player's % is text) */
        }
        if (g_open) {
            char row[CHAT_MAX + 8];
            snprintf(row, sizeof row, "> %s%s", g_typing, fmod(now * 2, 2) < 1 ? "_" : " ");
            ImGui::Separator();
            shadowed(ImVec4(1, 1, 1, 1), row, 1);
            ImGui::TextDisabled("Enter: send   Esc: cancel");
        }
    }
    if (bold)
        ImGui::PopFont();
    ImGui::End();
}

#else
extern "C" void netui_chat_add(int who, const char *name, const char *text) { (void)who; (void)name; (void)text; }
extern "C" const char *netui_chat_said(int who, float *age) { (void)who; (void)age; return NULL; }
extern "C" void netui_chat_allow(int on) { (void)on; }
extern "C" int netui_chat_open(void) { return 0; }
extern "C" int netui_chat_take(char *out, int cap) { (void)out; (void)cap; return 0; }
extern "C" int netui_chat_wndproc(unsigned msg, uintptr_t wp, intptr_t lp) { (void)msg; (void)wp; (void)lp; return 0; }
extern "C" void netui_chat_draw(void) {}
#endif

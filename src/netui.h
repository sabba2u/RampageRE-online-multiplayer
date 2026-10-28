/* netui.h - online play's panel (Dear ImGui, src/net/third_party/imgui), drawn in the game's own window over its
 * frame: Insert opens and closes it. Host / join, the connection, ping and the log; a small line in the corner while
 * online with the panel closed. Windows (Win32 + OpenGL 3 back ends); elsewhere these do nothing. */
#ifndef RAMPAGE_NETUI_H
#define RAMPAGE_NETUI_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int netui_init(void *hwnd);                          /* 0 ok */
void netui_shutdown(void);
/* the window's messages, before the game sees them: 1 eaten (the Insert that toggles the panel) */
int netui_wndproc(void *hwnd, unsigned msg, uintptr_t wp, intptr_t lp);
void netui_frame(void);                              /* build and draw the panel, just before the swap */
int netui_wants_input(void);                         /* the panel is open: the game gets no keys */
int netui_wants_mouse(void);                         /* the mouse is over one of its windows */
void netui_toggle(void);
int netgame_rate(void);                             /* netgame.c: the host's updates a second (20, 30, 60) */
void netgame_set_rate(int r);
void netgame_resync(void);                         /* netgame.c: the host re-sends its whole world; a guest asks */
const char *netgame_slot_name(int k);               /* netgame.c: slot k's monster ("" none) */
int netgame_predict(void);                          /* netgame.c: the guest moves its own monster at once */
void netgame_set_predict(int on);
/* a label over the picture at screen point x, y (pixels from the top left), this frame only */
void netui_label(float x, float y, const char *text);
/* a big one (the player slots' DISCONNECTED / WAITING FOR PLAYER, for the stream to read): bold, scaled, in colour
 * rgb (0xRRGGBB), a smaller second line under it ("Rejoin 0:47", NULL none) */
void netui_label_big(float x, float y, const char *text, const char *sub, uint32_t rgb);
/* a toast in the middle of the screen for a few seconds ("Local controller took over Player 2") */
void netui_toast(const char *text);
/* the chat (netchat.cpp): who 0 the host, 1 the guest, -1 a system line, -2 an error or a defeat (red) */
void netui_chat_add(int who, const char *name, const char *text);
const char *netui_chat_said(int who, float *age);   /* that player's last line and its age (the bubble), NULL */
void netui_chat_allow(int on);                      /* Enter may open it (online, in play), each frame */
int netui_chat_open(void);
int netui_chat_take(char *out, int cap);            /* 1: a line was sent (Enter), into out */
int netui_chat_wndproc(unsigned msg, uintptr_t wp, intptr_t lp);
void netui_chat_draw(void);
int netui_panel_open(void);
void *netui_bold_font(void);                         /* (an ImFont *: the chat's, NULL none) */                          /* the Insert panel is up */
/* this frame's notice over the game (netrep_menu.c: what the other player is doing in the menus): big, the screen
 * dimmed and the box in its middle; else a line at the top. Drawn by the next netui_frame, then forgotten */
void netui_notice(int big, const char *title, const char *text);

#ifdef __cplusplus
}
#endif
#endif

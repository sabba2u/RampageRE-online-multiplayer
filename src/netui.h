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

#ifdef __cplusplus
}
#endif
#endif

#ifndef ATOMS_H
#define ATOMS_H

#include <X11/Xlib.h>

/* =========================================================================
 * Atoms — declarados como extern; definidos em atoms.c
 * ========================================================================= */
extern Atom WM_PROTOCOLS, WM_DELETE_WINDOW, WM_STATE;
extern Atom _NET_SUPPORTED, _NET_WM_STATE;
extern Atom _NET_WM_STATE_MAXIMIZED_VERT, _NET_WM_STATE_MAXIMIZED_HORZ;
extern Atom _NET_WM_STATE_FULLSCREEN;
extern Atom _NET_WM_STATE_HIDDEN;
extern Atom _NET_ACTIVE_WINDOW, _NET_WM_NAME, _NET_SUPPORTING_WM_CHECK;
extern Atom UTF8_STRING, _NET_WM_WINDOW_TYPE;
extern Atom _NET_WM_WINDOW_TYPE_DOCK, _NET_WM_WINDOW_TYPE_DESKTOP;
extern Atom _NET_WM_WINDOW_TYPE_SPLASH, _NET_WM_WINDOW_TYPE_TOOLTIP;
extern Atom _NET_WM_WINDOW_TYPE_NOTIFICATION, _NET_WM_WINDOW_TYPE_MENU;
extern Atom _NET_WM_WINDOW_TYPE_POPUP_MENU;
extern Atom _NET_WM_WINDOW_TYPE_TOOLBAR, _NET_WM_WINDOW_TYPE_UTILITY;
extern Atom _NET_WM_STRUT, _NET_CLIENT_LIST;
extern Atom _NET_WM_MOVERESIZE;

/* =========================================================================
 * Funções
 * ========================================================================= */
void init_atoms(void);
void ewmh_init(void);
void update_client_list(void);
void set_wm_state(Window w, long st);
void set_net_maximized(Window w, int on);
void set_net_fullscreen(Window w, int on);
void set_net_hidden(Window w, int on);
void set_net_active(Window w);
void send_delete(Window w);
void send_configure(struct Win *iw);

#endif /* ATOMS_H */

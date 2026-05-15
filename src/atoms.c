#include "types.h"
#include "atoms.h"

#include <X11/Xatom.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================================
 * Atom definitions
 * ========================================================================= */
Atom WM_PROTOCOLS, WM_DELETE_WINDOW, WM_STATE;
Atom _NET_SUPPORTED, _NET_WM_STATE;
Atom _NET_WM_STATE_MAXIMIZED_VERT, _NET_WM_STATE_MAXIMIZED_HORZ;
Atom _NET_WM_STATE_FULLSCREEN;
Atom _NET_WM_STATE_HIDDEN;
Atom _NET_ACTIVE_WINDOW, _NET_WM_NAME, _NET_SUPPORTING_WM_CHECK;
Atom UTF8_STRING, _NET_WM_WINDOW_TYPE;
Atom _NET_WM_WINDOW_TYPE_DOCK, _NET_WM_WINDOW_TYPE_DESKTOP;
Atom _NET_WM_WINDOW_TYPE_SPLASH, _NET_WM_WINDOW_TYPE_TOOLTIP;
Atom _NET_WM_WINDOW_TYPE_NOTIFICATION, _NET_WM_WINDOW_TYPE_MENU;
Atom _NET_WM_WINDOW_TYPE_POPUP_MENU;
Atom _NET_WM_WINDOW_TYPE_TOOLBAR, _NET_WM_WINDOW_TYPE_UTILITY;
Atom _NET_WM_STRUT, _NET_CLIENT_LIST;
Atom _NET_WM_MOVERESIZE;

/* =========================================================================
 * init_atoms
 * ========================================================================= */
void init_atoms(void) {
#define A(n) n = XInternAtom(dpy, #n, False)
    A(WM_PROTOCOLS); A(WM_DELETE_WINDOW); A(WM_STATE);
    A(_NET_SUPPORTED); A(_NET_WM_STATE);
    A(_NET_WM_STATE_MAXIMIZED_VERT); A(_NET_WM_STATE_MAXIMIZED_HORZ);
    A(_NET_WM_STATE_FULLSCREEN);
    A(_NET_WM_STATE_HIDDEN);
    A(_NET_ACTIVE_WINDOW); A(_NET_WM_NAME); A(_NET_SUPPORTING_WM_CHECK);
    A(UTF8_STRING); A(_NET_WM_WINDOW_TYPE);
    A(_NET_WM_WINDOW_TYPE_DOCK); A(_NET_WM_WINDOW_TYPE_DESKTOP);
    A(_NET_WM_WINDOW_TYPE_SPLASH); A(_NET_WM_WINDOW_TYPE_TOOLTIP);
    A(_NET_WM_WINDOW_TYPE_NOTIFICATION); A(_NET_WM_WINDOW_TYPE_MENU);
    A(_NET_WM_WINDOW_TYPE_POPUP_MENU);
    A(_NET_WM_WINDOW_TYPE_TOOLBAR); A(_NET_WM_WINDOW_TYPE_UTILITY);
    A(_NET_WM_STRUT); A(_NET_CLIENT_LIST);
    A(_NET_WM_MOVERESIZE);
#undef A
}

/* =========================================================================
 * ewmh_init
 * ========================================================================= */
void ewmh_init(void) {
    Atom sup[] = { _NET_SUPPORTED, _NET_WM_STATE, _NET_WM_STATE_MAXIMIZED_VERT,
                   _NET_WM_STATE_MAXIMIZED_HORZ, _NET_WM_STATE_FULLSCREEN,
                   _NET_WM_STATE_HIDDEN,
                   _NET_ACTIVE_WINDOW, _NET_WM_NAME,
                   _NET_SUPPORTING_WM_CHECK, _NET_CLIENT_LIST };
    XChangeProperty(dpy, root, _NET_SUPPORTED, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)sup, (int)(sizeof(sup)/sizeof(sup[0])));
    /* Cria a check window do EWMH com override_redirect=True para que
     * skip_window() a ignore e ela não seja gerenciada como janela comum. */
    XSetWindowAttributes chk_attr = {0};
    chk_attr.override_redirect = True;
    Window chk = XCreateWindow(dpy, root, -1, -1, 1, 1, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWOverrideRedirect, &chk_attr);
    XChangeProperty(dpy, chk, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&chk, 1);
    XChangeProperty(dpy, root, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&chk, 1);
    const char *nm = "InfernoWM";
    XChangeProperty(dpy, chk, _NET_WM_NAME, UTF8_STRING, 8, PropModeReplace,
                    (unsigned char *)nm, (int)strlen(nm));
}

/* =========================================================================
 * update_client_list
 * ========================================================================= */
void update_client_list(void) {
    int n = 0;
    for (Win *c = wins; c; c = c->next) if (!c->closing) n++;
    Window *list = n ? malloc(n * sizeof(Window)) : NULL;
    int i = 0;
    for (Win *c = wins; c; c = c->next) if (!c->closing && list) list[i++] = c->client;
    XChangeProperty(dpy, root, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)list, n);
    free(list);
}

/* =========================================================================
 * ICCCM helpers
 * ========================================================================= */
void set_wm_state(Window w, long st) {
    long d[2] = { st, None };
    XChangeProperty(dpy, w, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char *)d, 2);
}

void set_net_maximized(Window w, int on) {
    if (on) {
        Atom a[2] = { _NET_WM_STATE_MAXIMIZED_VERT, _NET_WM_STATE_MAXIMIZED_HORZ };
        XChangeProperty(dpy, w, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)a, 2);
    } else {
        XDeleteProperty(dpy, w, _NET_WM_STATE);
    }
}

void set_net_fullscreen(Window w, int on) {
    if (on) {
        XChangeProperty(dpy, w, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&_NET_WM_STATE_FULLSCREEN, 1);
    } else {
        XDeleteProperty(dpy, w, _NET_WM_STATE);
    }
}

void set_net_hidden(Window w, int on) {
    Atom t; int fmt; unsigned long n, ba; unsigned char *data = NULL;
    Atom atoms[16];
    int natoms = 0;

    /* Lê o _NET_WM_STATE atual */
    if (XGetWindowProperty(dpy, w, _NET_WM_STATE, 0, 16, False,
            XA_ATOM, &t, &fmt, &n, &ba, &data) == Success && data) {
        Atom *existing = (Atom *)data;
        for (unsigned long i = 0; i < n && natoms < 16; i++) {
            /* Se vamos adicionar HIDDEN, não copia HIDDEN existente
             * Se vamos remover HIDDEN, copia tudo exceto HIDDEN */
            if (existing[i] != _NET_WM_STATE_HIDDEN)
                atoms[natoms++] = existing[i];
        }
        XFree(data);
    }

    if (on) {
        /* Adiciona HIDDEN se ainda não estiver na lista */
        int found = 0;
        for (int i = 0; i < natoms; i++)
            if (atoms[i] == _NET_WM_STATE_HIDDEN) { found = 1; break; }
        if (!found && natoms < 16)
            atoms[natoms++] = _NET_WM_STATE_HIDDEN;
    }

    if (natoms > 0) {
        XChangeProperty(dpy, w, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)atoms, natoms);
    } else {
        XDeleteProperty(dpy, w, _NET_WM_STATE);
    }
}

void set_net_active(Window w) {
    XChangeProperty(dpy, root, _NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&w, 1);
}

void send_delete(Window w) {
    Atom *p = NULL; int n = 0, ok = 0;
    if (XGetWMProtocols(dpy, w, &p, &n)) {
        for (int i = 0; i < n; i++) if (p[i] == WM_DELETE_WINDOW) ok = 1;
        XFree(p);
    }
    if (ok) {
        XEvent e = {0};
        e.type = ClientMessage;
        e.xclient.window       = w;
        e.xclient.message_type = WM_PROTOCOLS;
        e.xclient.format       = 32;
        e.xclient.data.l[0]   = (long)WM_DELETE_WINDOW;
        e.xclient.data.l[1]   = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, &e);
    } else {
        XKillClient(dpy, w);
    }
}

void send_configure(Win *iw) {
    XConfigureEvent ce = {0};
    ce.type              = ConfigureNotify;
    ce.display           = dpy;
    ce.event             = iw->client;
    ce.window            = iw->client;
    if (iw->decorated) {
        /* Envia coordenadas relativas ao ROOT, não ao frame.
         * O VLC/Qt usa estas coordenadas para calcular onde
         * posicionar popups menus. Se enviarmos x=1 (relativo
         * ao frame), o VLC nunca sabe que foi movido na tela. */
        ce.x            = iw->x + 1;
        ce.y            = iw->y + TITLE_HEIGHT;
        ce.width        = iw->w - 2;
        ce.height       = iw->h - TITLE_HEIGHT - 2;
    } else {
        ce.x            = iw->x;
        ce.y            = iw->y;
        ce.width        = iw->w;
        ce.height       = iw->h;
    }
    ce.border_width      = 0;
    ce.above             = None;
    ce.override_redirect = False;
    XSendEvent(dpy, iw->client, False, StructureNotifyMask, (XEvent *)&ce);
}

#include "types.h"
#include "atoms.h"
#include "draw.h"
#include "window.h"
#include "menu.h"
#include "events.h"
#include "main.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

/* =========================================================================
 * Globals — definições
 * ========================================================================= */
Display *dpy     = NULL;
int      scr;
Window   root;
Window   taskbar = None;
Win     *wins    = NULL;
Win     *focused = NULL;
int      SW, SH;

Cursor cur_normal, cur_move, cur_br;

int x_err = 0, wm_running = 0;
char **saved_argv = NULL;
const char *font_name = NULL;

/* =========================================================================
 * X11 error handlers
 * ========================================================================= */
int on_xerror(Display *d, XErrorEvent *e) {
    (void)d;
    if (e->error_code == BadWindow  || e->error_code == BadDrawable ||
        e->error_code == BadMatch   || e->error_code == BadAccess   ||
        e->error_code == BadValue) { x_err = 1; return 0; }
    char msg[256];
    XGetErrorText(d, e->error_code, msg, sizeof(msg));
    fprintf(stderr, "[inferno_wm] X: %s (req=%d)\n", msg, e->request_code);
    return 0;
}

int on_xerror_detect(Display *d, XErrorEvent *e) {
    (void)d;
    if (e->error_code == BadAccess) wm_running = 1;
    return 0;
}

void on_sigchld(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/* =========================================================================
 * init_font — detecta se Inter está disponível, fallback para monospace
 * ========================================================================= */
static void init_font(void) {
    FcBool res = FcInit();
    if (res == FcFalse) { font_name = "monospace"; return; }

    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)"Go");
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    if (match) {
        FcChar8 *family = NULL;
        if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch
            && family != NULL) {
            font_name = "Go";
        } else {
            font_name = "monospace";
        }
        FcPatternDestroy(match);
    } else {
        font_name = "monospace";
    }
    FcPatternDestroy(pat);
    FcFini();
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(int argc, char *argv[]) {
    (void)argc;
    struct sigaction sa={0};
    sa.sa_handler=on_sigchld; sa.sa_flags=SA_RESTART|SA_NOCLDSTOP;
    sigaction(SIGCHLD,&sa,NULL);

    init_font();

    dpy=XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr,"[inferno_wm] Não foi possível abrir display.\n"); return 1; }
    scr=DefaultScreen(dpy); root=RootWindow(dpy,scr);
    SW=DisplayWidth(dpy,scr); SH=DisplayHeight(dpy,scr);

    XSetErrorHandler(on_xerror_detect);
    XSelectInput(dpy,root,
        SubstructureRedirectMask|SubstructureNotifyMask|
        StructureNotifyMask|  /* necessário para detectar mudança de resolução via ConfigureNotify no root */
        ButtonPressMask|KeyPressMask|PropertyChangeMask);
    XSync(dpy,False);
    XSetErrorHandler(on_xerror);
    if (wm_running) { fprintf(stderr,"[inferno_wm] Outro WM está rodando.\n"); XCloseDisplay(dpy); return 1; }

    XSetWindowBackground(dpy,root,C_BG_HEX);
    XClearWindow(dpy,root);
    init_atoms(); ewmh_init();
    init_logo();

    cur_normal=XCreateFontCursor(dpy,XC_left_ptr);
    cur_move  =XCreateFontCursor(dpy,XC_fleur);
    cur_br    =XCreateFontCursor(dpy,XC_bottom_right_corner);
    XDefineCursor(dpy,root,cur_normal);

    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_F4), Mod1Mask,root,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_F2), Mod1Mask,root,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_Tab),Mod1Mask,root,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_F9), Mod1Mask,root,True,GrabModeAsync,GrabModeAsync);
    XGrabButton(dpy,Button1,AnyModifier,root,True,ButtonPressMask,GrabModeSync,GrabModeAsync,None,None);
    /* Button3 (right-click) removido — menu agora é acionado pelo botão X da taskbar */

    XSetWindowAttributes ta={0};
    ta.background_pixmap=None; ta.override_redirect=True;
    ta.event_mask=ExposureMask|ButtonPressMask|ButtonReleaseMask;
    ta.background_pixel=C_BG_HEX;
    int taskbar_y = TASKBAR_GAP_ENABLED ? (SH - TASKBAR_H + 1) : (SH - TASKBAR_H);
    taskbar=XCreateWindow(dpy,root,0,taskbar_y,SW,TASKBAR_H,0,
                           CopyFromParent,InputOutput,CopyFromParent,
                           CWBackPixmap|CWOverrideRedirect|CWEventMask|CWBackPixel,&ta);
    unsigned long strut[4]={0,0,0,(unsigned long)TASKBAR_H};
    XChangeProperty(dpy,taskbar,_NET_WM_STRUT,XA_CARDINAL,32,PropModeReplace,(unsigned char *)strut,4);
    XMapWindow(dpy,taskbar);
    draw_taskbar();
    create_taskbar_pixmap();

    Window wr,wp,*ch=NULL; unsigned int nch=0;
    if (XQueryTree(dpy,root,&wr,&wp,&ch,&nch)) {
        for (unsigned int i=0;i<nch;i++) { if(ch[i]==taskbar)continue; manage(ch[i],1); }
        if (ch) XFree(ch);
    }

    draw_taskbar();
    flush_taskbar_pixmap();

    saved_argv = argv;

    XSync(dpy,False);
    load_menu_config();
    wm_running = 1;
    run();
    destroy_taskbar_pixmap();
    cleanup_wins();
    free_menu_config();
    free_logo();
    XCloseDisplay(dpy);
    return 0;
}

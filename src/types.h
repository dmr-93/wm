#ifndef TYPES_H
#define TYPES_H

#include <X11/Xlib.h>
#include "config.h"

/* Macros para posição do client dentro do frame
 * NOTA: Todas as janelas gerenciadas têm decoração forçada, então os valores
 * são constantes. O fullscreen seta decorated=0 mas gerencia posição manualmente. */
#define CLIENT_X(iw)    1
#define CLIENT_Y(iw)    TITLE_HEIGHT
#define CLIENT_W(iw)    ((iw)->w - 2)
#define CLIENT_H(iw)    ((iw)->h - TITLE_HEIGHT - 2)

/* Macros para posição dos botões na titlebar */
#define BTN_CLOSE_X(iw) ((iw)->w - BTN_MARGIN - BTN_W)
#define BTN_MIN_X(iw)   (BTN_CLOSE_X(iw) - BTN_W)
#define BTN_RSZ_X(iw)   (BTN_MIN_X(iw) - BTN_W)
#define BTN_Y           BTN_MARGIN

/* =========================================================================
 * Hit-test constants
 * ========================================================================= */
#define HIT_NONE   0
#define HIT_TITLE  1
#define HIT_CLOSE  2
#define HIT_MIN    3
#define HIT_RESIZE 4

/* =========================================================================
 * Estrutura de janela
 * ========================================================================= */
typedef struct Win {
    Window      frame;
    Window      client;
    char        title[512];
    int         x, y, w, h;
    int         sx, sy, sw, sh;   /* salvo antes de maximizar */
    int         maximized;
    int         fullscreen;       /* 1 = modo tela cheia (sem decoração, cobre tudo) */
    int         minimized;
    int         closing;
    int         decorated;        /* 1 = tem titlebar/bordas/botoes; 0 = sem decoracao (fullscreen) */
    struct Win *next;
} Win;

/* =========================================================================
 * Item de menu (dinâmico, carregado de ~/.inferno_wm.conf)
 * ========================================================================= */
typedef struct {
    char *label;        /* string a ser exibida (allocada) */
    char *cmd;          /* comando a executar (allocada) */
    int   is_separator; /* 1 = linha separadora */
} MenuItem;

/* =========================================================================
 * Globais — declarados como extern; definidos em wm_main.c
 * ========================================================================= */
extern Display *dpy;
extern int      scr;
extern Window   root;
extern Window   taskbar;
extern Win     *wins;
extern Win     *focused;
extern int      SW, SH;

extern Cursor cur_normal, cur_move, cur_br;

extern int x_err, wm_running;

extern int px, py;

extern MenuItem *menu_items;
extern int       menu_count;
extern char    **saved_argv;
extern const char *font_name;

#endif /* TYPES_H */

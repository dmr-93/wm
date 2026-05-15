#ifndef WINDOW_H
#define WINDOW_H

#include <X11/Xlib.h>

struct Win; /* forward declaration */

/* =========================================================================
 * Lookup
 * ========================================================================= */
struct Win *by_frame(Window w);
struct Win *by_client(Window w);

/* =========================================================================
 * Título
 * ========================================================================= */
void get_title(Window w, char *dst);

/* =========================================================================
 * Skip
 * ========================================================================= */
int skip_window(Window w);

/* =========================================================================
 * Gerenciamento
 * ========================================================================= */
void manage(Window client, int pre);
void unmanage(Window client);

/* =========================================================================
 * Foco
 * ========================================================================= */
void focus_win(struct Win *w);

/* =========================================================================
 * Resolução de tela
 * ========================================================================= */
void configure_screen(int new_SW, int new_SH);

/* =========================================================================
 * Maximizar / Minimizar
 * ========================================================================= */
void toggle_max(struct Win *iw);
void toggle_fullscreen(struct Win *iw);
void minimize(struct Win *iw);
void restore(struct Win *iw);

/* =========================================================================
 * Resize interativo
 * ========================================================================= */
void do_resize(struct Win *iw);

/* =========================================================================
 * Overlay window — usado pelo resize "armar + arrastar cantos"
 * ========================================================================= */
Window create_resize_overlay(struct Win *iw);
void   destroy_resize_overlay(void);
void   move_resize_overlay(int x, int y, int w, int h);

/* =========================================================================
 * Red overlay (XOR) — usado pelo resize interativo
 * ========================================================================= */
void red_ensure(void);
void red_paint(int x, int y, int w, int h);
void red_hide(void);

/* =========================================================================
 * Hit-test
 * ========================================================================= */
int hit_titlebar(struct Win *iw, int lx, int ly);

/* =========================================================================
 * position_client — reposiciona o client dentro do frame
 * ========================================================================= */
void position_client(struct Win *iw);

/* =========================================================================
 * focus_next_available — foca a próxima janela disponível
 * ========================================================================= */
void focus_next_available(void);

/* =========================================================================
 * Cleanup — reparentar clientes para root antes de sair/reiniciar
 * ========================================================================= */
void cleanup_wins(void);

#endif /* WINDOW_H */

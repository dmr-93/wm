#ifndef DRAW_H
#define DRAW_H

#include <cairo/cairo.h>

struct Win; /* forward declaration */

/* =========================================================================
 * Constantes de fonte padronizadas
 * ========================================================================= */
#define FONT_SIZE       13
#define FONT_PAD_LEFT   10

/* Macro para converter hex → float RGB na chamada do Cairo */
#define SET_SOURCE_HEX(cr, hex) \
    cairo_set_source_rgb(cr, ((hex>>16)&0xFF)/255.0, \
                              ((hex>>8)&0xFF)/255.0,  \
                              ((hex)&0xFF)/255.0)

extern cairo_surface_t *logo_surf;

/* Backing pixmap para a taskbar — registrado como background via
 * XSetWindowBackgroundPixmap. draw_taskbar() desenha no pixmap
 * e flush_taskbar_pixmap() re-registra + XClearWindow para que o
 * X11 auto-preencha a window a partir do pixmap em qualquer Expose. */
extern Pixmap taskbar_pixmap;

void init_logo(void);
void free_logo(void);
void create_taskbar_pixmap(void);
void destroy_taskbar_pixmap(void);
void flush_taskbar_pixmap(void);
void draw_bevel(cairo_t *cr, double x, double y, double w, double h, int raised);
void draw_btn(cairo_t *cr, int x, int y, const char *type);
void draw_frame(struct Win *iw);
void draw_taskbar(void);
void draw_text_centered(cairo_t *cr, double x, double y, double h,
                        const char *text);
void fill_rect_hex(cairo_t *cr, unsigned int hex,
                   double x, double y, double w, double h);
void setup_font(cairo_t *cr, int bold);

/* Cria um context cairo dummy (1x1) para medição de texto */
cairo_t *create_measure_context(void);

/* begin_draw_xlib / end_draw — par open/close para desenho em X11 window */
cairo_t *begin_draw_xlib(Window w, int width, int height);
void     end_draw(cairo_t *cr);

/* redraw_all — redesenha frame de uma janela + taskbar */
void redraw_all(struct Win *iw);

#endif /* DRAW_H */

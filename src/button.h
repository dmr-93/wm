#ifndef BUTTON_H
#define BUTTON_H

#include <cairo/cairo.h>

/* =========================================================================
 * draw_bevel — bevel soft 2px
 * ========================================================================= */
void draw_bevel(cairo_t *cr, double x, double y, double w, double h, int raised);

/* =========================================================================
 * draw_btn — botão com bevel + gradiente + símbolo (21x21)
 * ========================================================================= */
void draw_btn(cairo_t *cr, int x, int y, const char *type);

#endif /* BUTTON_H */

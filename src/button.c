#include "config.h"
#include "draw.h"
#include "button.h"

#include <string.h>

/* =========================================================================
 * draw_bevel — bevel soft 2px
 * ========================================================================= */
void draw_bevel(cairo_t *cr, double x, double y, double w, double h, int raised) {
    (void)raised;
    cairo_set_line_width(cr, 1.0);
    /* Linha externa top+left — clara */
    SET_SOURCE_HEX(cr, 0xFFFFFF);
    cairo_move_to(cr, x+0.5, y+h-0.5); cairo_line_to(cr, x+0.5, y+0.5);
    cairo_line_to(cr, x+w-0.5, y+0.5); cairo_stroke(cr);
    /* Linha interna top+left — clara */
    cairo_move_to(cr, x+1.5, y+h-1.5); cairo_line_to(cr, x+1.5, y+1.5);
    cairo_line_to(cr, x+w-1.5, y+1.5); cairo_stroke(cr);
    /* Linha externa bottom+right — cinza como titlebar inativa */
    SET_SOURCE_HEX(cr, C_TITLE_INACT);
    cairo_move_to(cr, x+w-0.5, y+0.5); cairo_line_to(cr, x+w-0.5, y+h-0.5);
    cairo_line_to(cr, x+0.5, y+h-0.5); cairo_stroke(cr);
    /* Linha interna bottom+right — cinza como titlebar inativa */
    cairo_move_to(cr, x+w-1.5, y+1.5); cairo_line_to(cr, x+w-1.5, y+h-1.5);
    cairo_line_to(cr, x+1.5, y+h-1.5); cairo_stroke(cr);
}

/* =========================================================================
 * draw_btn — botão com bevel + gradiente + símbolo (21x21)
 * ========================================================================= */
void draw_btn(cairo_t *cr, int x, int y, const char *type) {
    draw_bevel(cr, x, y, BTN_W, BTN_H, 1);
    cairo_pattern_t *pat = cairo_pattern_create_linear(x, y+BTN_H, x+BTN_W, y);
    cairo_pattern_add_color_stop_rgb(pat, 0, 0.639, 0.639, 0.639);
    cairo_pattern_add_color_stop_rgb(pat, 1, 0.921, 0.921, 0.921);
    cairo_set_source(cr, pat);
    cairo_rectangle(cr, x+2, y+2, BTN_W-4, BTN_H-4);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    SET_SOURCE_HEX(cr, 0x000000);
    cairo_set_line_width(cr, 2.0);

    int off = 6;

    if (strcmp(type, "X") == 0) {
        cairo_move_to(cr, x+off,        y+off);
        cairo_line_to(cr, x+BTN_W-off,  y+BTN_H-off);
        cairo_move_to(cr, x+BTN_W-off,  y+off);
        cairo_line_to(cr, x+off,         y+BTN_H-off);
        cairo_stroke(cr);
    } else if (strcmp(type, "_") == 0) {
        cairo_move_to(cr, x+off,        y+BTN_H-off);
        cairo_line_to(cr, x+BTN_W-off,  y+BTN_H-off);
        cairo_stroke(cr);
    } else { /* "[]" resize */
        cairo_rectangle(cr, x+off, y+off, BTN_W-off*2, BTN_H-off*2);
        cairo_stroke(cr);
    }
}

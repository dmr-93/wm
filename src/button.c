#include "config.h"
#include "draw.h"
#include "button.h"

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
 * draw_btn — botão com bevel + gradiente + imagem XPM centralizada (21x21)
 * ========================================================================= */
void draw_btn(cairo_t *cr, int x, int y, cairo_surface_t *surf) {
    draw_bevel(cr, x, y, BTN_W, BTN_H, 1);
    cairo_pattern_t *pat = cairo_pattern_create_linear(x, y+BTN_H, x+BTN_W, y);
    cairo_pattern_add_color_stop_rgb(pat, 0, 0.639, 0.639, 0.639);
    cairo_pattern_add_color_stop_rgb(pat, 1, 0.921, 0.921, 0.921);
    cairo_set_source(cr, pat);
    cairo_rectangle(cr, x+2, y+2, BTN_W-4, BTN_H-4);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    /* Desenha a imagem XPM centralizada no botão, escalada proporcionalmente */
    if (surf) {
        int img_w = cairo_image_surface_get_width(surf);
        int img_h = cairo_image_surface_get_height(surf);
        if (img_w > 0 && img_h > 0) {
            /* Escala para caber dentro do botão com 2px de padding */
            int max_draw = BTN_W - 4;
            double scale = (double)max_draw / (img_w > img_h ? img_w : img_h);
            int draw_w = (int)(img_w * scale);
            int draw_h = (int)(img_h * scale);
            int ix = x + (BTN_W - draw_w) / 2;
            int iy = y + (BTN_H - draw_h) / 2;

            cairo_save(cr);
            cairo_translate(cr, ix, iy);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, surf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        }
    }
}

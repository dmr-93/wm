#include "types.h"
#include "draw.h"
#include "button.h"
#include "vitalogo.h"
#include "close.xpm.h"
#include "minimize.xpm.h"
#include "max_resize.xpm.h"

#include <cairo/cairo-xlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* =========================================================================
 * Logo surface — criado a partir do XPM embutido em vitalogo.h
 * ========================================================================= */
cairo_surface_t *logo_surf = NULL;
cairo_surface_t *close_btn_surf = NULL;
cairo_surface_t *minimize_btn_surf = NULL;
cairo_surface_t *max_resize_btn_surf = NULL;

/* Backing pixmap para a taskbar — registrado como background via
 * XSetWindowBackgroundPixmap. draw_taskbar() desenha no pixmap
 * e flush_taskbar_pixmap() re-registra + XClearWindow para que o
 * X11 auto-preencha a window a partir do pixmap em qualquer Expose. */
Pixmap taskbar_pixmap = None;

void create_taskbar_pixmap(void) {
    if (taskbar == None) return;
    taskbar_pixmap = XCreatePixmap(dpy, root, SW, TASKBAR_H, DefaultDepth(dpy, scr));
    /* Popula o pixmap com o conteúdo inicial e registra como background */
    draw_taskbar();
    flush_taskbar_pixmap();
}

void destroy_taskbar_pixmap(void) {
    if (taskbar_pixmap != None) { XFreePixmap(dpy, taskbar_pixmap); taskbar_pixmap = None; }
}

void flush_taskbar_pixmap(void) {
    if (taskbar_pixmap == None || taskbar == None) return;
    /* Registra o pixmap como background oficial da taskbar.
     * A partir de agora, X11 auto-preenche qualquer região exposta
     * da taskbar copiando deste pixmap — sem necessidade de handler. */
    XSetWindowBackgroundPixmap(dpy, taskbar, taskbar_pixmap);
    XClearWindow(dpy, taskbar);
    XSync(dpy, False);
}

/* =========================================================================
 * xpm_to_cairo_surface — Converte array XPM para cairo_surface_t ARGB32
 *
 * Formato suportado: XPM simples com 1 char/pixel, cores definidas como
 * "#RRGGBB" (sem nomes como "None" ou "s RGB..."). Usado exclusivamente
 * para o logo embutido em vitalogo.h.
 * ========================================================================= */
static cairo_surface_t *xpm_to_cairo_surface(char *xpm[]) {
    /* Header: "<width> <height> <ncolors> <cpp>" */
    int w, h, ncolors, cpp;
    if (sscanf(xpm[0], "%d %d %d %d", &w, &h, &ncolors, &cpp) != 4)
        return NULL;
    if (w <= 0 || h <= 0 || ncolors <= 0 || cpp != 1)
        return NULL;

    /* Lookup table indexada pelo char (0-255) */
    typedef struct { unsigned char r, g, b, a; } Color;
    Color color_lookup[256];
    memset(color_lookup, 0, sizeof(color_lookup)); /* default: transparente */

    for (int i = 0; i < ncolors; i++) {
        const char *line = xpm[1 + i];
        /* Formato: "X c #RRGGBB" — extrai o char e o hexadecimal */
        char ch;
        char hex_str[8];
        if (sscanf(line, " %c c #%6s", &ch, hex_str) < 2)
            continue;
        unsigned int hex;
        sscanf(hex_str, "%x", &hex);
        unsigned char uc = (unsigned char)ch;
        color_lookup[uc].r = (hex >> 16) & 0xFF;
        color_lookup[uc].g = (hex >> 8) & 0xFF;
        color_lookup[uc].b = hex & 0xFF;
        color_lookup[uc].a = 0xFF;
    }

    /* Cria superficie ARGB32 */
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    for (int y = 0; y < h; y++) {
        const char *row = xpm[1 + ncolors + y];
        uint32_t *pixel_row = (uint32_t *)(data + y * stride);
        for (int x = 0; x < w; x++) {
            unsigned char uc = (unsigned char)row[x];
            Color c = color_lookup[uc];
            /* Cairo ARGB32: 0xAARRGGBB em native byte order */
            pixel_row[x] = ((uint32_t)c.a << 24) |
                           ((uint32_t)c.r << 16) |
                           ((uint32_t)c.g <<  8) |
                           ((uint32_t)c.b);
        }
    }

    cairo_surface_mark_dirty(surface);
    return surface;
}

void init_logo(void) {
    if (!logo_surf) {
        logo_surf = xpm_to_cairo_surface(vitalogo);
        if (!logo_surf)
            fprintf(stderr, "[inferno_wm] Aviso: falha ao criar logo do XPM embutido.\n");
    }
    if (!close_btn_surf) {
        close_btn_surf = xpm_to_cairo_surface(close_xpm);
        if (!close_btn_surf)
            fprintf(stderr, "[inferno_wm] Aviso: falha ao criar surface close XPM.\n");
    }
    if (!minimize_btn_surf) {
        minimize_btn_surf = xpm_to_cairo_surface(minimize_xpm);
        if (!minimize_btn_surf)
            fprintf(stderr, "[inferno_wm] Aviso: falha ao criar surface minimize XPM.\n");
    }
    if (!max_resize_btn_surf) {
        max_resize_btn_surf = xpm_to_cairo_surface(max_resize_xpm);
        if (!max_resize_btn_surf)
            fprintf(stderr, "[inferno_wm] Aviso: falha ao criar surface max_resize XPM.\n");
    }
}

void free_logo(void) {
    if (logo_surf) { cairo_surface_destroy(logo_surf); logo_surf = NULL; }
    if (close_btn_surf) { cairo_surface_destroy(close_btn_surf); close_btn_surf = NULL; }
    if (minimize_btn_surf) { cairo_surface_destroy(minimize_btn_surf); minimize_btn_surf = NULL; }
    if (max_resize_btn_surf) { cairo_surface_destroy(max_resize_btn_surf); max_resize_btn_surf = NULL; }
}

/* =========================================================================
 * draw_text_centered — desenha texto centralizado verticalmente
 * com padding de FONT_PAD_LEFT à esquerda
 * ========================================================================= */
void draw_text_centered(cairo_t *cr, double x, double y, double h,
                        const char *text) {
    cairo_font_extents_t fe;
    cairo_font_extents(cr, &fe);
    double ty = y + (h / 2.0) + (fe.height / 2.0) - fe.descent;
    double tx = x + FONT_PAD_LEFT;
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, text);
}

/* =========================================================================
 * fill_rect_hex — preenche retângulo com cor em hexadecimal
 * ========================================================================= */
void fill_rect_hex(cairo_t *cr, unsigned int hex,
                   double x, double y, double w, double h) {
    SET_SOURCE_HEX(cr, hex);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

/* =========================================================================
 * setup_font — configura fonte (bold ou normal) com tamanho padrão
 * ========================================================================= */
void setup_font(cairo_t *cr, int bold) {
    cairo_select_font_face(cr, font_name, CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD
                                : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, FONT_SIZE);
}

/* =========================================================================
 * create_measure_context — context cairo dummy (1x1) para medição de texto
 * ========================================================================= */
cairo_t *create_measure_context(void) {
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
    cairo_t *cr = cairo_create(s);
    cairo_surface_destroy(s); /* surface mantida viva pelo cr */
    setup_font(cr, 1);
    return cr;
}

/* =========================================================================
 * begin_draw_xlib / end_draw — par open/close para desenho em X11 window
 * ========================================================================= */
cairo_t *begin_draw_xlib(Window w, int width, int height) {
    cairo_surface_t *surf = cairo_xlib_surface_create(
        dpy, w, DefaultVisual(dpy, scr), width, height);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf); return NULL; }
    cairo_t *cr = cairo_create(surf);
    if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr); cairo_surface_destroy(surf); return NULL; }
    /* Reseta qualquer estado de erro anterior do Cairo */
    cairo_set_source_rgb(cr, 0, 0, 0);
    return cr;
}

void end_draw(cairo_t *cr) {
    cairo_surface_t *surf = cairo_get_target(cr);
    cairo_surface_flush(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

/* =========================================================================
 * draw_frame — desenho do frame sem flickering
 * ========================================================================= */
void draw_frame(Win *iw) {
    if (!iw || iw->closing || iw->minimized) return;
    x_err = 0; XSync(dpy, False); if (x_err) return;

    cairo_t *cr = begin_draw_xlib(iw->frame, iw->w, iw->h);
    if (!cr) return;

    /* ── Bordas externas 1px ── */
    /* Esquerda + superior: #fbfbfa */
    fill_rect_hex(cr, C_BORDER_LT, 0, 0, 1, iw->h);
    fill_rect_hex(cr, C_BORDER_LT, 0, 0, iw->w, 1);

    /* Direita + inferior: #a5aeae */
    fill_rect_hex(cr, C_BORDER_RB, iw->w-1, 0, 1, iw->h);
    fill_rect_hex(cr, C_BORDER_RB, 0, iw->h-1, iw->w, 1);

    /* Fundo interno */
    fill_rect_hex(cr, C_FRAME, 1, 1, iw->w-2, iw->h-2);

    /* Titlebar — 23px a partir do topo */
    fill_rect_hex(cr, focused == iw ? C_TITLE_ACT : C_TITLE_INACT,
                  1, 1, iw->w-2, TITLE_HEIGHT);

    /* Título — centralizado verticalmente, 10px da borda esquerda */
    SET_SOURCE_HEX(cr, 0xFFFFFF);
    setup_font(cr, 1);
    draw_text_centered(cr, 1, 1, TITLE_HEIGHT,
                       iw->title[0] ? iw->title : " ");

    /* Botões: fechar à direita, minimizar, resize — sem gap */
    draw_btn(cr, BTN_CLOSE_X(iw), BTN_Y, close_btn_surf);
    draw_btn(cr, BTN_MIN_X(iw),   BTN_Y, minimize_btn_surf);
    draw_btn(cr, BTN_RSZ_X(iw),   BTN_Y, max_resize_btn_surf);

    end_draw(cr);
}

/* =========================================================================
 * draw_taskbar — desenha no backing pixmap
 * ========================================================================= */
void draw_taskbar(void) {
    if (taskbar == None) return;
    x_err = 0; XSync(dpy, False); if (x_err) return;

    /* Se o backing pixmap não existe (ex: chamado antes de create_taskbar_pixmap),
     * desenha direto na window mesmo (fallback). */
    Window target = (taskbar_pixmap != None) ? taskbar_pixmap : taskbar;

    int taskbar_off = TASKBAR_GAP_ENABLED ? 1 : 0;
    int active_h = TASKBAR_H - 2 - taskbar_off;
    cairo_t *cr = begin_draw_xlib(target, SW, TASKBAR_H);
    if (!cr) return;

    /* Se gap ativo, pinta o pixel do topo com cor de background */
    if (TASKBAR_GAP_ENABLED)
        fill_rect_hex(cr, C_BG_HEX, 0, 0, SW, 1);

    /* 2px preto na base */
    fill_rect_hex(cr, 0x000000, 0, TASKBAR_H-2, SW, 2);

    /* Fundo */
    fill_rect_hex(cr, C_FRAME, 0, taskbar_off, SW, active_h);

    /* Botão Inferno (quadrado preto + logo PNG centralizado) */
    fill_rect_hex(cr, 0x000000, 0, taskbar_off, 40, active_h);

    if (logo_surf) {
        /* Redimensiona proporcionalmente: maior lado = 36px */
        int logo_w = cairo_image_surface_get_width(logo_surf);
        int logo_h = cairo_image_surface_get_height(logo_surf);
        double scale = 36.0 / (logo_w > logo_h ? logo_w : logo_h);
        int draw_w = (int)(logo_w * scale);
        int draw_h = (int)(logo_h * scale);
        int lx = (40 - draw_w) / 2;
        int ly = taskbar_off + (active_h - draw_h) / 2;

        cairo_save(cr);
        cairo_translate(cr, lx, ly);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, logo_surf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        /* Fallback: X branco como antes */
        int icon = 16, ix = (40-icon)/2, iy2 = taskbar_off + (active_h-icon)/2;
        SET_SOURCE_HEX(cr, 0xFFFFFF);
        cairo_set_line_width(cr, 2);
        cairo_move_to(cr, ix, iy2);            cairo_line_to(cr, ix+icon, iy2+icon);
        cairo_move_to(cr, ix+icon, iy2);       cairo_line_to(cr, ix, iy2+icon);
        cairo_stroke(cr);
    }

    /* Botões das janelas — apenas janelas minimizadas (estilo Inferno OS) */
    setup_font(cr, 1);

    int bx = 41;
    for (Win *c = wins; c; c = c->next) {
        if (c->closing || !c->minimized) continue;
        const char *lbl = c->title[0] ? c->title : "app";
        cairo_text_extents_t te; cairo_text_extents(cr, lbl, &te);
        int bw = (int)te.x_advance + TASKBAR_PAD * 2;
        if (bx + bw > SW - 2) break;
        fill_rect_hex(cr, C_FRAME, bx, taskbar_off, bw, active_h);
        draw_bevel(cr, bx, taskbar_off, bw, active_h, 1);
        SET_SOURCE_HEX(cr, 0x000000);
        draw_text_centered(cr, bx, taskbar_off, active_h, lbl);
        bx += bw + 1;
    }

    end_draw(cr);
    flush_taskbar_pixmap();
}

/* =========================================================================
 * redraw_all — redesenha frame de uma janela + taskbar
 * ========================================================================= */
void redraw_all(Win *iw) {
    draw_frame(iw);
    draw_taskbar();
}

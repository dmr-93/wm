#include "types.h"
#include "draw.h"
#include "menu.h"
#include "window.h"

#include <cairo/cairo-xlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* =========================================================================
 * Variáveis globais do menu dinâmico
 * ========================================================================= */
MenuItem *menu_items = NULL;
int       menu_count = 0;

/* =========================================================================
 * spawn
 * ========================================================================= */
void spawn(const char *cmd) {
    if (fork() == 0) { setsid(); execl("/bin/sh","sh","-c",cmd,NULL); _exit(1); }
}

/* =========================================================================
 * Parser JSON minimalista para ~/.inferno_wm.conf
 *
 * Formato esperado:
 *   {
 *       "menu": [
 *           {"str":"xterm", "cmd":"xterm"},
 *           {"str":"xclock", "cmd":"xclock"}
 *       ]
 *   }
 *
 * Apenas extrai pares str/cmd de dentro do nó "menu",
 * ignora whitespace e formatação.
 * ========================================================================= */
static char *json_strdup(const char *start, const char *end) {
    size_t len = end - start;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

static int parse_json_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    /* Lê o arquivo inteiro */
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    if (flen <= 0) { fclose(f); return -1; }
    rewind(f);

    char *buf = malloc(flen + 1);
    if (!buf) { fclose(f); return -1; }
    size_t nread = fread(buf, 1, flen, f);
    fclose(f);
    buf[nread] = '\0';

    /* Primeira passagem: contar quantos objetos têm str/cmd */
    int count = 0;
    char *p = buf;
    while (*p) {
        /* Pula whitespace */
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (*p == '[' || *p == ']' || *p == ',' || *p == '{' || *p == '}') { p++; continue; }
        /* Procura por "str" */
        if (strncmp(p, "\"str\"", 5) == 0) {
            p += 5;
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')) p++;
            if (*p == '"') {
                p++; /* abre aspas */
                while (*p && *p != '"') p++;
                if (*p == '"') { p++; count++; }
            }
        } else {
            p++;
        }
    }

    if (count == 0) { free(buf); return 0; }

    /* Aloca os itens */
    menu_items = calloc(count, sizeof(MenuItem));
    if (!menu_items) { free(buf); return -1; }

    /* Segunda passagem: extrair str e cmd */
    int idx = 0;
    p = buf;
    char *cur_str = NULL, *cur_cmd = NULL;

    while (*p && idx < count) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (!*p) break;
        if (*p == '[' || *p == ']' || *p == ',' || *p == '{' || *p == '}') { p++; continue; }

        if (strncmp(p, "\"str\"", 5) == 0) {
            p += 5;
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')) p++;
            if (*p == '"') {
                p++;
                const char *sstart = p;
                while (*p && *p != '"') p++;
                if (*p == '"') {
                    cur_str = json_strdup(sstart, p);
                    p++;
                }
            }
            continue;
        }

        if (strncmp(p, "\"cmd\"", 5) == 0) {
            p += 5;
            while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':')) p++;
            if (*p == '"') {
                p++;
                const char *cstart = p;
                while (*p && *p != '"') p++;
                if (*p == '"') {
                    cur_cmd = json_strdup(cstart, p);
                    p++;
                }
            }
            /* Se temos str e cmd, adiciona */
            if (cur_str && cur_cmd) {
                menu_items[idx].label = cur_str;
                menu_items[idx].cmd   = cur_cmd;
                menu_items[idx].is_separator = 0;
                idx++;
                cur_str = NULL;
                cur_cmd = NULL;
            }
            continue;
        }

        p++;
    }

    menu_count = idx;
    free(buf);
    return idx;
}

/* =========================================================================
 * load_menu_config — Carrega itens do nó "menu" em ~/.inferno_wm.conf
 *                    e adiciona itens fixos
 * ========================================================================= */
void load_menu_config(void) {
    /* Libera configuração anterior se houver */
    free_menu_config();

    /* Tenta carregar do ~/.inferno_wm.conf */
    const char *home = getenv("HOME");
    char path[1024];
    int json_count = 0;

    if (home) {
        snprintf(path, sizeof(path), "%s/.inferno_wm.conf", home);
        json_count = parse_json_config(path);
    }

    /* Se não carregou nada do JSON, usa default único */
    if (json_count <= 0) {
        const char *default_label = "Xterm";
        const char *default_cmd   = "xterm";
        menu_items = calloc(1 + 4, sizeof(MenuItem)); /* +4 = separador + 3 fixos */
        if (!menu_items) return;

        menu_items[0].label = strdup(default_label);
        menu_items[0].cmd   = strdup(default_cmd);
        menu_items[0].is_separator = 0;
        menu_count = 1;
    }

    /* Adiciona os 3 itens fixos com separador */
    int base = menu_count;

    /* Realoca para caber os itens fixos (separador + Exit + Restart + More...) */
    MenuItem *tmp = realloc(menu_items, (menu_count + 4) * sizeof(MenuItem));
    if (!tmp) return;
    menu_items = tmp;

    /* Separador */
    menu_items[base].label       = strdup("---");
    menu_items[base].cmd         = NULL;
    menu_items[base].is_separator = 1;
    base++;

    /* More... */
    menu_items[base].label       = strdup("More...");
    menu_items[base].cmd         = strdup("xterm -e 'nano ~/.inferno_wm.conf'");
    menu_items[base].is_separator = 0;
    base++;

    /* Restart */
    menu_items[base].label       = strdup("Restart");
    menu_items[base].cmd         = NULL;  /* ação especial */
    menu_items[base].is_separator = 0;
    base++;

    /* Exit */
    menu_items[base].label       = strdup("Exit");
    menu_items[base].cmd         = NULL;  /* ação especial */
    menu_items[base].is_separator = 0;
    base++;

    menu_count = base;
}

/* =========================================================================
 * free_menu_config — Libera memória alocada
 * ========================================================================= */
void free_menu_config(void) {
    if (menu_items) {
        for (int i = 0; i < menu_count; i++) {
            free(menu_items[i].label);
            free(menu_items[i].cmd);
        }
        free(menu_items);
        menu_items = NULL;
    }
    menu_count = 0;
}

/* =========================================================================
 * Menu de aplicações
 * ========================================================================= */

/* Forward declarations for render helpers */
static void render_menu(Window mwin, int mw, int mh, int sel);
static void render_menu_item(cairo_t *cr, int mw, int i, int selected);
static int get_text_width(cairo_t *cr, const char *label);

void show_menu(int mx, int my) {
    int n = menu_count;
    if (!n) return;
    const int MPADX=10, MPADY=4, ITEMH=FONT_SIZE+MPADY*2+2;
    cairo_t *dc = create_measure_context();
    int mw=80;
    for (int i=0;i<n;i++) {
        if (menu_items[i].is_separator) continue;
        cairo_text_extents_t te; cairo_text_extents(dc,menu_items[i].label,&te);
        if ((int)te.x_advance+MPADX*2>mw) mw=(int)te.x_advance+MPADX*2;
    }
    cairo_destroy(dc);
    int mh=n*ITEMH+2;

    /* Se chamado para o botão X (mx=0, my próximo à taskbar), posiciona acima */
    if (mx == 0 && my >= SH - TASKBAR_H - 10) {
        my = SH - TASKBAR_H - mh;  /* base do menu no topo da taskbar */
        mx = 0;                     /* encostado à esquerda */
    } else {
        if (mx+mw>SW) mx=SW-mw;
        if (my+mh>SH-TASKBAR_H) my=SH-TASKBAR_H-mh;
    }

    XSetWindowAttributes swa={0};
    swa.override_redirect=True; swa.background_pixel=WhitePixel(dpy,scr);
    swa.event_mask=ButtonPressMask|ButtonReleaseMask|PointerMotionMask|ExposureMask;
    Window mwin=XCreateWindow(dpy,root,mx,my,mw,mh,1,CopyFromParent,InputOutput,CopyFromParent,
                               CWOverrideRedirect|CWBackPixel|CWEventMask,&swa);
    XMapRaised(dpy,mwin);
    XGrabPointer(dpy,mwin,False,ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                 GrabModeAsync,GrabModeAsync,None,cur_normal,CurrentTime);
    XGrabKeyboard(dpy,mwin,False,GrabModeAsync,GrabModeAsync,CurrentTime);

    /* Cria um context cairo para medições durante o loop de eventos */
    cairo_t *mcr = create_measure_context();

    int sel=-1,done=0;
    render_menu(mwin, mw, mh, sel); XFlush(dpy);
    while (!done) {
        XEvent ev; XNextEvent(dpy,&ev);
        if (ev.xany.window==mwin) {
            if (ev.type==MotionNotify) {
                int ns = (ev.xmotion.y - 1) / ITEMH;
                if (ns < 0 || ns >= n) ns = -1;
                /* Verifica se o X está dentro da área do texto do item */
                if (ns >= 0 && !menu_items[ns].is_separator) {
                    int tw = get_text_width(mcr, menu_items[ns].label);
                    int x_min = 1 + FONT_PAD_LEFT;
                    int x_max = x_min + tw + FONT_PAD_LEFT;
                    if (ev.xmotion.x < x_min || ev.xmotion.x > x_max)
                        ns = -1;
                }
                if (ns != sel) {
                    /* Redesenha apenas os dois itens afetados */
                    cairo_t *cr = begin_draw_xlib(mwin, mw, mh);
                    if (!cr) { sel = ns; continue; }
                    setup_font(cr, 0);
                    if (sel >= 0) render_menu_item(cr, mw, sel, 0);
                    if (ns >= 0)  render_menu_item(cr, mw, ns, 1);
                    end_draw(cr);
                    sel = ns;
                    XFlush(dpy);
                }
            }
            else if (ev.type==ButtonRelease) done=(sel>=0)?2:1;
            else if (ev.type==Expose) { render_menu(mwin,mw,mh,sel); XFlush(dpy); }
        } else if (ev.type==KeyPress||ev.type==ButtonPress) done=1;
    }
    cairo_destroy(mcr);
    XUngrabPointer(dpy,CurrentTime); XUngrabKeyboard(dpy,CurrentTime);
    XDestroyWindow(dpy,mwin); XFlush(dpy);

    if (done==2 && sel>=0) {
        MenuItem *item = &menu_items[sel];
        if (item->is_separator) {
            /* não faz nada */
        } else if (item->cmd) {
            spawn(item->cmd);
        } else if (strcmp(item->label, "Exit") == 0) {
            /* Sair do WM sem matar janelas */
            wm_running = 0;
        } else if (strcmp(item->label, "Restart") == 0) {
            /* Reiniciar o WM sem matar janelas */
            cleanup_wins();
            if (saved_argv && saved_argv[0]) {
                execvp(saved_argv[0], saved_argv);
                /* Se execvp falhar, tenta com sh -c */
                spawn(saved_argv[0]);
                wm_running = 0;
            }
        }
    }
}

static void render_menu(Window mwin, int mw, int mh, int sel) {
    cairo_t *cr=begin_draw_xlib(mwin, mw, mh);
    if (!cr) return;
    fill_rect_hex(cr, 0xEDEDED, 0, 0, mw, mh);
    draw_bevel(cr,0,0,mw,mh,1);
    setup_font(cr, 0);
    for (int i=0;i<menu_count;i++)
        render_menu_item(cr, mw, i, i == sel);
    end_draw(cr);
}

/* =========================================================================
 * render_menu_item — Desenha um único item do menu na posição i
 * ========================================================================= */
static void render_menu_item(cairo_t *cr, int mw, int i, int selected) {
    const int MPADX=10, MPADY=4, ITEMH=FONT_SIZE+MPADY*2+2;
    int iy = 1 + i * ITEMH;

    /* Limpa a área do item (restaura fundo) */
    fill_rect_hex(cr, 0xEDEDED, 1, iy, mw - 2, ITEMH);

    if (menu_items[i].is_separator) {
        /* Desenha linha separadora */
        int sep_y = iy + ITEMH / 2;
        SET_SOURCE_HEX(cr, 0x999999);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, MPADX, sep_y);
        cairo_line_to(cr, mw - MPADX, sep_y);
        cairo_stroke(cr);
        return;
    }

    if (selected) {
        fill_rect_hex(cr, C_TITLE_ACT, 1, iy, mw - 2, ITEMH);
        SET_SOURCE_HEX(cr, 0xFFFFFF);
    } else {
        SET_SOURCE_HEX(cr, 0x000000);
    }
    draw_text_centered(cr, 1, iy, ITEMH, menu_items[i].label);
}

/* =========================================================================
 * get_text_width — Retorna a largura do texto em pixels
 * ========================================================================= */
static int get_text_width(cairo_t *cr, const char *label) {
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);
    return (int)te.x_advance;
}

/* =========================================================================
 * taskbar_click
 * ========================================================================= */
void taskbar_click(int lx) {
    if (lx <= 40) {
        /* Botão X — mostra menu acima, encostado à esquerda */
        show_menu(0, SH - TASKBAR_H);
        return;
    }
    cairo_t *dc=create_measure_context();
    int bx=41;
    for (Win *c=wins;c;c=c->next) {
        if (c->closing) continue;
        const char *lbl=c->title[0]?c->title:"app";
        cairo_text_extents_t te; cairo_text_extents(dc,lbl,&te);
        int bw=(int)te.x_advance+TASKBAR_PAD*2;
        if (lx>=bx&&lx<bx+bw) {
            if (c->minimized) restore(c);
            else if (focused==c) minimize(c);
            else { XRaiseWindow(dpy,c->frame); focus_win(c); }
            break;
        }
        bx+=bw+1;
    }
    cairo_destroy(dc);
}

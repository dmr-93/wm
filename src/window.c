#include "types.h"
#include "atoms.h"
#include "draw.h"
#include "window.h"

#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* =========================================================================
 * Lookup helpers
 * ========================================================================= */
Win *by_frame(Window w)  { for (Win *c=wins;c;c=c->next) if(c->frame ==w) return c; return NULL; }
Win *by_client(Window w) { for (Win *c=wins;c;c=c->next) if(c->client==w) return c; return NULL; }

/* =========================================================================
 * sanitize_title — remove caracteres não-imprimíveis e emoji do título
 *
 * Firefox e outros navegadores incluem indicadores Unicode (como o ícone
 * de áudio 🔊 U+1F50A) no _NET_WM_NAME.  A fonte "Go" (ou monospace) não
 * possui glifos para esses caracteres, o que faz o Cairo retornar
 * x_advance = 0 ou valores inesperados em cairo_text_extents(), fazendo
 * com que os botões da taskbar desapareçam ou fiquem com largura zero.
 *
 * Esta função percorre a string UTF-8 e:
 *   - Remove caracteres de controle (exceto tab, newline, carriage return)
 *   - Remove caracteres fora do BMP (acima de U+FFFF) — onde ficam a
 *     maioria dos emojis
 *   - Remove caracteres das faixas Unicode de símbolos diversos que
 *     tipicamente não são renderizáveis por fontes de texto comuns
 *   - Mantém letras, números, pontuação comum e acentos latinos
 * ========================================================================= */
static void sanitize_title(char *dst) {
    if (!dst || !dst[0]) return;

    /* Tabela de faixas Unicode a remover (inclusive, exclusive) */
    static const struct {
        unsigned long lo, hi;
    } remove_ranges[] = {
        /* Control chars (exceto tab 0x09, newline 0x0A, CR 0x0D) */
        { 0x0000, 0x0008 },
        { 0x000B, 0x000C },
        { 0x000E, 0x001F },
        { 0x007F, 0x009F },
        /* Dingbats (U+2700–U+27BF) — inclui símbolos decorativos */
        { 0x2700, 0x27BF },
        /* Emoticons (U+1F600–U+1F64F) */
        { 0x1F600, 0x1F64F },
        /* Símbolos diversos e pictogramas (U+1F300–U+1F5FF) — inclui 🔊 */
        { 0x1F300, 0x1F5FF },
        /* Símbolos e pictogramas suplementares (U+1F900–U+1F9FF) */
        { 0x1F900, 0x1F9FF },
        /* Transporte e mapas (U+1F680–U+1F6FF) */
        { 0x1F680, 0x1F6FF },
        /* Símbolos diversos (U+2600–U+26FF) — clima, etc. */
        { 0x2600, 0x26FF },
        /* Símbolos miscelâneos (U+2300–U+23FF) */
        { 0x2300, 0x23FF },
        /* Símbolos de setas (U+2190–U+21FF) */
        { 0x2190, 0x21FF },
        /* Símbolos CJK e suplementares — raramente relevantes */
        { 0x2E80, 0x2EFF },
        { 0x3000, 0x303F },
        /* Símbolos de tags e variação de seletores (usados em emoji) */
        { 0xE0000, 0xE007F },
        { 0xFE000, 0xFE0FF },
        { 0xFE00, 0xFE0F },  /* variation selectors */
        /* Símbolos de meio-fio e largura zero */
        { 0x200B, 0x200F },  /* ZWSP, ZWNJ, ZWJ, LRM, RLM */
        { 0x2028, 0x202E },  /* separadores e direcionais */
        { 0x2060, 0x2064 },  /* word joiner, etc. */
    };
    static const int n_ranges = sizeof(remove_ranges) / sizeof(remove_ranges[0]);

    /* Buffer de saída — mesmo tamanho máximo que dst (512) */
    char out[512];
    int out_pos = 0;

    const unsigned char *s = (const unsigned char *)dst;
    while (*s && out_pos < 510) {
        unsigned long cp = 0;
        int seq_len = 0;

        /* Decodifica um caractere UTF-8 */
        if ((s[0] & 0x80) == 0) {
            cp = s[0];
            seq_len = 1;
        } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
            cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            seq_len = 2;
        } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            seq_len = 3;
        } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
            cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            seq_len = 4;
        } else {
            /* Sequência inválida — pula um byte */
            s++;
            continue;
        }

        /* Verifica se o code point está em alguma faixa a remover */
        int remove_it = 0;
        for (int i = 0; i < n_ranges; i++) {
            if (cp >= remove_ranges[i].lo && cp <= remove_ranges[i].hi) {
                remove_it = 1;
                break;
            }
        }

        if (!remove_it) {
            /* Copia a sequência UTF-8 para a saída */
            for (int i = 0; i < seq_len && out_pos < 511; i++)
                out[out_pos++] = s[i];
        }

        s += seq_len;
    }

    out[out_pos] = '\0';
    strcpy(dst, out);
}

/* =========================================================================
 * get_title — leitura do nome da janela
 * ========================================================================= */
void get_title(Window w, char *dst) {
    dst[0] = '\0';
    Atom t; int fmt; unsigned long n, ba; unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, w, _NET_WM_NAME, 0, 1024, False, UTF8_STRING,
            &t, &fmt, &n, &ba, &data) == Success && data && n) {
        strncpy(dst, (char *)data, 511); dst[511] = '\0'; XFree(data);
        sanitize_title(dst);
        return;
    }
    if (data) { XFree(data); data = NULL; }
    XTextProperty tp = {0};
    if (XGetWMName(dpy, w, &tp) && tp.value && tp.nitems) {
        char **lst = NULL; int cnt = 0;
        if (XmbTextPropertyToTextList(dpy, &tp, &lst, &cnt) == Success && cnt > 0)
            strncpy(dst, lst[0], 511);
        else
            strncpy(dst, (char *)tp.value, 511);
        dst[511] = '\0';
        if (lst) XFreeStringList(lst);
        if (tp.value) XFree(tp.value);
        sanitize_title(dst);
        if (dst[0]) return;
    }
    XClassHint ch = {0};
    if (XGetClassHint(dpy, w, &ch)) {
        if (ch.res_name) strncpy(dst, ch.res_name, 511);
        dst[511] = '\0';
        if (ch.res_name)  XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
        sanitize_title(dst);
        if (dst[0]) return;
    }
    strcpy(dst, "app");
}

/* =========================================================================
 * skip_window — ignorar janelas de dock, desktop, etc.
 * ========================================================================= */
int skip_window(Window w) {
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, w, &wa)) return 1;
    if (wa.override_redirect) return 1;
    Atom t; int fmt; unsigned long n, ba; unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, w, _NET_WM_WINDOW_TYPE, 0, 32, False,
            XA_ATOM, &t, &fmt, &n, &ba, &data) == Success && data) {
        Atom *types = (Atom *)data; int skip = 0;
        for (unsigned long i = 0; i < n; i++) {
            if (types[i] == _NET_WM_WINDOW_TYPE_DOCK     ||
                types[i] == _NET_WM_WINDOW_TYPE_DESKTOP  ||
                types[i] == _NET_WM_WINDOW_TYPE_SPLASH   ||
                types[i] == _NET_WM_WINDOW_TYPE_TOOLTIP  ||
                types[i] == _NET_WM_WINDOW_TYPE_NOTIFICATION ||
                types[i] == _NET_WM_WINDOW_TYPE_MENU     ||
                types[i] == _NET_WM_WINDOW_TYPE_POPUP_MENU ||
                types[i] == _NET_WM_WINDOW_TYPE_TOOLBAR  ||
                types[i] == _NET_WM_WINDOW_TYPE_UTILITY) { skip = 1; break; }
        }
        XFree(data); if (skip) return 1;
    }
    return 0;
}

/* =========================================================================
 * Posicionamento em cascata
 * ========================================================================= */
int px = 20, py = 20;

static void next_pos(int *ox, int *oy) {
    *ox = px; *oy = py; px += 24; py += 24;
    if (px > SW/2) { px = 20; py = 20; }
}

/* =========================================================================
 * manage — gerenciar janela
 *
 * NOTA: Todas as janelas gerenciadas recebem decoração (titlebar + bordas +
 * botões) independentemente de _MOTIF_WM_HINTS. O campo iw->decorated é
 * sempre 1 para janelas comuns; o fullscreen ainda pode setá-lo como 0.
 * ========================================================================= */
void manage(Window client, int pre) {
    if (skip_window(client)) { XMapWindow(dpy, client); return; }
    if (by_client(client)) return;
    x_err = 0;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, client, &wa)) return;
    if (pre && wa.map_state == IsUnmapped) {
        /* Janela órfã que ficou Unmapped (ex: após cleanup_wins sem frame).
         * Mapeia para que possa ser re-gerenciada. */
        XMapWindow(dpy, client);
        XSync(dpy, False);
        /* Re-obtem atributos após o map */
        if (!XGetWindowAttributes(dpy, client, &wa)) return;
    }

    Win *iw = calloc(1, sizeof(Win));
    if (!iw) return;

    int cw = (wa.width  > 1) ? wa.width  : SW/2;
    int ch = (wa.height > 1) ? wa.height : (SH-TASKBAR_H)/2;

    /* Todas as janelas recebem decoração forçada */
    iw->decorated = 1;
    iw->w = cw + 2;
    iw->h = ch + TITLE_HEIGHT + 2;

    if (wa.x != 0 || wa.y != 0) { iw->x = wa.x; iw->y = wa.y; }
    else {
        Window trans = None; XGetTransientForHint(dpy, client, &trans);
        if (trans != None) {
            Win *par = by_client(trans);
            if (par) { iw->x = par->x + (par->w-iw->w)/2; iw->y = par->y + (par->h-iw->h)/2; }
            else     { iw->x = (SW-iw->w)/2; iw->y = (SH-TASKBAR_H-iw->h)/2; }
        } else { next_pos(&iw->x, &iw->y); }
    }
    if (iw->x < 0) iw->x = 0;
    if (iw->y < 0) iw->y = 0;
    if (iw->x + iw->w > SW) iw->x = SW - iw->w;
    if (iw->y + iw->h > SH - TASKBAR_H) iw->y = SH - TASKBAR_H - iw->h;
    if (iw->x < 0) iw->x = 0;
    if (iw->y < 0) iw->y = 0;

    get_title(client, iw->title);

    XSetWindowAttributes swa = {0};
    swa.background_pixmap = None;
    swa.event_mask = (ExposureMask | ButtonPressMask | ButtonReleaseMask | SubstructureNotifyMask);
    iw->frame = XCreateWindow(dpy, root, iw->x, iw->y, iw->w, iw->h, 0,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixmap | CWEventMask, &swa);
    if (!iw->frame) { free(iw); return; }

    iw->client = client;
    XSetWindowBorderWidth(dpy, client, 0);

    XGrabServer(dpy);
    x_err = 0;
    XReparentWindow(dpy, client, iw->frame, 1, TITLE_HEIGHT);
    XSync(dpy, False);
    if (x_err) { XUngrabServer(dpy); XDestroyWindow(dpy, iw->frame); free(iw); return; }
    XUngrabServer(dpy);

    position_client(iw);
    XSync(dpy, False);
    XSelectInput(dpy, client, StructureNotifyMask | PropertyChangeMask);
    XGrabButton(dpy, Button1, AnyModifier, client, True,
                ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    XSelectInput(dpy, iw->frame, ExposureMask | ButtonPressMask | ButtonReleaseMask | SubstructureNotifyMask);

    set_wm_state(client, 1);
    /* Mapeia o frame PRIMEIRO para que o client seja viewable imediatamente
     * quando mapeado. Isso evita BadMatch em XSetInputFocus chamado por
     * toolkits como SDL 1.2 logo após mapear sua janela. */
    XMapWindow(dpy, iw->frame);
    XSync(dpy, False);
    XMapWindow(dpy, client);
    XSync(dpy, False);

    /* Gera ConfigureNotify real (não sintético) para toolkits como SDL
     * que ignoram eventos send_event=True. */
    position_client(iw);
    XSync(dpy, False);

    send_configure(iw);

    iw->next = wins; wins = iw;
    focused = iw;
    XSetInputFocus(dpy, iw->client, RevertToPointerRoot, CurrentTime);
    set_net_active(iw->client);
    XSync(dpy, False);
    /* Redesenha TODAS as janelas para garantir titlebars e botões corretos */
    for (Win *c = wins; c; c = c->next)
        if (!c->closing && !c->minimized) draw_frame(c);
    update_client_list();
    draw_taskbar();
}

/* GC para overlay vermelho de resize — declarado aqui para cleanup_wins() poder liberá-lo */
GC red_gc = NULL;

/* Overlay window — janela opaca com bordas vermelhas e cruz divisória
 * para resize por quadrantes. Criada quando o usuário clica em [] (armar). */
Window overlay_win = None;
Win *armed_win = NULL;

/* =========================================================================
 * unmanage — desgerenciar sem ghost window
 * ========================================================================= */
void unmanage(Window client) {
    Win **pp = &wins, *iw = NULL;
    while (*pp) {
        if ((*pp)->client == client) { iw = *pp; *pp = iw->next; break; }
        pp = &(*pp)->next;
    }
    if (!iw) return;
    if (overlay_win != None && armed_win == iw) {
        destroy_resize_overlay();
        armed_win = NULL;
    }
    iw->closing = 1;

    XGrabServer(dpy);
    x_err = 0;
    XUnmapWindow(dpy, iw->frame);
    XReparentWindow(dpy, client, root, iw->x, iw->y + TITLE_HEIGHT);
    XSync(dpy, False);
    XDestroyWindow(dpy, iw->frame);
    XSync(dpy, False);
    XUngrabServer(dpy);

    XClearArea(dpy, root, iw->x, iw->y, iw->w, iw->h, True);
    XFlush(dpy);

    if (focused == iw) {
        focus_next_available();
    }
    free(iw);
    update_client_list();
    draw_taskbar();
}

/* =========================================================================
 * cleanup_wins — reparenta todos os clientes para root e destrói frames
 * Chamado antes de XCloseDisplay (Exit) ou antes de execvp (Restart)
 * para que as janelas dos aplicativos não sejam mortas.
 *
 * IMPORTANTE:
 * 1. NÃO desmapeia o frame antes de reparentar, pois isso deixaria o client
 *    window com map_state == IsUnmapped. Quando o WM reiniciar, manage()
 *    com pre=1 rejeitaria a janela.
 * 2. Reparenta o client em root na posição (iw->x, iw->y) — SEM o offset
 *    TITLE_HEIGHT para janelas decorated. Isso porque quando o novo WM
 *    chamar manage(), ele usará wa.x/wa.y como posição do frame, e o client
 *    será reposicionado dentro do frame em (1, TITLE_HEIGHT). Se usássemos
 *    (iw->x, iw->y + TITLE_HEIGHT), o client ficaria deslocado TITLE_HEIGHT
 *    pixels para baixo após o re-gerenciamento.
 * ========================================================================= */
void cleanup_wins(void) {
    Win *iw = wins;
    while (iw) {
        Win *next = iw->next;
        if (!iw->closing) {
            XGrabServer(dpy);
            x_err = 0;
            /* NÃO desmapeia o frame — isso deixaria o client Unmapped */
            /* Reparenta em (iw->x, iw->y) — sem offset TITLE_HEIGHT.
             * O novo WM usará wa.x/wa.y como posição do frame, e o client
             * será colocado dentro do frame em (1, TITLE_HEIGHT). */
            XReparentWindow(dpy, iw->client, root, iw->x, iw->y);
            XSync(dpy, False);
            XUngrabServer(dpy);
            set_wm_state(iw->client, 0);  /* WithdrawnState — para o novo WM poder re-gerenciar */
            XDeleteProperty(dpy, iw->client, _NET_WM_STATE);
        }
        XDestroyWindow(dpy, iw->frame);
        free(iw);
        iw = next;
    }
    wins = NULL;
    focused = NULL;
    if (red_gc) { XFreeGC(dpy, red_gc); red_gc = NULL; }
    XSync(dpy, False);
}

/* =========================================================================
 * focus_win
 * ========================================================================= */
void focus_win(Win *w) {
    if (focused == w) {
        if (w && !w->minimized) { draw_frame(w); draw_taskbar(); }
        return;
    }
    Win *old = focused;
    focused = w;
    if (w && !w->minimized) {
        x_err = 0;
        XSetInputFocus(dpy, w->client, RevertToPointerRoot, CurrentTime);
        XSync(dpy, False);
        x_err = 0;
        set_net_active(w->client);
    } else {
        x_err = 0;
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XSync(dpy, False);
        x_err = 0;
        Window none = None;
        XChangeProperty(dpy, root, _NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&none, 1);
    }
    if (old && !old->closing && !old->minimized) draw_frame(old);
    if (w   && !w->closing   && !w->minimized)   draw_frame(w);
    draw_taskbar();
}

/* =========================================================================
 * toggle_max — maximizar / restaurar
 * ========================================================================= */
void toggle_max(Win *iw) {
    if (iw->maximized) {
        iw->w = iw->sw; iw->h = iw->sh; iw->x = iw->sx; iw->y = iw->sy;
        iw->maximized = 0; set_net_maximized(iw->client, 0);
    } else {
        iw->sx = iw->x; iw->sy = iw->y; iw->sw = iw->w; iw->sh = iw->h;
        iw->x = 0; iw->y = 0; iw->w = SW; iw->h = SH - TASKBAR_H;
        iw->maximized = 1; set_net_maximized(iw->client, 1);
    }
    XMoveResizeWindow(dpy, iw->frame, iw->x, iw->y, iw->w, iw->h);
    position_client(iw);
    XSync(dpy, False); draw_frame(iw); send_configure(iw); draw_taskbar();
}

/* =========================================================================
 * toggle_fullscreen — entrar / sair de tela cheia
 * ========================================================================= */
void toggle_fullscreen(Win *iw) {
    if (iw->fullscreen) {
        /* Sair do fullscreen */
        iw->fullscreen = 0;
        iw->decorated = 1;
        iw->w = iw->sw; iw->h = iw->sh; iw->x = iw->sx; iw->y = iw->sy;
        XMoveResizeWindow(dpy, iw->frame, iw->x, iw->y, iw->w, iw->h);
        position_client(iw);
        XMapWindow(dpy, iw->frame);
        set_net_fullscreen(iw->client, 0);
    } else {
        /* Entrar em fullscreen */
        iw->sx = iw->x; iw->sy = iw->y; iw->sw = iw->w; iw->sh = iw->h;
        iw->fullscreen = 1;
        iw->decorated = 0;
        iw->x = 0; iw->y = 0; iw->w = SW; iw->h = SH;
        XMoveResizeWindow(dpy, iw->frame, iw->x, iw->y, iw->w, iw->h);
        position_client(iw);
        XRaiseWindow(dpy, iw->frame);
        set_net_fullscreen(iw->client, 1);
    }
    XSync(dpy, False);
    draw_frame(iw);
    send_configure(iw);
    draw_taskbar();
}

/* =========================================================================
 * minimize / restore
 * ========================================================================= */
void minimize(Win *iw) {
    iw->minimized = 1;
    XUnmapWindow(dpy, iw->frame);
    set_wm_state(iw->client, 3);
    set_net_hidden(iw->client, 1);
    if (focused == iw) {
        focus_next_available();
    }
    draw_taskbar();
}

void restore(Win *iw) {
    iw->minimized = 0;
    set_wm_state(iw->client, 1);
    set_net_hidden(iw->client, 0);
    XMapWindow(dpy, iw->frame);
    XRaiseWindow(dpy, iw->frame);
    focus_win(iw);
    redraw_all(iw);
    send_configure(iw);
}

/* =========================================================================
 * Resize interativo com overlay vermelho (XOR no root — sem janelas)
 * =========================================================================
 * Desenha as bordas vermelhas diretamente no root usando GXxor.
 * Para apagar, basta desenhar o mesmo retângulo novamente (XOR é reversível).
 * Isso não gera eventos Expose em janela alguma e não repinta a titlebar.
 * ========================================================================= */
static int     red_old_x, red_old_y, red_old_w, red_old_h;
static int     red_active = 0;

void red_ensure(void) {
    if (red_gc) return;
    XGCValues gcv;
    gcv.function = GXxor;
    gcv.foreground = 0xFF0000 ^ WhitePixel(dpy, scr); /* XOR com branco = vermelho */
    gcv.line_width = RBW;
    gcv.subwindow_mode = IncludeInferiors; /* desenha sobre tudo */
    red_gc = XCreateGC(dpy, root,
        GCFunction | GCForeground | GCLineWidth | GCSubwindowMode, &gcv);
    XFlush(dpy);
}

static void red_draw_rects(int x, int y, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* Desenha 4 retângulos preenchidos (bordas) usando XOR */
    XFillRectangle(dpy, root, red_gc, x,       y,       w, RBW);       /* topo */
    XFillRectangle(dpy, root, red_gc, x,       y+h-RBW, w, RBW);       /* base */
    XFillRectangle(dpy, root, red_gc, x,       y,       RBW, h);       /* esquerda */
    XFillRectangle(dpy, root, red_gc, x+w-RBW, y,       RBW, h);       /* direita */
}

void red_paint(int x, int y, int w, int h) {
    /* Se já havia um retângulo ativo, apaga ele primeiro */
    if (red_active)
        red_draw_rects(red_old_x, red_old_y, red_old_w, red_old_h);
    /* Desenha o novo */
    red_draw_rects(x, y, w, h);
    red_old_x = x; red_old_y = y;
    red_old_w = w; red_old_h = h;
    red_active = 1;
    XFlush(dpy);
}

void red_hide(void) {
    if (red_active) {
        red_draw_rects(red_old_x, red_old_y, red_old_w, red_old_h);
        red_active = 0;
        XFlush(dpy);
    }
}

/* =========================================================================
 * Overlay window — janela opaca com bordas vermelhas e cruz divisória
 * para resize por quadrantes.
 *
 * Criada quando o usuário clica em [] (armar). Fica sempre em cima da
 * janela alvo, bloqueando interação com o conteúdo. O overlay é dividido
 * em 4 quadrantes por uma cruz (+). Cada quadrante controla um canto
 * específico do resize.
 * ========================================================================= */

/* Desenha o conteúdo do overlay: fundo opaco (C_BG_HEX), bordas vermelhas
 * sólidas (3px) e cruz divisória pontilhada (1px).
 *
 * O overlay é totalmente opaco — sem alpha blending. O fundo é pintado
 * com a mesma cor do background do WM (C_BG_HEX = 0x444444, cinza escuro).
 * A cruz no centro usa linhas pontilhadas (dash pattern {4, 4}) de 1px. */
static void draw_overlay_contents(cairo_t *cr, int w, int h) {
    /* Fundo opaco — mesma cor do background do WM */
    SET_SOURCE_HEX(cr, C_BG_HEX);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    /* Bordas vermelhas (3px) — totalmente opacas */
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_rectangle(cr, 0, 0, w, RBW);            /* topo */
    cairo_rectangle(cr, 0, h - RBW, w, RBW);      /* base */
    cairo_rectangle(cr, 0, 0, RBW, h);            /* esquerda */
    cairo_rectangle(cr, w - RBW, 0, RBW, h);      /* direita */
    cairo_fill(cr);

    /* Cruz divisória — linha pontilhada (1px) no centro */
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    /* Configura para linha pontilhada: 4px traço, 4px espaço */
    const double dash[] = {4.0, 4.0};
    cairo_set_dash(cr, dash, 2, 0);
    /* Linha vertical */
    cairo_move_to(cr, w / 2.0 + 0.5, RBW);
    cairo_line_to(cr, w / 2.0 + 0.5, h - RBW);
    cairo_stroke(cr);
    /* Linha horizontal */
    cairo_move_to(cr, RBW, h / 2.0 + 0.5);
    cairo_line_to(cr, w - RBW, h / 2.0 + 0.5);
    cairo_stroke(cr);
    /* Reseta o dash para não afetar desenhos futuros */
    cairo_set_dash(cr, NULL, 0, 0);
}

Window create_resize_overlay(Win *iw) {
    if (overlay_win != None)
        destroy_resize_overlay();

    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask        = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    attrs.cursor            = cur_br;
    attrs.background_pixmap = None;
    attrs.border_pixel      = 0;
    attrs.background_pixel  = C_BG_HEX;  /* X11 preenche fundo automaticamente */

    unsigned long mask = CWOverrideRedirect | CWEventMask | CWCursor |
                         CWBackPixmap | CWBorderPixel | CWBackPixel;

    overlay_win = XCreateWindow(dpy, root,
                                iw->x, iw->y, iw->w, iw->h, 0,
                                CopyFromParent,
                                InputOutput,
                                CopyFromParent,
                                mask, &attrs);
    XMapWindow(dpy, overlay_win);
    XRaiseWindow(dpy, overlay_win);

    /* Desenha o conteúdo via Cairo */
    cairo_t *cr = begin_draw_xlib(overlay_win, iw->w, iw->h);
    if (cr) {
        draw_overlay_contents(cr, iw->w, iw->h);
        end_draw(cr);
    }

    XFlush(dpy);
    return overlay_win;
}

void destroy_resize_overlay(void) {
    if (overlay_win != None) {
        XDestroyWindow(dpy, overlay_win);
        overlay_win = None;
        XFlush(dpy);
    }
}

void move_resize_overlay(int x, int y, int w, int h) {
    if (overlay_win == None) return;
    XMoveResizeWindow(dpy, overlay_win, x, y, w, h);

    /* Redesenha o conteúdo */
    cairo_t *cr = begin_draw_xlib(overlay_win, w, h);
    if (cr) {
        draw_overlay_contents(cr, w, h);
        end_draw(cr);
    }

    XFlush(dpy);
}

void do_resize(Win *iw) {
    (void)iw;
    /* Placeholder — resize interativo usa overlay agora (events.c) */
}

/* =========================================================================
 * hit_titlebar — hit-test na titlebar
 * ========================================================================= */
int hit_titlebar(Win *iw, int lx, int ly) {
    if (ly < 0 || ly >= TITLE_HEIGHT) return HIT_NONE;
    if (lx >= BTN_CLOSE_X(iw) && lx < BTN_CLOSE_X(iw) + BTN_W) return HIT_CLOSE;
    if (lx >= BTN_MIN_X(iw)   && lx < BTN_MIN_X(iw)   + BTN_W) return HIT_MIN;
    if (lx >= BTN_RSZ_X(iw)   && lx < BTN_RSZ_X(iw)   + BTN_W) return HIT_RESIZE;
    return HIT_TITLE;
}

/* =========================================================================
 * position_client — reposiciona o client dentro do frame
 * ========================================================================= */
void position_client(Win *iw) {
    XMoveResizeWindow(dpy, iw->client,
                      CLIENT_X(iw), CLIENT_Y(iw),
                      CLIENT_W(iw), CLIENT_H(iw));
}

/* =========================================================================
 * focus_next_available — foca a próxima janela disponível
 * ========================================================================= */
void focus_next_available(void) {
    focused = NULL;
    for (Win *c = wins; c; c = c->next) {
        if (!c->closing && !c->minimized) {
            focused = c;
            XRaiseWindow(dpy, c->frame);
            XSetInputFocus(dpy, c->client, RevertToPointerRoot, CurrentTime);
            set_net_active(c->client);
            draw_frame(c);
            return;
        }
    }
    /* Nenhuma janela disponível */
    Window none = None;
    XChangeProperty(dpy, root, _NET_ACTIVE_WINDOW, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *)&none, 1);
}

/* =========================================================================
 * configure_screen — reposiciona todas as janelas quando a resolução muda
 *
 * Chamado quando o root window recebe ConfigureNotify com novo tamanho
 * (ex: xrandr, troca de monitor). Recebe new_SW/new_SH diretamente do
 * evento ConfigureNotify para evitar problemas com cache stale do
 * DisplayWidth()/DisplayHeight().
 *
 * Reposiciona janelas proporcionalmente, redimensiona a taskbar e
 * redesenha tudo.
 * ========================================================================= */
void configure_screen(int new_SW, int new_SH) {
    if (new_SW == SW && new_SH == SH) return;

    int old_SW = SW, old_SH = SH;
    SW = new_SW; SH = new_SH;

    /* Reposiciona todas as janelas proporcionalmente */
    for (Win *iw = wins; iw; iw = iw->next) {
        if (iw->closing) continue;

        if (iw->fullscreen) {
            iw->x = 0; iw->y = 0;
            iw->w = SW; iw->h = SH;
        } else if (iw->maximized) {
            iw->x = 0; iw->y = 0;
            iw->w = SW; iw->h = SH - TASKBAR_H;
        } else {
            int nx = (iw->x * SW) / old_SW;
            int ny = (iw->y * SH) / old_SH;
            int nw = (iw->w * SW) / old_SW;
            int nh = (iw->h * SH) / old_SH;

            if (nw < MIN_WIN_W) nw = MIN_WIN_W;
            if (nh < MIN_WIN_H + TITLE_HEIGHT) nh = MIN_WIN_H + TITLE_HEIGHT;

            if (nx + nw > SW) nx = SW - nw;
            if (ny + nh > SH - TASKBAR_H) ny = SH - TASKBAR_H - nh;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;

            iw->x = nx; iw->y = ny;
            iw->w = nw; iw->h = nh;
        }

        XMoveResizeWindow(dpy, iw->frame, iw->x, iw->y, iw->w, iw->h);
        position_client(iw);
    }

    /* Reposiciona e redimensiona a taskbar */
    int taskbar_y = TASKBAR_GAP_ENABLED ? (SH - TASKBAR_H + 1) : (SH - TASKBAR_H);
    XMoveResizeWindow(dpy, taskbar, 0, taskbar_y, SW, TASKBAR_H);

    /* Atualiza _NET_WM_STRUT com a nova largura */
    unsigned long strut[4] = {0, 0, 0, (unsigned long)TASKBAR_H};
    XChangeProperty(dpy, taskbar, _NET_WM_STRUT, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)strut, 4);

    /* Limpa e redesenha o root */
    XSetWindowBackground(dpy, root, C_BG_HEX);
    XClearWindow(dpy, root);

    /* Redesenha todas as janelas */
    for (Win *iw = wins; iw; iw = iw->next) {
        if (!iw->closing && !iw->minimized)
            draw_frame(iw);
    }
    draw_taskbar();
    XFlush(dpy);
}

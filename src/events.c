#include "types.h"
#include "atoms.h"
#include "draw.h"
#include "window.h"
#include "menu.h"
#include "events.h"

#include <X11/Xatom.h>
#include <X11/keysym.h>

/* =========================================================================
 * Variáveis de drag — usadas para drag iniciado no client window
 * (diferente do drag por titlebar que usa inner loop, e do
 * _NET_WM_MOVERESIZE que também usa inner loop).
 * ========================================================================= */
static int drag = 0;
static int drag_sx = 0, drag_sy = 0;
static Win *drag_win = NULL;

/* =========================================================================
 * Variáveis de arm/resize — overlay window + janela alvo
 * ========================================================================= */
extern Window overlay_win;
extern Win *armed_win;          /* definido em window.c */

/* =========================================================================
 * Quadrantes do resize
 * ========================================================================= */
typedef enum {
    QUAD_TL,  /* Top-Left     */
    QUAD_TR,  /* Top-Right    */
    QUAD_BL,  /* Bottom-Left  */
    QUAD_BR   /* Bottom-Right */
} Quadrant;

/* Detecta em qual quadrante do overlay o clique ocorreu */
static Quadrant detect_quadrant(int mx, int my, int w, int h) {
    int left_half = (mx < w / 2);
    int top_half  = (my < h / 2);
    if (top_half && left_half)  return QUAD_TL;
    if (top_half && !left_half) return QUAD_TR;
    if (!top_half && left_half)  return QUAD_BL;
    return QUAD_BR;
}

/* Calcula o novo retângulo baseado no quadrante e delta do mouse
 * a partir do ponto de ancoragem (anchor_x, anchor_y).
 *
 * Cada quadrante aplica o delta (mx - anchor_x, my - anchor_y) nos
 * lados apropriados:
 *   BR: bottom + right  - nw += dx, nh += dy
 *   BL: bottom + left   - nx += dx, nw -= dx, nh += dy
 *   TR: top + right     - ny += dy, nw += dx, nh -= dy
 *   TL: top + left      - nx += dx, ny += dy, nw -= dx, nh -= dy
 *
 * Mover o mouse na direcao que o quadrante representa aumenta o
 * tamanho da janela. Mover na direcao oposta diminui. */
static void calc_quadrant_rect(Quadrant q,
    int orig_x, int orig_y, int orig_w, int orig_h,
    int anchor_x, int anchor_y,
    int mx, int my,
    int *new_x, int *new_y, int *new_w, int *new_h)
{
    int dx = mx - anchor_x;
    int dy = my - anchor_y;
    int nx = orig_x, ny = orig_y, nw = orig_w, nh = orig_h;

    switch (q) {
    case QUAD_BR: /* Bottom-Right: grow/shrink bottom & right */
        nw = orig_w + dx;
        nh = orig_h + dy;
        break;
    case QUAD_BL: /* Bottom-Left: grow/shrink bottom & left */
        nx = orig_x + dx;
        nw = orig_w - dx;
        nh = orig_h + dy;
        break;
    case QUAD_TR: /* Top-Right: grow/shrink top & right */
        ny = orig_y + dy;
        nw = orig_w + dx;
        nh = orig_h - dy;
        break;
    case QUAD_TL: /* Top-Left: grow/shrink top & left */
        nx = orig_x + dx;
        ny = orig_y + dy;
        nw = orig_w - dx;
        nh = orig_h - dy;
        break;
    }

    /* Clamp minimum size */
    if (nw < MIN_WIN_W) {
        if (q == QUAD_TL || q == QUAD_BL)
            nx = orig_x + orig_w - MIN_WIN_W;
        nw = MIN_WIN_W;
    }
    if (nh < MIN_WIN_H + TITLE_HEIGHT) {
        if (q == QUAD_TL || q == QUAD_TR)
            ny = orig_y + orig_h - (MIN_WIN_H + TITLE_HEIGHT);
        nh = MIN_WIN_H + TITLE_HEIGHT;
    }

    /* Clamp to screen */
    if (nx + nw > SW) nw = SW - nx;
    if (ny + nh > SH - TASKBAR_H) nh = SH - TASKBAR_H - ny;
    if (nx < 0) { if (q == QUAD_TL || q == QUAD_BL) nw += nx; nx = 0; }
    if (ny < 0) { if (q == QUAD_TL || q == QUAD_TR) nh += ny; ny = 0; }

    *new_x = nx; *new_y = ny;
    *new_w = nw; *new_h = nh;
}

/* =========================================================================
 * alt_tab
 * ========================================================================= */
void alt_tab(void) {
    Win *start = focused ? focused->next : wins;
    for (Win *c=start;c;c=c->next) {
        if (!c->closing) { if(c->minimized)restore(c); else{XRaiseWindow(dpy,c->frame);focus_win(c);} return; }
    }
    for (Win *c=wins;c&&c!=focused;c=c->next) {
        if (!c->closing) { if(c->minimized)restore(c); else{XRaiseWindow(dpy,c->frame);focus_win(c);} return; }
    }
}

/* =========================================================================
 * run — loop principal event-driven
 * ========================================================================= */
void run(void) {
    while (wm_running) {
        XEvent ev; XNextEvent(dpy,&ev); x_err=0;

        if (ev.type==MapRequest) {
            /* Todas as janelas passam por manage(), que:
             * 1. Filtra popups/menus/tooltips via skip_window() (baseado em
             *    _NET_WM_WINDOW_TYPE e override_redirect)
             * 2. Centraliza janelas transitórias (About dialogs) sobre o pai
             * 3. Força decoração (decorated = 1) para todas as janelas gerenciadas
             *
             * NOTA: Antes havia um special case que pulava manage() para janelas
             * com WM_TRANSIENT_FOR, o que fazia About dialogs e outros diálogos
             * modais perderem a decoração. */
            manage(ev.xmaprequest.window, 0);
            continue;
        }

        if (ev.type==MapNotify) {
            Win *iw=by_client(ev.xmap.window);
            if (iw&&!iw->closing) {
                XRaiseWindow(dpy,iw->frame);
                redraw_all(iw);
            }
            continue;
        }

        if (ev.type==ConfigureRequest) {
            Win *iw=by_client(ev.xconfigurerequest.window);
            if (iw&&!iw->closing&&!iw->maximized&&!iw->fullscreen) {
                if (ev.xconfigurerequest.value_mask&CWWidth)  iw->w=ev.xconfigurerequest.width+(iw->decorated?2:0);
                if (ev.xconfigurerequest.value_mask&CWHeight) iw->h=ev.xconfigurerequest.height+(iw->decorated?TITLE_HEIGHT+2:0);
                if (ev.xconfigurerequest.value_mask&CWX) iw->x=ev.xconfigurerequest.x;
                if (ev.xconfigurerequest.value_mask&CWY) iw->y=ev.xconfigurerequest.y;
                XMoveResizeWindow(dpy,iw->frame,iw->x,iw->y,iw->w,iw->h);
                position_client(iw);
                send_configure(iw);
                draw_frame(iw);
            } else if (!iw) {
                XWindowChanges wc={0};
                wc.x=ev.xconfigurerequest.x; wc.y=ev.xconfigurerequest.y;
                wc.width=ev.xconfigurerequest.width; wc.height=ev.xconfigurerequest.height;
                wc.border_width=ev.xconfigurerequest.border_width;
                wc.sibling=ev.xconfigurerequest.above; wc.stack_mode=ev.xconfigurerequest.detail;
                XConfigureWindow(dpy,ev.xconfigurerequest.window,ev.xconfigurerequest.value_mask,&wc);
            }
            continue;
        }

        if (ev.type==ButtonPress) {
            /* =============================================================
             * Se há um overlay ativo (resize armado), o clique pode ser:
             * 1. No overlay → inicia resize por quadrantes
             * 2. Fora do overlay → desarma
             *
             * NOTA: O XGrabButton(Button1, root, GrabModeSync) na
             * inicialização faz com que ButtonPress em qualquer janela
             * seja entregue com window=root. Precisamos verificar se o
             * ponteiro está sobre o overlay para distinguir.
             * ============================================================= */
            if (overlay_win != None) {
                if (ev.xbutton.window == overlay_win) {
                    /* Clique no overlay — inicia resize por quadrantes */
                    Win *iw = armed_win;
                    if (!iw) { destroy_resize_overlay(); continue; }

                    /* Detecta o quadrante baseado na posição do clique
                     * dentro do overlay */
                    Quadrant q = detect_quadrant(ev.xbutton.x, ev.xbutton.y,
                                                  iw->w, iw->h);

                    /* Graba o ponteiro para receber Motion/Release */
                    XGrabPointer(dpy, root, False,
                                 ButtonReleaseMask | Button1MotionMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, None, cur_br, CurrentTime);

                    int orig_x = iw->x, orig_y = iw->y;
                    int orig_w = iw->w, orig_h = iw->h;
                    int fx = iw->x, fy = iw->y, fw = iw->w, fh = iw->h;
                    int done = 0;
                    /* Ponto de ancoragem: onde o usuário clicou (root coords) */
                    int anchor_x = ev.xbutton.x_root;
                    int anchor_y = ev.xbutton.y_root;

                    while (!done) {
                        XEvent mev;
                        XNextEvent(dpy, &mev);
                        if (mev.type == MotionNotify && (mev.xmotion.state & Button1Mask)) {
                            while (XCheckMaskEvent(dpy, PointerMotionMask | Button1MotionMask, &mev));
                            int mx = mev.xmotion.x_root;
                            int my = mev.xmotion.y_root;

                            /* Calcula novo retângulo baseado no quadrante
                             * e delta a partir do ponto de ancoragem */
                            calc_quadrant_rect(q, orig_x, orig_y, orig_w, orig_h,
                                               anchor_x, anchor_y,
                                               mx, my, &fx, &fy, &fw, &fh);
                            move_resize_overlay(fx, fy, fw, fh);
                        } else if (mev.type == Expose) {
                            if (mev.xexpose.count == 0) {
                                /* Taskbar não precisa de tratamento especial:
                                 * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
                                Win *ew = by_frame(mev.xexpose.window);
                                if (ew && !ew->closing && !ew->minimized)
                                    draw_frame(ew);
                            }
                        } else if (mev.type == ButtonRelease) {
                            done = 1;
                        }
                    }
                    XUngrabPointer(dpy, CurrentTime);

                    /* Aplica a nova geometria na janela alvo */
                    iw->x = fx; iw->y = fy;
                    iw->w = fw; iw->h = fh;
                    XMoveResizeWindow(dpy, iw->frame, fx, fy, fw, fh);
                    position_client(iw);
                    XSync(dpy, False);
                    draw_frame(iw);
                    send_configure(iw);
                    draw_taskbar();
                    flush_taskbar_pixmap();
                    /* Destroi overlay e desarma */
                    destroy_resize_overlay();
                    armed_win = NULL;
                    continue;
                }
                /* Clique veio com window=root devido ao XGrabButton.
                 * Verifica se o ponteiro está sobre o overlay. */
                if (ev.xbutton.window == root && armed_win) {
                    int ox = armed_win->x, oy = armed_win->y;
                    int ow = armed_win->w, oh = armed_win->h;
                    if (ev.xbutton.x_root >= ox &&
                        ev.xbutton.x_root < ox + ow &&
                        ev.xbutton.y_root >= oy &&
                        ev.xbutton.y_root < oy + oh) {
                        /* Ponteiro está sobre o overlay — replay para
                         * que o evento chegue ao overlay corretamente. */
                        XAllowEvents(dpy, ReplayPointer, CurrentTime);
                        continue;
                    }
                }
                /* Clique fora do overlay — desarma */
                destroy_resize_overlay();
                armed_win = NULL;
                /* Continua para processamento normal do clique */
            }

            if (ev.xbutton.window==root) {
                /* Menu removido do botão direito — agora é acionado pelo botão X da taskbar */
                XAllowEvents(dpy,ReplayPointer,CurrentTime); continue;
            }
            if (ev.xbutton.window==taskbar) {
                if (ev.xbutton.button==Button1) {
                    if (ev.xbutton.x > 40) {
                        taskbar_click(ev.xbutton.x);   /* botões de janela no press */
                    }
                    /* x <= 40 (logo) — não faz nada no press, aguarda release */
                }
                continue;
            }
            Win *iw=by_frame(ev.xbutton.window);
            if (!iw) iw=by_client(ev.xbutton.window);
            if (!iw||iw->closing) { XAllowEvents(dpy,ReplayPointer,CurrentTime); continue; }

            XRaiseWindow(dpy,iw->frame);
            int changed=(focused!=iw);
            focus_win(iw);

            /* Clique no client window: repassa o evento ao app via
             * ReplayPointer. Se o usuário arrastar (MotionNotify + Button1),
             * a janela é movida. Se soltar sem arrastar, o app processou
             * o clique normalmente (seleção de texto, clique em botão, etc.). */
            if (ev.xbutton.window == iw->client) {
                XAllowEvents(dpy, ReplayPointer, CurrentTime);
                if (!iw->maximized && ev.xbutton.button == Button1) {
                    drag = 1;
                    drag_sx = ev.xbutton.x_root - iw->x;
                    drag_sy = ev.xbutton.y_root - iw->y;
                    drag_win = iw;
                    XGrabPointer(dpy, root, False,
                                 ButtonReleaseMask | Button1MotionMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, None, cur_move, CurrentTime);
                }
                if (changed) { draw_taskbar(); flush_taskbar_pixmap(); }
                continue;
            }

            if (ev.xbutton.window==iw->frame) {
                int lx=ev.xbutton.x, ly=ev.xbutton.y;
                int hit=hit_titlebar(iw,lx,ly);
                if (hit==HIT_CLOSE) {
                    iw->closing=1; send_delete(iw->client);
                } else if (hit==HIT_MIN) {
                    minimize(iw);
                } else if (hit==HIT_RESIZE) {
                    /* Arma a janela para resize por quadrantes:
                     * 1. Cria overlay window (opaca + bordas vermelhas +
                     *    cruz divisória) em cima da janela.
                     * 2. NÃO graba o ponteiro — o usuário pode mover o
                     *    mouse livremente.
                     * 3. O próximo clique no overlay inicia o resize. */
                    overlay_win = create_resize_overlay(iw);
                    armed_win = iw;
                    continue;
                } else if (hit==HIT_TITLE&&!iw->maximized&&ev.xbutton.button==Button1) {
                    /* Drag pela titlebar — usa inner loop com grab no root
                     * para consistência e para evitar glitches. */
                    int dsx = lx;
                    int dsy = ly;
                    XGrabPointer(dpy, root, False,
                                 ButtonReleaseMask | Button1MotionMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, None, cur_move, CurrentTime);
                    int done = 0;
                    while (!done) {
                        XEvent mev;
                        XNextEvent(dpy, &mev);
                        if (mev.type == MotionNotify && (mev.xmotion.state & Button1Mask)) {
                            while (XCheckMaskEvent(dpy, PointerMotionMask | Button1MotionMask, &mev));
                            int nx = mev.xmotion.x_root - dsx;
                            int ny = mev.xmotion.y_root - dsy;
                            if (ny < 0) ny = 0;
                            if (ny > SH - TASKBAR_H - TITLE_HEIGHT) ny = SH - TASKBAR_H - TITLE_HEIGHT;
                            XMoveWindow(dpy, iw->frame, nx, ny);
                            iw->x = nx; iw->y = ny;
                            position_client(iw);
                            /* Após mover, drena Expose events que o X11 gerou
                             * para janelas encobertas/descobertas e redesenha
                             * suas decorações. */
                            XEvent ev_expose;
                            while (XCheckTypedEvent(dpy, Expose, &ev_expose)) {
                                if (ev_expose.xexpose.count == 0) {
                                    /* Taskbar não precisa de tratamento especial:
                                     * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
                                    Win *ew = by_frame(ev_expose.xexpose.window);
                                    if (ew && !ew->closing && !ew->minimized)
                                        draw_frame(ew);
                                }
                            }
                        } else if (mev.type == Expose) {
                            /* Expose event de outras janelas que chegou
                             * antes do próximo MotionNotify — processa
                             * imediatamente para evitar acúmulo. */
                            if (mev.xexpose.count == 0) {
                                /* Taskbar não precisa de tratamento especial:
                                 * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
                                Win *ew = by_frame(mev.xexpose.window);
                                if (ew && !ew->closing && !ew->minimized)
                                    draw_frame(ew);
                            }
                        } else if (mev.type == ButtonRelease) {
                            send_configure(iw);
                            done = 1;
                        }
                    }
                    XUngrabPointer(dpy, CurrentTime);
                } else if (hit==HIT_TITLE&&ev.xbutton.button==Button2) {
                    toggle_max(iw);
                }
            }
            if (changed) { draw_taskbar(); flush_taskbar_pixmap(); }
            XAllowEvents(dpy,ReplayPointer,CurrentTime);
            continue;
        }

        if (ev.type==MotionNotify) {
            if (drag && drag_win && !drag_win->closing) {
                while (XCheckMaskEvent(dpy, PointerMotionMask | Button1MotionMask, &ev));
                int nx = ev.xmotion.x_root - drag_sx;
                int ny = ev.xmotion.y_root - drag_sy;
                if (ny < 0) ny = 0;
                if (ny > SH - TASKBAR_H - TITLE_HEIGHT) ny = SH - TASKBAR_H - TITLE_HEIGHT;
                XMoveWindow(dpy, drag_win->frame, nx, ny);
                drag_win->x = nx; drag_win->y = ny;
                position_client(drag_win);
                /* Processa Expose events gerados pelo X11 para janelas
                 * que foram encobertas/descobertas pelo movimento. */
                XEvent expose_ev;
                while (XCheckTypedEvent(dpy, Expose, &expose_ev)) {
                    if (expose_ev.xexpose.count == 0) {
                        /* Taskbar não precisa de tratamento especial:
                         * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
                        Win *ew = by_frame(expose_ev.xexpose.window);
                        if (ew && !ew->closing && !ew->minimized)
                            draw_frame(ew);
                    }
                }
            }
            continue;
        }

        if (ev.type==ButtonRelease) {
            /* ButtonRelease no logo da taskbar → abre menu flutuante */
            if (ev.xbutton.window == taskbar &&
                ev.xbutton.button == Button1 &&
                ev.xbutton.x <= 40) {
                taskbar_click(ev.xbutton.x);
            }
            if (drag) {
                XUngrabPointer(dpy, CurrentTime);
                if (drag_win) send_configure(drag_win);
                draw_taskbar();
                flush_taskbar_pixmap();
                drag = 0; drag_win = NULL;
            }
            continue;
        }

        if (ev.type==KeyPress) {
            KeySym ks=XLookupKeysym(&ev.xkey,0); int alt=ev.xkey.state&Mod1Mask;
            if (alt&&ks==XK_F4&&focused&&!focused->closing) { focused->closing=1; send_delete(focused->client); }
            else if (alt&&ks==XK_F2&&focused) toggle_max(focused);
            else if (alt&&ks==XK_Tab) alt_tab();
            else if (alt&&ks==XK_F9&&focused) minimize(focused);
            continue;
        }

        if (ev.type==DestroyNotify) {
            if (!by_frame(ev.xdestroywindow.window)) {
                Win *iw=by_client(ev.xdestroywindow.window);
                if (iw) unmanage(iw->client);
            }
            continue;
        }

        if (ev.type==UnmapNotify) {
            Win *iw=by_client(ev.xunmap.window);
            if (iw&&!iw->minimized) {
                if (ev.xunmap.send_event) {
                    /* Aplicativo pediu para desmapear (ex: withdraw) — remove do WM */
                    unmanage(iw->client);
                }
                /* send_event == False: unmap feito pelo WM (minimize) ou pelo X11
                 * (reparenting durante manage()). Em ambos os casos, o WM já
                 * gerenciou o estado ou está em processo de gerenciamento.
                 * Não fazer nada aqui evita que um UnmapNotify gerado por
                 * XReparentWindow durante manage() desmapeie o frame. */
            }
            continue;
        }

        if (ev.type==PropertyNotify) {
            if (ev.xproperty.atom==_NET_WM_NAME||ev.xproperty.atom==XA_WM_NAME) {
                Win *iw=by_client(ev.xproperty.window);
                if (iw&&!iw->closing) { get_title(iw->client,iw->title); if(!iw->minimized)draw_frame(iw); draw_taskbar(); flush_taskbar_pixmap(); }
            }
            continue;
        }

        if (ev.type==Expose&&ev.xexpose.count==0) {
            /* Taskbar não precisa de tratamento especial:
             * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
            Win *iw=by_frame(ev.xexpose.window); if(iw&&!iw->closing&&!iw->minimized)draw_frame(iw);
            continue;
        }

        if (ev.type==ConfigureNotify) {
            /* Mudança de resolução de tela (ex: xrandr) — o root window
             * recebe ConfigureNotify com o novo tamanho. Usamos os valores
             * do evento diretamente para evitar cache stale. */
            if (ev.xconfigure.window == root) {
                configure_screen(ev.xconfigure.width, ev.xconfigure.height);
                continue;
            }
            Win *iw=by_client(ev.xconfigure.window);
            if (iw&&!iw->closing&&!iw->maximized&&!iw->fullscreen&&iw->decorated) {
                /* Verifica se o client window foi movido/redimensionado
                 * externamente (ex: GTK/CSD movendo o client durante drag).
                 * Se a posição ou tamanho atual não corresponde ao esperado
                 * dentro do frame, corrige e redesenha a decoração. */
                int ex=CLIENT_X(iw), ey=CLIENT_Y(iw);
                int ew=CLIENT_W(iw), eh=CLIENT_H(iw);
                XWindowAttributes wa;
                if (XGetWindowAttributes(dpy,iw->client,&wa)) {
                    if (wa.x!=ex||wa.y!=ey||wa.width!=ew||wa.height!=eh) {
                        /* Tamanho mudou? Atualiza frame */
                        if (wa.width!=ew||wa.height!=eh) {
                            iw->w=wa.width+2;
                            iw->h=wa.height+TITLE_HEIGHT+2;
                            if (iw->w<MIN_WIN_W) iw->w=MIN_WIN_W;
                            if (iw->h<MIN_WIN_H+TITLE_HEIGHT) iw->h=MIN_WIN_H+TITLE_HEIGHT;
                            XMoveResizeWindow(dpy,iw->frame,iw->x,iw->y,iw->w,iw->h);
                        }
                        /* Reposiciona client dentro do frame */
                        position_client(iw);
                        /* Redesenha decoração e notifica client */
                        draw_frame(iw);
                        send_configure(iw);
                    }
                }
            }
            continue;
        }

        if (ev.type==ClientMessage) {
            Win *iw=by_client(ev.xclient.window);
            if (!iw||iw->closing) continue;
            if (ev.xclient.message_type==_NET_WM_STATE) {
                Atom act=(Atom)ev.xclient.data.l[0],p1=(Atom)ev.xclient.data.l[1],p2=(Atom)ev.xclient.data.l[2];
                int req_hidden=(p1==_NET_WM_STATE_HIDDEN||p2==_NET_WM_STATE_HIDDEN);
                if (req_hidden) {
                    int dh=(act==1)||(act==2&&!iw->minimized);
                    if (dh&&!iw->minimized) minimize(iw);
                    else if (!dh&&iw->minimized) restore(iw);
                } else {
                    int req_full=(p1==_NET_WM_STATE_FULLSCREEN||p2==_NET_WM_STATE_FULLSCREEN);
                    if (req_full) {
                        int df=(act==1)||(act==2&&!iw->fullscreen);
                        if (df!=iw->fullscreen) toggle_fullscreen(iw);
                    } else {
                        int req_max=(p1==_NET_WM_STATE_MAXIMIZED_VERT||p1==_NET_WM_STATE_MAXIMIZED_HORZ||p2==_NET_WM_STATE_MAXIMIZED_VERT||p2==_NET_WM_STATE_MAXIMIZED_HORZ);
                        if (req_max) { int dm=(act==1)||(act==2&&!iw->maximized); if(dm!=iw->maximized)toggle_max(iw); }
                    }
                }
            } else if (ev.xclient.message_type==_NET_ACTIVE_WINDOW) {
                if (iw->minimized) restore(iw);
                else { XRaiseWindow(dpy,iw->frame); focus_win(iw); }
            } else if (ev.xclient.message_type==_NET_WM_MOVERESIZE) {
                int dir = ev.xclient.data.l[2];
                if (dir == 8) { /* _NET_WM_MOVERESIZE_MOVE */
                    /* Inicia drag da janela a pedido do client (ex: VS Code
                     * com titlebar customizada). O client envia este message
                     * quando o usuário arrasta pela decoração dele.
                     *
                     * NOTA: O grab é feito no root (não no frame) para que o
                     * WM tenha prioridade sobre grabs internos do client (ex:
                     * GTK/CSD). Em X11, o grab mais recente tem prioridade,
                     * então o WM "rouba" os eventos de mouse do GTK,
                     * impedindo que o GTK processe MotionEvents e mova o
                     * client window independentemente. */
                    int msx = ev.xclient.data.l[0];
                    int msy = ev.xclient.data.l[1];
                    int m_drag_sx = msx - iw->x;
                    int m_drag_sy = msy - iw->y;
                    XRaiseWindow(dpy, iw->frame);
                    focus_win(iw);
                    /* Reposiciona o client dentro do frame ANTES do drag,
                     * garantindo que o estado inicial esteja correto antes
                     * de iniciar o loop de movimento. */
                    position_client(iw);
                    XSync(dpy, False);
                    XGrabPointer(dpy, root, False,
                                 ButtonReleaseMask | Button1MotionMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, None, cur_move, CurrentTime);
                    int done = 0;
                    while (!done) {
                        XEvent mev;
                        XNextEvent(dpy, &mev);
                        if (mev.type == MotionNotify && (mev.xmotion.state & Button1Mask)) {
                            while (XCheckMaskEvent(dpy, PointerMotionMask | Button1MotionMask, &mev));
                            int nx = mev.xmotion.x_root - m_drag_sx;
                            int ny = mev.xmotion.y_root - m_drag_sy;
                            if (ny < 0) ny = 0;
                            if (ny > SH - TASKBAR_H - TITLE_HEIGHT) ny = SH - TASKBAR_H - TITLE_HEIGHT;
                            XMoveWindow(dpy, iw->frame, nx, ny);
                            iw->x = nx; iw->y = ny;
                            /* Reposiciona o client dentro do frame para evitar
                             * que GTK/CSD desaloque o client durante o drag. */
                            position_client(iw);
                            /* Após mover, drena Expose events que o X11 gerou. */
                            XEvent ev_expose;
                            while (XCheckTypedEvent(dpy, Expose, &ev_expose)) {
                                if (ev_expose.xexpose.count == 0) {
                                    /* Taskbar não precisa de tratamento especial:
                                     * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
                                    Win *ew = by_frame(ev_expose.xexpose.window);
                                    if (ew && !ew->closing && !ew->minimized)
                                        draw_frame(ew);
                                }
                            }
                        } else if (mev.type == Expose) {
                            /* Expose de outras janelas — processa imediatamente. */
                            if (mev.xexpose.count == 0) {
                                /* Taskbar não precisa de tratamento especial:
                                 * XSetWindowBackgroundPixmap faz o X11 auto-preenchê-la. */
                                Win *ew = by_frame(mev.xexpose.window);
                                if (ew && !ew->closing && !ew->minimized)
                                    draw_frame(ew);
                            }
                        } else if (mev.type == ButtonRelease) {
                            send_configure(iw);
                            done = 1;
                        }
                    }
                    XUngrabPointer(dpy, CurrentTime);
                }
            }
            continue;
        }
    }
}

#ifndef CONFIG_H
#define CONFIG_H

/* =========================================================================
 * config.h — Definições estéticas e dimensionais
 *
 * Altere este arquivo para customizar a aparência do inferno_wm.
 * ========================================================================= */

/* =========================================================================
 * Dimensões da decoração
 * ========================================================================= */
#define TITLE_HEIGHT    24   /* altura da titlebar */
#define TASKBAR_H       47   /* altura total da taskbar */
#define TASKBAR_PAD     10   /* padding horizontal dos botões na taskbar */
#define BTN_W           21   /* largura dos botões da titlebar */
#define BTN_H           21   /* altura dos botões da titlebar */
#define BTN_MARGIN       2   /* margem entre botões e borda da titlebar */
#define BTN_GAP          0   /* gap entre botões (atualmente 0 = colados) */
#define RBW              3   /* largura da borda vermelha de resize */

/* =========================================================================
 * Tamanhos mínimos de janela
 * ========================================================================= */
#define MIN_WIN_W       80
#define MIN_WIN_H       60

/* =========================================================================
 * Fonte
 * ========================================================================= */
#define FONT_SIZE       14
#define FONT_PAD_LEFT   10

/* =========================================================================
 * Flags de comportamento
 * ========================================================================= */
#define TASKBAR_GAP_ENABLED 1   /* 1 = taskbar sobe 1px, pinta pixel abaixo c/ bg; 0 = antigo */

/* =========================================================================
 * Paleta de cores — todas em hexadecimal (0xRRGGBB)
 * ========================================================================= */
#define C_BG_HEX        0x444444   /* fundo do root window */
#define C_TITLE_ACT     0x0015BC   /* titlebar da janela ativa */
#define C_TITLE_INACT   0x9B9B9B   /* titlebar da janela inativa */
#define C_FRAME         0xD6D6D6   /* fundo do frame / taskbar */
#define C_BORDER_LT     0xFBFBFA   /* borda clara (topo + esquerda) */
#define C_BORDER_RB     0xA5AEAE   /* borda escura (baixo + direita) */

#endif /* CONFIG_H */

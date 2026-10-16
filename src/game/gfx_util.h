// Texto em bitmap, cores (HSL/tingimento) e montagem dos graficos de interface.
#pragma once

#include "compat/allegro.h"

namespace hoh {

// Indice do glifo numa tira de fonte: so maiusculas/digitos/pontuacao (fontes 18 e 10 px) ou
// maiusculas+minusculas (fonte 8 px, g_font_small).
int glyph_index_upper(char c);
int glyph_index_mixed(char c);
// Desenha `text` com glifos de largura fixa (18, 10 ou 8 px) tirados de `font_strip`.
void draw_text_w18(BITMAP *dst, BITMAP *font_strip, const char *text, int x, int y);
void draw_text_w10(BITMAP *dst, BITMAP *font_strip, const char *text, int x, int y);
void draw_text_w8(BITMAP *dst, BITMAP *font_strip, const char *text, int x, int y);

// Componente HSL -> 0..255 (m1 = 2l - m2; hue em voltas, 0..1). Trunca como _ftol.
int hsl_hue_to_rgb(double m1, double m2, double hue);
// h, l, s em 0..1 -> r, g, b em 0..255.
void hsl_to_rgb(double h, double l, double s, int *r, int *g, int *b);
// Retinge a regiao (src_x, src_y, w x h) de `src` em (dst_x, dst_y) de `dst`: cada pixel nao-mascara
// vira hsl_to_rgb(hue/255, luma(pixel) * lum/255 / 255, sat/255); com boost_pct > 0 o resultado e
// multiplicado por 1 + boost_pct/100 (saturado em 255). src_x == -1: bitmap inteiro (src_x = src_y =
// dst_y = 0, w/h = tamanho de src; dst_x e mantido). copy_mask (so o byte baixo conta): copia os pixels
// de mascara para dst. Pode ser chamada com src == dst.
void tint_bitmap(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct, int src_x,
                 int src_y, int dst_x, int dst_y, int w, int h, int copy_mask);
// Igual a tint_bitmap, mas grava o pixel com R e B trocados (usada quando g_convert_colors != 0).
void tint_bitmap_bgr(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct, int src_x,
                     int src_y, int dst_x, int dst_y, int w, int h, int copy_mask);
// dst = mascara onde src e mascara, preto (0) no resto (dst do mesmo tamanho de src).
void make_silhouette(BITMAP *src, BITMAP *dst);
// Novo bitmap com a versao cinza ((r+g+b)/4) de src; mascara preservada.
BITMAP *create_grayscale_bitmap(BITMAP *src);
// dst = src espelhado na horizontal (dst limpo para magenta antes).
void bitmap_make_h_flip(BITMAP *src, BITMAP *dst);

// Guarda o bitmap em g_bitmap_registry (liberado por free_game_resources).
void register_bitmap(BITMAP *bmp);
// g_font_w18_tinted[0..7] = g_font_w18 tingida com matizes 126, 114, ... (passo -12).
void build_tinted_fonts();
// 0x40e9c0: fontes, logo, copyright, ticker, icones do HUD e bitmaps de trabalho da interface.
void init_ui_graphics();

}  // namespace hoh

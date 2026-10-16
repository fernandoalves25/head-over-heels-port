// Efeito de agua do fundo dos menus e transicoes de tela.
//
// Os mapas de altura (g_water_buf_a / g_water_buf_b, int16[480][640]) e os ponteiros
// g_water_cur / g_water_prev / g_water_rendered (int16_t*) estao em game/globals.h.
#pragma once

#include "compat/allegro.h"

#include <cstdint>

namespace hoh {

void water_init();
void water_propagate(int16_t *cur, int16_t *prev);
void water_render_bgr(BITMAP *dst, int16_t *water);
void water_render(BITMAP *dst, int16_t *water);
void water_random_drop(int16_t *water);
void water_stamp_bitmap(BITMAP *shape, int16_t *water, int x, int y, int16_t value);
void water_update_title_shapes(int16_t *water);
void draw_water_background(BITMAP *dst);

// type: 0 = fade da sala, 1 = preto curto; -1 vira 0.
void start_screen_transition(int type);
// from_left: colunas inteiras de src a partir da esquerda (todos os chamadores passam true).
void transition_column_wipe(BITMAP *dst, int step, bool from_left, BITMAP *src);

}  // namespace hoh

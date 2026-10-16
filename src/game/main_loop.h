// Laco principal do jogo: inicializacao, passo fixo de logica e renderizacao.
#pragma once

#include "compat/allegro.h"

namespace hoh {

int game_main(int argc, char **argv);  // 0x0040f050
void game_update();                    // 0x0040c3c0
void game_render();                    // 0x0040e210
void timer_tick();                     // 0x00402150
void triple_buffer_init();             // 0x0040a570
void free_game_resources();            // 0x0040e6b0
void draw_fps_counter(BITMAP *dst);    // 0x0040e150
float int_div_float(int a, float b);   // 0x00402170
int sound_update_ambient();            // 0x0040c170

}  // namespace hoh

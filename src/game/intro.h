// Abertura ("Retrospec PRESENTS..."), tela-titulo e ticker de creditos.
//
// Estado 5 (g_game_state == 5). g_menu_id = fase: 0 cena com a sala 0xde e o campo de
// estrelas, 1 tela-titulo. g_intro_step = subfase: 0 fade-in (64 ticks), 1 cena / espera
// (0x30c ticks; PRESENTS... depois de 600), 2 fade-out (32 ticks).
#pragma once

#include "compat/allegro.h"

namespace hoh {

// Estrela da abertura (g_stars, 0x527458, 600 x 12 bytes, .bss). Projecao isometrica com
// rastro de 4 pixels em draw_intro_screen.
struct Star {
    int x;      // 0..999; anda `layer` por tick e recicla em 1000 com novo y
    int y;      // 0..999
    int layer;  // 1..4 (i/170 + 1): velocidade e brilho
};
extern Star g_stars[600];

void intro_start();                       // 0x004070f0
void intro_update_scene();                // 0x00407450 (o Ghidra dava int: eax era lixo)
void update_state_intro();                // 0x0040bff0
void draw_intro_screen(BITMAP *dst);      // 0x0040dd90
void intro_draw_floor_logo(BITMAP *dst);  // 0x0040dd10
void draw_credits_ticker(BITMAP *dst);    // 0x0040cc20

// As funcoes de sala/objeto usadas pela abertura vem dos modulos do motor
// (room_load.h, physics.h, room_render.h, controllers.h, gameplay.h).

}  // namespace hoh

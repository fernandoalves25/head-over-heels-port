// Funcoes de jogo chamadas pelo front-end mas ainda nao portadas (motor isometrico, sala,
// jogabilidade). As implementacoes provisorias estao em pending_stubs.cpp e avisam uma vez no
// stderr quando chamadas. Cada uma sera substituida pela versao portada.
#pragma once

#include "compat/allegro.h"

namespace hoh {

// --- estados do jogo (handlers da tabela de saltos 0x40c44c) ---
void update_state_game();        // 0x004074b0
void update_state_empire_map();  // 0x0040bcc0
void update_state_result();      // 0x0040bb20

// --- desenho ---
void render_game_frame(BITMAP *dst);  // 0x00406570
void hud_draw(BITMAP *dst);           // 0x00406150
void draw_empire_screen(BITMAP *dst); // 0x0040d8f0
void draw_pause_screen(BITMAP *dst);  // 0x0040dcd0
void draw_end_screen(BITMAP *dst);    // 0x0040d330

// --- ja portadas em render_util.cpp ---
void draw_sprite_lit(BITMAP *dst, BITMAP *spr, int x, int y, int light);  // 0x004158a0
int world_index3_from_named(int named_index);                             // 0x00406020

// --- aviso na tela de funcionalidade ainda nao portada (so do port) ---
extern int g_port_notice_ticks;
extern const char *g_port_notice;

// --- acesso a sala ativa (campos da classe Room; viram metodos quando a sala for portada) ---
int active_room_id();           // Room+0x448d8c
bool room_has_rain_active();    // Room+0x448da2
int room_rain_level_active();   // Room+0x448da4
int room_named_index_active();  // Room+0x448dc0

}  // namespace hoh

// Menus do titulo/pausa, opcoes, controles e Config.dat.
#pragma once

#include "compat/allegro.h"

namespace hoh {

void menu_open(int menu, int item);   // 0x00407020
void menu_return_to_main();           // 0x00407080
void menu_update_cursor();            // 0x00409ca0
void menu_update();                   // 0x0040a6a0; usa g_menu_id (o Ghidra perdeu o argumento)
void menu_compose_items();            // 0x0040cdb0
void draw_menu_screen(BITMAP *dst);   // 0x0040cf90

void options_text_sound_mode();       // 0x0040a0c0
void options_text_music_volume();     // 0x0040a0f0
void options_text_sound_volume();     // 0x0040a110
void options_apply_shadows();         // 0x0040a130
void options_apply_game_speed();      // 0x0040a190
void options_text_triple_buffer();    // 0x0040a1f0
void options_text_sensitivity();      // 0x0040a230
void options_text_gfx_mode();         // 0x0040a350

// port: item FRAME RATE no menu GAME OPTIONS (antes do BACK); chamar depois de set_gfx_mode.
void options_port_init();
void options_text_frame_rate();
void options_text_language();  // port: item LANGUAGE de GAME OPTIONS

void input_read_controls();           // 0x00407360
void controls_build_menu_text(bool full);  // 0x0040a260
bool input_any_pressed();             // 0x0040bf80

void config_load();                   // 0x0040a390
void config_save();                   // 0x0040a490
void scan_save_slots();               // 0x0040e8d0

// externas pendentes (jogo ainda nao portado; chamadas por menu_update)
void planet_screen_start(bool from_menu);  // 0x00406f10 mapa dos planetas -> estado 2
void save_game();                          // 0x00409e20 grava Save%i.dat
void free_character_rooms();               // 0x00405d70 destroi g_char_rooms[2]

}  // namespace hoh

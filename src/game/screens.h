// Modulo screens: HUD, quadro do jogo, saves, pausa, mapa do imperio (estado 2), tela de
// resultado/recorde (estado 4). Dono: screens.cpp. O envio do recorde (WinInet) e a area de
// transferencia (Win32) sao do original; o port pode reduzi-los a stubs (mesmos efeitos no estado).
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

namespace hoh {

// ------------------------------------------------------------------ HUD e quadro do jogo
void hud_draw_bar(BITMAP *dst, int x, int y, int value);                // 0x00405d90 barra de tempo (g_bar_image)
void hud_draw_lives(BITMAP *dst);                                       // 0x00405e00
// Paineis do mundo: desenhados em y = 320 - y_shift; named_index -1 = room->wall_named da sala ativa.
void hud_draw_world_panels(BITMAP *dst, int y_shift, int named_index);  // 0x00406080
void hud_draw(BITMAP *dst);                                             // 0x00406150
void render_game_frame(BITMAP *dst);                                    // 0x00406570 clear + Room_draw + hud_draw

// ------------------------------------------------------------------ saves
void load_game();                                                       // 0x004065c0 Save%i.dat (g_load_slot)
void save_game();                                                       // 0x00409e20

// ------------------------------------------------------------------ recorde
void clipboard_init(Clipboard *cb);                                     // 0x00406b00
void clipboard_set_text(Clipboard *cb);                                 // 0x00406b20 (Win32 OpenClipboard...)
// out recebe 12 caracteres base64; tag = 0x3f; count valores com nbytes[i] bytes cada.
void highscore_encode(char *out, int tag, int count, const int *values, const int *nbytes);  // 0x00406bd0
void gameover_screen_start();                                           // 0x00406d40 -> estado 4
void highscore_submit();                                                // 0x0040b7c0
void highscore_name_input();                                            // 0x0040b810
int highscore_net_open();                                               // 0x0040f4b0 0 ok, 1/2 erro (WinInet)
void highscore_net_close();                                             // 0x0040f500
void url_escape_plus(const char *src, char *dst);                       // 0x0040f530 '+' -> "%2B"
bool str_is_digits(const char *s);                                      // 0x0040f590
int highscore_query_position(const char *name, const char *code, int *position);  // 0x0040f5d0 0 ok, 3/4 erro

// ------------------------------------------------------------------ estados 2, 3 e 4
void planet_screen_start(bool from_menu);                               // 0x00406f10 -> estado 2
void pause_menu_open();                                                 // 0x004072f0 -> estado 3
void update_state_result();                                             // 0x0040bb20 game_update, estado 4
void update_state_empire_map();                                         // 0x0040bcc0 game_update, estado 2
void draw_end_screen(BITMAP *dst);                                      // 0x0040d330
void draw_empire_screen(BITMAP *dst);                                   // 0x0040d8f0
void draw_pause_screen(BITMAP *dst);                                    // 0x0040dcd0

// ------------------------------------------------------------------ globais do modulo (MODULE)
extern Clipboard g_clipboard;                                           // 0xa5a980
extern Clipboard *g_clipboard_ptr;                                      // 0xa5a978 (= &g_clipboard)

// ------------------------------------------------------------------ externas ja portadas (render_util.cpp)
int world_index3_from_named(int named_index);                           // 0x00406020 (tambem em pending.h)
// intro.cpp (identica a intro.h, que os modulos do motor nao incluem): usada por draw_empire_screen.
void draw_credits_ticker(BITMAP *dst);                                  // 0x0040cc20

}  // namespace hoh

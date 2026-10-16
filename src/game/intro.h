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

// ------------------------------------------------------------------ externas pendentes
// Sala/objetos do motor isometrico, ainda nao portados. NAO implementados aqui.
struct Room;    // sala carregada (Room_ctor 0x415f80, 0x448dd4 bytes); g_char_rooms[]
struct Object;  // objeto instanciado na sala (deriva de Sprite); g_char_obj[]

Room *load_room(int id);                                          // 0x00403c50
void *Room_scalar_dtor(Room *room, int flags);                    // 0x00415f60 (vtable[0]; flags&1 = delete)
void Room_update_controllers(Room *room);                         // 0x004174e0
void Room_update_physics(Room *room);                             // 0x00416600
void Room_animate(Room *room);                                    // 0x00415b50
void Room_set_active_player(Room *room, Object *player);          // 0x00417510
void Room_draw_objects(Room *room, BITMAP *dst, bool shadows_on, BITMAP *shadow_buffer,
                       bool shadows_complex);                     // 0x004130a0
void Room_draw_glows(Room *room, BITMAP *dst);                    // 0x00415ef0
void Sprite_set_anim_frame(Object *obj, int anim, int frame);     // 0x004100f0
void Sprite_draw(Object *obj, BITMAP *dst, int origin_x, int origin_y);  // 0x0040fe50
void reset_idle_animation();                                      // 0x00405470

// Campos que o original le direto por offset; o port da sala deve fornecer estes acessos
// (ou eles viram acesso a membro quando Room/Object existirem).
int &room_origin_x(Room *room);         // Room+0x404: origem x de tela (a intro rola a sala)
int &room_origin_y(Room *room);         // Room+0x408: origem y de tela
int room_id_of(Room *room);             // Room+0x448d8c: id na grade 40x40
Object *room_object(Room *room, int i); // Room+0x004+4*i: objects[i] (0 = Head, 1 = Heels)
int object_pos_x(Object *obj);          // Object+0x010
int object_pos_y(Object *obj);          // Object+0x014 (altura)
int object_pos_z(Object *obj);          // Object+0x018
int object_anim(Object *obj);           // Object+0x44c: animacao/direcao

}  // namespace hoh

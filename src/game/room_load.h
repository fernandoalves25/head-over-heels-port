// Modulo room_load: montagem da sala a partir do registro (load_room), construcao/destruicao de
// Room/RoomBase, piso, paredes, recortes, luzes fixas e portas. Dono: room_load.cpp.
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

#include <cstdint>

namespace hoh {

// ------------------------------------------------------------------ Door
void *Door_deleting_dtor(Door *self, int flags);                        // 0x00401260 vtable[0]
void Door_dtor(Door *self);                                             // 0x00401280
// Cria os objetos de sala da porta (pedestal + 2 batentes + verga). side 0/1 escolhe as pecas
// P/Q 0..2 ou 3..5 do World; mirrored usa as copias espelhadas.
void Door_build_objects(Door *self, int x, int pillar_blocks, int z, int side, int mirrored);  // 0x00401290
Door *Door_ctor_vertical(Door *self, Room *room, int type, int ceiling);                     // 0x004014a0 tipos 4/5
// p, q = posicao no registro (celulas); pillar_blocks = altura do pedestal em blocos.
Door *Door_ctor(Door *self, Room *room, World *world, int type, int p, int pillar_blocks, int q);  // 0x004014d0 tipos 0..3
void Door_destroy(Door *self);  // port: (***vtable)(1) = Door_deleting_dtor(self, 1)

// ------------------------------------------------------------------ leitura do registro da sala
// Cursor em g_room_cursor sobre g_room_blob (globals.h; no port g_room_blob aponta para
// g_data->room_blob). O Ghidra perdeu o retorno de room_read_int/room_read_bool.
int room_peek_int(int offset);                                          // 0x00402de0
bool room_peek_bool(int offset);                                        // 0x00402df0
int room_read_int();                                                    // 0x00402e00
bool room_read_bool();                                                  // 0x00402e20
void room_init_scroll(Room *room);                                      // 0x00403c00 g_view_ext_x/z e room->scroll_x/z
Room *load_room(int id);                                                // 0x00403c50

// ------------------------------------------------------------------ RoomBase / Room
RoomBase *RoomBase_ctor(RoomBase *self);                                // 0x00412e30 (init com 320, 470)
void *RoomBase_scalar_dtor(RoomBase *self, int flags);                  // 0x00412e50
void RoomBase_init(RoomBase *self, int origin_x, int origin_y);         // 0x00412e70
void RoomBase_dtor(RoomBase *self);                                     // 0x00412f80 destroi objects[] (Object_destroy)
void *Room_scalar_dtor(Room *self, int flags);                          // 0x00415f60 vtable[0]
Room *Room_ctor(Room *self, int origin_x, int origin_y);                // 0x00415f80
void Room_dtor(Room *self);                                             // 0x00416050 destroi doors[] e chama RoomBase_dtor
void Room_destroy(Room *self);  // port: (***vtable)(1) = Room_scalar_dtor(self, 1)
void Room_set_border(Room *self, int border);                           // 0x00415f50

// ------------------------------------------------------------------ piso e paredes
// fx, fz / pos, hpos em celulas do registro (contadas a partir do canto do fundo); phase = quadro
// inicial da animacao.
void Room_place_floor_pattern(Room *self, int fx, int fz, FloorSet *set, int phase);  // 0x00414150
void Room_build_floor(Room *self, int w, int d, FloorSet *set);         // 0x004141f0 tambem fixa tile/width_px/depth_px
void Room_clear_floor_rect(Room *self, int fx, int fz, int w, int d);   // 0x004143d0 buraco
void Room_clear_wall0_rect(Room *self, int pos, int hpos, int len, int h);  // 0x00414460 janela/abertura
void Room_clear_wall1_rect(Room *self, int pos, int hpos, int len, int h);  // 0x004144e0
void Room_place_wall0_pattern(Room *self, int pos, int hpos, Named *named, int phase);  // 0x00414560
void Room_place_wall1_pattern(Room *self, int pos, int hpos, Named *named, int phase);  // 0x00414670
void Room_build_walls(Room *self, Named *named, int base_h);            // 0x00414780 (load_room passa base_h = 0)
// Recortes da planta (flags7 do registro, em celulas): canto A = f0 x f4, canto B = f1 x f5,
// entalhe de x0 = f2 ate W - f3 com profundidade f6.
void Room_apply_shape_cuts(Room *self, int cut_a_w, int cut_a_d, int cut_b_w, int cut_b_d,
                           int notch_x0, int notch_x1, int notch_d);    // 0x00414d70

// ------------------------------------------------------------------ luzes e portas
int Room_add_light(Room *self, int x, int y, int z, bool flicker, int radius);  // 0x00415810 devolve o indice; liga room->dark
// Luz presa ao objeto, no centro da caixa + (dx, dy, dz).
void Room_attach_light(Room *self, Object *obj, bool flicker, int radius, int dx, int dy, int dz);  // 0x00416d00
void Room_update_attached_lights(Room *self);                           // 0x00416d80
Door *Room_add_door(Room *self, World *world, int type, int p, int pillar_blocks, int q);  // 0x00417520
Door *Room_add_vertical_exit(Room *self, int type, int ceiling);        // 0x004175b0

// ------------------------------------------------------------------ globais do modulo
// 0x431f88 (MODULE): indice MOBILE-Sound -> amostra do Sound.dat. No exe a tabela comeca no
// ultimo int de g_sample_category ([0] = 1, nunca usado); [1] = 16 (Heli), [2] = 35.
// Definir em room_load.cpp com {1, 16, 35}.
extern int32_t g_sound_index_map[3];

}  // namespace hoh

// Modulo engine_data: classes de dados graficos do motor (Block, Behaviour, FloorSet = "Mobile",
// Named, World) e auxiliares do carregador. Dono: engine_data.cpp. Outros modulos so incluem.
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

namespace hoh {

// ------------------------------------------------------------------ World (pecas de porta)
World *World_ctor(World *self);                                         // 0x00401000
void *World_deleting_dtor(World *self, int flags);                      // 0x00401010 vtable[0]; flags&1 = delete
void World_dtor(World *self);                                           // 0x00401030 destroi as 24 pecas (Block_destroy)
// depth = 1o int do registro; *_img/*_shadow = 6 pecas P (portal) e Q (pedestal), imagem e sombra.
void World_init_door_blocks(World *self, int depth, BITMAP *const door_img[6],
                            BITMAP *const pillar_img[6], BITMAP *const door_shadow[6],
                            BITMAP *const pillar_shadow[6]);            // 0x00401070
void World_destroy(World *self);  // port: (***vtable)(1) = World_deleting_dtor(self, 1)

// ------------------------------------------------------------------ FloorSet (o "Mobile": piso)
FloorSet *Mobile_ctor(FloorSet *self);                                  // 0x00401910
void *Mobile_deleting_dtor(FloorSet *self, int flags);                  // 0x00401920
void Mobile_dtor(FloorSet *self);                                       // 0x00401940
void Mobile_build_static(FloorSet *self);                               // 0x004019b0
void Mobile_build_animated(FloorSet *self, int nframes);                // 0x00401a20
void Mobile_create_tile_mask(FloorSet *self);                           // 0x00401ab0
void bitmap_apply_mask(BITMAP *bmp, BITMAP *mask);                      // 0x00401bd0 pixel de bmp vira magenta onde mask e magenta
void Mobile_cut_tiles(FloorSet *self, int frame, bool flip);            // 0x00401c40
void Mobile_copy_left_face(FloorSet *self, BITMAP *tile, int tile_index, int frame);   // 0x00401da0
void Mobile_copy_right_face(FloorSet *self, BITMAP *tile, int tile_index, int frame);  // 0x00401e70
void Mobile_create_tile_blocks(FloorSet *self);                         // 0x00401f50
void Mobile_set_animated(FloorSet *self, int speed);                    // 0x00402020
void Mobile_add_frame(FloorSet *self, int frame);                       // 0x004020a0
void Mobile_destroy(FloorSet *self);  // port: (***vtable)(1) = Mobile_deleting_dtor(self, 1)

// ------------------------------------------------------------------ carregador (load_game_data)
void read_string(char *dst, PACKFILE *f);                               // 0x00402ce0 int len + bytes
void read_behaviour_table(Behaviour *dst, PACKFILE *f);                 // 0x00402d10
void map_attribute_names();                                             // 0x00402e40 g_attr_names -> g_attr_map
void fix_bitmap_colors(BITMAP *bmp);                                    // 0x00402eb0 (so com g_convert_colors)

// ------------------------------------------------------------------ Block
Block *Block_ctor(Block *self);                                         // 0x0040f700
void *Block_deleting_dtor(Block *self, int flags);                      // 0x0040f730
void Block_dtor(Block *self);                                           // 0x0040f750
Block *Block_ctor_mirrored(Block *self, const Block *src);              // 0x0040f830 copia espelhada (g_blocks_alt)
Block *Block_ctor_from_bitmap(Block *self, BITMAP *image, int shadow_mode, int size_x);  // 0x0040fa30
Block *Block_ctor_from_bitmap_auto(Block *self, BITMAP *image, int shadow_mode);         // 0x0040fa80
void Block_calc_size_auto(Block *self);                                 // 0x0040fad0 size_x = sprite_w/2
void Block_calc_size(Block *self, int size_x);                          // 0x0040faf0
void Block_init_from_bitmap(Block *self, int shadow_mode);              // 0x0040fb10
void Block_make_animated(Block *self, int speed);                       // 0x0040fb40
int Block_add_anim(Block *self, int speed);                             // 0x0040fb70 devolve o indice da anim
void Block_set_frame(Block *self, int anim, int frame, BITMAP *image, BITMAP *shadow);  // 0x0040fba0
void Block_append_frame(Block *self, int anim, BITMAP *image, BITMAP *shadow);          // 0x0040fbd0
void Block_append_frame_from_block(Block *self, int anim, const Block *src);            // 0x0040fc00
void Block_destroy(Block *self);  // port: (***vtable)(1) = Block_deleting_dtor(self, 1)

// ------------------------------------------------------------------ Behaviour
Behaviour *Behaviour_ctor(Behaviour *self);                             // 0x00412df0
void *Behaviour_scalar_dtor(Behaviour *self, int flags);                // 0x00412e00
void Behaviour_dtor(Behaviour *self);                                   // 0x00412e20
void Behaviour_destroy(Behaviour *self);  // port: (***vtable)(1) = Behaviour_scalar_dtor(self, 1)

// ------------------------------------------------------------------ Named (paredes)
Named *Named_ctor(Named *self);                                         // 0x00417a80
void *Named_scalar_dtor(Named *self, int flags);                        // 0x00417a90
void Named_dtor(Named *self);                                           // 0x00417ab0
void Named_build(Named *self);                                          // 0x00417ba0
void Named_build_animated(Named *self, int nframes);                    // 0x00417c10
void Named_create_cell_mask(Named *self);                               // 0x00417ce0
void Named_slice_frame(Named *self, int frame);                         // 0x00417db0
void Named_create_cell_blocks(Named *self);                             // 0x00417f10
void Named_set_animated(Named *self, int speed);                        // 0x00418030
void Named_add_frame(Named *self, int frame);                           // 0x004180c0
void Named_destroy(Named *self);  // port: (***vtable)(1) = Named_scalar_dtor(self, 1)

// ------------------------------------------------------------------ port
// O load_game_data original (0x402f30) lia o HoHOriginal.dat E construia os objetos acima; no
// port data_bridge.cpp (ja portado) so le o GameData (g_data) e preenche contagens/g_images.
// engine_build_game_objects() faz a parte de construcao, na ordem e com a logica do original:
// map_attribute_names; por bloco Block_ctor + frames/hold/ping-pong (Block_append_frame) +
// Block_calc_size[_auto] + Behaviour (g_block_behaviours) + Block_ctor_mirrored (g_blocks_alt);
// por piso Mobile_ctor + Mobile_build_static/animated + Mobile_add_frame (g_mobiles); idem Named
// (g_named); por mundo World_ctor + World_init_door_blocks (g_worlds). Os BITMAPs lidos pelo
// GameData pertencem a ele (nao destruir); os criados pelo motor pertencem aos objetos.
// Aplica fix_bitmap_colors a cada bitmap do GameData (o original o fazia ao ler; data_bridge nao).
// Tambem aponta g_room_blob para g_data->room_blob e g_room_table para g_data->room_table (a
// mesma tabela de 1600 offsets do fim do blob, ja decodificada e alinhada). Chamar depois de
// load_game_data() (data_bridge.h). Devolve false se faltar dado.
bool engine_build_game_objects();
void engine_free_game_objects();  // destroi o que engine_build_game_objects criou (antes de delete g_data)

}  // namespace hoh

// Modulo physics: objetos (Sprite/Object: construcao, posicao, animacao), lista de objetos da
// sala, colisao, movimento com empurroes, gravidade, esteiras, sons de objeto. Dono: physics.cpp.
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

namespace hoh {

// ------------------------------------------------------------------ posicoes salvas (carregar/soltar)
void room_save_object_positions();                                      // 0x00405a10 sala ativa: obj->saved_* = x,y,z
void room_restore_object_positions();                                   // 0x00405a60

// ------------------------------------------------------------------ Sprite
Sprite *Sprite_ctor(Sprite *self);                                      // 0x0040fc30
void *Sprite_deleting_dtor(Sprite *self, int flags);                    // 0x0040fc60 vtable[0] da base
void Sprite_dtor(Sprite *self);                                         // 0x0040fc80
void Sprite_init(Sprite *self, Block *block, int x, int y, int z, int index);  // 0x0040fcb0
void Sprite_set_block(Sprite *self, Block *block);                      // 0x0040fd50 troca o grafico (ex.: Head over Heels)
void Sprite_set_position(Sprite *self, int x, int y, int z);            // 0x0040ffd0
void Sprite_play_anim(Sprite *self, int anim, int frame, int speed);    // 0x0040fff0 speed -1 = block->anim_speed[anim]
void Sprite_tick_anim(Sprite *self);                                    // 0x00410060
void Sprite_stop_anim(Sprite *self);                                    // 0x004100e0
void Sprite_set_anim_frame(Sprite *self, int anim, int frame);          // 0x004100f0 mostra o quadro sem mudar `animating`

// ------------------------------------------------------------------ Object
void *Object_deleting_dtor(Object *self, int flags);                    // 0x00410120 vtable[0]
Object *Object_ctor(Object *self, Block *block, int x, int y, int z, bool pushable, bool sticky,
                    bool gravity, int index);                           // 0x00410140
void Object_dtor(Object *self);                                         // 0x004102b0
void Object_set_conveyor(Object *self, int dx, int dz);                 // 0x004102f0
void Object_set_door_side(Object *self, int side);                      // 0x00410310
void Object_destroy(Object *self);  // port: (***vtable)(1) = Object_deleting_dtor(self, 1)

// ------------------------------------------------------------------ Room: lista de objetos e colisao
void Room_append_object(Room *self, Object *obj);                       // 0x00412ff0
void Room_remove_object(Room *self, Object *obj);                       // 0x00413aa0
void Room_remove_object_at(Room *self, int index);                      // 0x00413ac0 tambem destroi o objeto
bool Room_is_box_free(Room *self, const Block *box, int x, int y, int z);  // 0x00413d40 caixa de `box` em (x,y,z) nao cruza nenhum objeto
// true = livre; senao *hit = objeto atingido (o proprio obj e ignorado).
bool Room_find_collision(Room *self, Object *obj, int x, int y, int z, Object **hit);  // 0x00413e10
bool Room_try_place_object(Room *self, Object *obj, int x, int y, int z);  // 0x00413f10 posiciona se couber
Object *Room_create_object(Room *self, Block *block, int x, int y, int z, bool pushable,
                           bool sticky, bool gravity);                  // 0x00416ac0 new Object + append
void Room_destroy_object(Room *self, Object *obj);                      // 0x00417700 desliga controladores/luzes/brilhos dele e remove
bool Room_has_rider(Room *self, Object *obj);                           // 0x00417890 alguem tem standing_on == obj
bool Room_has_weight_on(Room *self, Object *obj);                       // 0x004169f0 objeto com gravidade em cima (tolerancia 1)
bool object_stands_on(const Object *top, const Object *base);           // 0x00416910
bool object_stands_on_tol(const Object *top, const Object *base);       // 0x00416980 aceita top->y - 1

// ------------------------------------------------------------------ Room: movimento e fisica
// Devolve -1 livre, -2 bateu na borda/recorte da sala, >= 0 indice do objeto atingido; *moved =
// objetos arrastados/empurrados (moved_count - 1). Desliza o jogador ativo nos batentes (door_side).
int Room_move_object(Room *self, Object *obj, int dx, int dy, int dz, bool push, int *moved);  // 0x004160d0
int Room_move_object_at(Room *self, int index, int dx, int dy, int dz, bool push);  // 0x00416290
void Room_update_physics(Room *self);                                   // 0x00416600 gravidade, standing_on, esteiras, saida pelo chao
void Room_update_conveyor(Room *self, Object *conveyor);                // 0x004167a0
void Room_carry_riders(Room *self, int index, int dx, int dz);          // 0x00416a50 move os sticky em cima de objects[index]
void Room_animate(Room *self);                                          // 0x00415b50 anima objetos, piso e paredes

// ------------------------------------------------------------------ sons de objeto
void Room_update_object_sounds(Room *self, int tick);                   // 0x004178d0 grava sound_tick; sons continuos
void voice_play_if_idle(const SAMPLE *spl, int *voice, int volume);     // 0x00417940 volume 0..15 (x17)
// `volume` nao e lido (usa self->sfx_volume); toca se category >= self->sound_mode.
void Room_play_sample(Room *self, const SAMPLE *spl, int volume, int category);  // 0x004179e0
void Room_play_sample_throttled(Room *self, const SAMPLE *spl, int *last_tick);  // 0x00417a20 no maximo 1 a cada 3 ticks

}  // namespace hoh

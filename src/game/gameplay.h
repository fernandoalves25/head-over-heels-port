// Modulo gameplay: estado 1 (jogo) — personagens, troca de sala, carregar objeto, luz dos
// personagens, jogo novo. Dono: gameplay.cpp.
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

namespace hoh {

void update_character_lights();                                         // 0x00403a70 raio das luzes 0/1 conforme g_light_timer
int other_character();                                                  // 0x00404e40 g_cur_char == 0 (usado como indice 0/1)
void park_character(int ch);                                            // 0x00404e50 g_char_obj[ch] -> (0x1000, 0x1000, 0x1000)
int opposite_exit_type(int type);                                       // 0x00404e80 0<->3, 1<->2, 4<->5, outro -1
// dest_door = indice entre as portas de tipo oposto a exit_type na sala ativa (exit_type -1 =
// teleporte: usa x, z); dy = altura relativa a base da porta; anim = direcao a mostrar.
bool place_character_at_entry(int dest_door, int exit_type, int dy, int x, int z, int anim);  // 0x00404ed0
void set_carried_object_suspended(bool suspend);                        // 0x004052e0 desliga/religa luzes e controladores de g_carried_obj
void reset_idle_animation();                                            // 0x00405470
// door_index em g_char_rooms[g_active_room_slot]->doors; -1 = teleporte (destino em teleporter).
void change_room(int door_index, Controller *teleporter);               // 0x00405490
void dir_to_delta(int dir, int *dx, int *dz);                           // 0x004059c0
void new_game();                                                        // 0x00405b00 salas 79 (Head) e 39 (Heels)
void free_character_rooms();                                            // 0x00405d70 Room_destroy de g_char_rooms[0..1]
void update_state_game();                                               // 0x004074b0 game_update, estado 1
void Room_set_active_player(Room *self, Object *player);                // 0x00417510
bool Room_on_free_spring(Room *self, Object *obj);                      // 0x00417630 obj sobre mola sem mais ninguem em cima

}  // namespace hoh

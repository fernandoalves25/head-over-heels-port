// Modulo controllers: comportamentos ativos (Controller: construtores por tipo, Controller_update,
// particulas de clima) e os Room_add_ctrl_* que os criam. Dono: controllers.cpp.
//
// Parametros pN = N-esimo dos 10 parametros do atributo (Behaviour::params); o mapeamento de cada
// load_room -> Room_add_ctrl_* -> campos esta em D_structs.md §4a.
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

namespace hoh {

// ------------------------------------------------------------------ clima (particulas)
void room_spawn_snow(Room *room);                                       // 0x00404c20 g_blocks[7], tipo 0x10
void room_spawn_rain(Room *room);                                       // 0x00404d10 g_blocks[8], tipo 0x11
void room_spawn_bubbles(Room *room);                                    // 0x00404de0 g_blocks[11], tipo 0x14
void room_spawn_weather(Room *room);                                    // 0x004052a0 neve, chuva ou bolhas

// ------------------------------------------------------------------ Controller
void *Controller_deleting_dtor(Controller *self, int flags);            // 0x00410320 vtable[0]
void Controller_dtor(Controller *self);                                 // 0x00410340
void Controller_destroy(Controller *self);  // port: (***vtable)(1) = Controller_deleting_dtor(self, 1) (o original nunca chama)
// Tipos 4 (mola), 8 (rosquinha), 0x10/0x11/0x14 (particulas); demais so gravam type/room/obj.
Controller *Controller_ctor_basic(Controller *self, Room *room, Object *obj, int type);  // 0x00410350
// Tipo 0xf: lights/glows = liga/desliga as luzes/brilhos do objeto; start_off -> state = 0.
Controller *Controller_ctor_flicker(Controller *self, Room *room, Object *obj, int type, bool lights,
                                    bool glows, int on_ticks, int off_ticks, bool start_off);  // 0x00410450
// 10 Switch: a = canal, b = ligado no inicio. 0xe Hoover: a = anim de morte, b = morre no chao
// mortal. 0x13 Glider: a = speed, b = push, c -> anim (o original guarda um bool como modo).
Controller *Controller_ctor_switch_hoover_glider(Controller *self, Room *room, Object *obj, int type,
                                                 int a, bool b, bool c);  // 0x004104c0
// Tipo 9: fixed_dir -1 = livre, senao so aceita empurrao nesse sentido.
Controller *Controller_ctor_joystick(Controller *self, Room *room, Object *obj, int type, int channel,
                                     int fixed_dir, int speed);         // 0x00410580
// 7 Extra: a = tipo de bonus, b = quem pode pegar. 0xd Dissapear: a = anim, b = quem dispara.
Controller *Controller_ctor_extra_disappear(Controller *self, Room *room, Object *obj, int type, int a,
                                            int b);                     // 0x00410620
Controller *Controller_ctor_teleporter(Controller *self, Room *room, Object *obj, int type,
                                       int dest_col, int dest_row, int dest_x, int dest_y,
                                       int dest_z, int anim);           // 0x00410680
// 3 Random: flag = diagonal, c = intervalo, d = modo de anim. 0xc Hit and Turn: flag = sentido
// do giro, c = modo de anim, d = direcao inicial. 0x12 Slider: c = alcance, d = modo de anim.
Controller *Controller_ctor_random_hitturn_slider(Controller *self, Room *room, Object *obj, int type,
                                                  int speed, bool flag, int c, int d,
                                                  bool push);           // 0x004106f0
Controller *Controller_ctor_pusher(Controller *self, Room *room, Object *obj, int type, int speed,
                                   bool vanish_on_hit, int vanish_anim, bool anim_while_moving,
                                   int move_anim);                      // 0x00410830
Controller *Controller_ctor_follower(Controller *self, Room *room, Object *obj, int type, int speed,
                                     bool diagonal, int anim_mode, bool push, bool fears_light,
                                     int range, bool axis_mode);        // 0x00410890
Controller *Controller_ctor_follower_extra(Controller *self, Room *room, Object *obj, int type,
                                           int speed, bool diagonal, int anim_mode, bool push,
                                           bool fears_light, int range, bool axis_mode,
                                           int block_offset, int walk_dir);  // 0x00410900
// vanish = 0 -> vx = -1 (nao some).
Controller *Controller_ctor_trap(Controller *self, Room *room, Object *obj, int type, bool falls,
                                 int vanish_anim, bool vanish, bool any_contact,
                                 bool only_while_weighted);             // 0x00410980
// axis 0/1/2 = x/y/z; max/min ja descontados da rampa por load_room.
Controller *Controller_ctor_mover(Controller *self, Room *room, Object *obj, int type, int axis,
                                  int speed, int ramp, int max, int min, int pause,
                                  bool start_positive, int anim_mode, bool push,
                                  bool reverse_on_bump);                // 0x004109e0
void Controller_reset(Controller *self);                                // 0x00410ab0
void Controller_anim_by_velocity(Controller *self);                     // 0x00410b00
void Controller_anim_by_mover_dir(Controller *self);                    // 0x00410bc0
void Controller_set_lights(Controller *self, bool on);                  // 0x00410c80 luzes (vx > 0) e brilhos (vy > 0) do objeto
void Controller_update(Controller *self);                               // 0x00410d00
int Controller_respawn_particle(Controller *self, bool initial);        // 0x00412cf0 sempre 0

// ------------------------------------------------------------------ Room: criacao de controladores
// Todos fazem new Controller + ctor e acrescentam em self->controllers.
void Room_add_ctrl_teleporter(Room *self, Object *obj, int type, int dest_col, int dest_row,
                              int dest_x, int dest_y, int dest_z, int anim);  // 0x00416de0
// Tipos 3, 0xc, 0x12 (ver Controller_ctor_random_hitturn_slider).
void Room_add_ctrl_wanderer(Room *self, Object *obj, int type, int speed, bool flag, int c, int d,
                            bool push);                                 // 0x00416e80
void Room_add_ctrl_flicker(Room *self, Object *obj, int type, bool lights, bool glows, int on_ticks,
                           int off_ticks, bool start_off);              // 0x00416f10
void Room_add_ctrl_trap(Room *self, Object *obj, int type, bool falls, int vanish_anim, bool vanish,
                        bool any_contact, bool only_while_weighted);    // 0x00416fa0
void Room_add_ctrl_mover(Room *self, Object *obj, int type, int axis, int speed, int ramp, int max,
                         int min, int pause, bool start_positive, int anim_mode, bool push,
                         bool reverse_on_bump);                         // 0x00417030
Controller *Room_add_ctrl_simple(Room *self, Object *obj, int type);    // 0x004170e0 Controller_ctor_basic; devolve o criado
// Tipos 10, 0xe, 0x13 (ver Controller_ctor_switch_hoover_glider).
void Room_add_ctrl_switch(Room *self, Object *obj, int type, int a, bool b, bool c);  // 0x00417160
void Room_add_ctrl_joystick(Room *self, Object *obj, int type, int channel, int fixed_dir,
                            int speed);                                 // 0x004171f0
// Tipos 7, 0xd (ver Controller_ctor_extra_disappear).
void Room_add_ctrl_extra(Room *self, Object *obj, int type, int a, int b);  // 0x00417280
void Room_add_ctrl_pusher(Room *self, Object *obj, int type, int speed, bool vanish_on_hit,
                          int vanish_anim, bool anim_while_moving, int move_anim);  // 0x00417300
void Room_add_ctrl_follower(Room *self, Object *obj, int type, int speed, bool diagonal,
                            int anim_mode, bool push, bool fears_light, int range,
                            bool axis_mode);                            // 0x00417390
void Room_add_ctrl_follower_extra(Room *self, Object *obj, int type, int speed, bool diagonal,
                                  int anim_mode, bool push, bool fears_light, int range,
                                  bool axis_mode, int block_offset, int walk_dir);  // 0x00417430
void Room_update_controllers(Room *self);                               // 0x004174e0 Controller_update em todos

}  // namespace hoh

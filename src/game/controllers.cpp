// Modulo controllers: comportamentos ativos (Controller: construtores por tipo, Controller_update,
// particulas de clima) e os Room_add_ctrl_* que os criam.
//
// Fidelidade: Controller_update (0x410d00) foi reestruturado a partir do pseudo-C (re/decomp/game.c)
// conferido instrucao a instrucao com a desmontagem do HoH.exe (os gotos do Ghidra para as caudas
// compartilhadas 0x412aa8 / 0x412ae2 / 0x4128f4 / 0x41296e viraram chamadas aos auxiliares abaixo).
// Mantidos: a ordem das chamadas a rand() (alteram a sequencia aleatoria do jogo), os retornos
// antecipados e em quais caminhos obj->contact e (ou nao e) zerado.
//
// Os campos +0x10..+0x48 do Controller mudam de papel conforme o tipo; o papel de cada um esta
// comentado no inicio de cada case (nomes de engine_types.h, offset original entre parenteses).
#include "game/controllers.h"

#include "game/globals.h"
#include "game/physics.h"

namespace hoh {

namespace {

// abs() como o MSVC gera (cdq; xor; sub), sem UB para INT_MIN.
int abs_cdq(int v) {
    const unsigned sign = v < 0 ? 0xffffffffu : 0u;
    return static_cast<int>((static_cast<unsigned>(v) ^ sign) - sign);
}

// Duracao em ticks da animacao `a` do bloco (anim_speed * frame_count; +0xf8 / +0x30 do Block).
int anim_ticks(const Block *b, int a) { return b->anim_speed[a] * b->frame_count[a]; }

// --- expansoes inline do original (mesmo teste em todos os pontos: categoria >= sound_mode) ---

// Som de movimento continuo: voice_play_if_idle(snd_move, &voice_move, sfx_volume) (tail 0x412c27).
void snd_move_loop(Room *room, Object *o) {
    if (o->snd_move != nullptr && room->sound_mode <= o->snd_move_cat)
        voice_play_if_idle(o->snd_move, &o->voice_move, room->sfx_volume);
}

// Som de batida numa voz exclusiva: voice_play_if_idle(snd_bump, &voice_bump, sfx_volume).
void snd_bump_loop(Room *room, Object *o) {
    if (o->snd_bump != nullptr && room->sound_mode <= o->snd_bump_cat)
        voice_play_if_idle(o->snd_bump, &o->voice_bump, room->sfx_volume);
}

// Som de batida limitado a 1 a cada 3 ticks (tail 0x412aa8).
void snd_bump_throttled(Room *room, Object *o) {
    if (o->snd_bump != nullptr && room->sound_mode <= o->snd_bump_cat)
        Room_play_sample_throttled(room, o->snd_bump, &o->bump_last_tick);
}

// Som de evento (pegar, sumir, morrer, congelar); Room_play_sample filtra a categoria.
void snd_event(Room *room, Object *o) {
    if (o->snd_event != nullptr)
        Room_play_sample(room, o->snd_event, room->sfx_volume, o->snd_event_cat);
}

// Tail dos Room_add_ctrl_*: controllers[n_controllers++] = c (sem checar o limite de 256,
// como no original).
void room_push_controller(Room *room, Controller *c) {
    room->controllers[room->n_controllers] = c;
    room->n_controllers++;
}

}  // namespace

// ================================================================== clima (particulas)

// 0x00404c20 room_spawn_snow
void room_spawn_snow(Room *room) {
    const int area = (room->width_px * room->depth_px) / 600;
    if (room->weather_amount == 0) room->weather_amount = 100;
    // original: fild amount; fmul 0.01 (double 0x430270); fild area; fmulp; _ftol (trunca)
    int n = static_cast<int>(static_cast<double>(room->weather_amount) * 0.01 *
                             static_cast<double>(area));
    if (n > 100) n = 100;
    for (; n > 0; n--) {
        Object *obj = Room_create_object(room, g_blocks[7], 0, 0, 0, true, false, false);
        Room_add_ctrl_simple(room, obj, CTRL_SNOW);
    }
}

// 0x00404d10 room_spawn_rain
void room_spawn_rain(Room *room) {
    const int area = (room->width_px * room->depth_px) / 500;
    if (room->weather_amount == 0) room->weather_amount = 100;
    int n = static_cast<int>(static_cast<double>(room->weather_amount) * 0.01 *
                             static_cast<double>(area));
    if (n > 100) n = 100;
    for (; n > 0; n--) {
        Object *obj = Room_create_object(room, g_blocks[8], 0, 0, 0, true, false, false);
        Room_add_ctrl_simple(room, obj, CTRL_RAIN);
    }
}

// 0x00404de0 room_spawn_bubbles
void room_spawn_bubbles(Room *room) {
    int n = (room->width_px * room->depth_px) / 1500;
    if (n > 100) n = 100;
    for (; n > 0; n--) {
        Object *obj = Room_create_object(room, g_blocks[11], 0, 0, 0, true, false, false);
        Room_add_ctrl_simple(room, obj, CTRL_BUBBLE);
    }
}

// 0x004052a0 room_spawn_weather
void room_spawn_weather(Room *room) {
    if (room->snowing) {
        room_spawn_snow(room);
        return;
    }
    if (room->raining) {
        room_spawn_rain(room);
        return;
    }
    if (room->underwater) room_spawn_bubbles(room);
}

// ================================================================== Controller: construcao

// 0x00410320 Controller_deleting_dtor
void *Controller_deleting_dtor(Controller *self, int flags) {
    Controller_dtor(self);
    if (flags & 1) delete self;
    return self;
}

// 0x00410340 Controller_dtor
void Controller_dtor(Controller *self) { self->vtbl = g_vtbl_Controller; }

// port: (***vtable)(1)
void Controller_destroy(Controller *self) { Controller_deleting_dtor(self, 1); }

// 0x00410350 Controller_ctor_basic
Controller *Controller_ctor_basic(Controller *self, Room *room, Object *obj, int type) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->type = type;
    self->obj = obj;
    self->room = room;
    switch (type) {
    case CTRL_SPRING:
        self->state = false;  // nao comprimida
        return self;
    case CTRL_PROJECTILE:
        self->disabled = true;  // rosquinha: desligada ate ser disparada
        return self;
    case CTRL_SNOW: {
        obj->particle = true;
        Object *o = self->obj;
        Sprite_set_anim_frame(o, 0, rand() % o->block->frame_count[0]);
        Controller_respawn_particle(self, true);
        return self;
    }
    case CTRL_RAIN:
        obj->particle = true;
        Sprite_set_anim_frame(self->obj, 0, rand() % 4);
        Controller_respawn_particle(self, true);
        return self;
    case CTRL_BUBBLE:
        obj->particle = true;
        Sprite_set_anim_frame(self->obj, 0, rand() % 5);
        Controller_respawn_particle(self, true);
        return self;
    default:
        return self;
    }
}

// 0x00410450 Controller_ctor_flicker
Controller *Controller_ctor_flicker(Controller *self, Room *room, Object *obj, int type, bool lights,
                                    bool glows, int on_ticks, int off_ticks, bool start_off) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_FLICKER) {
        if (lights) self->vx = 1;  // vx (+0x10): liga/desliga as luzes do objeto
        if (glows) self->vy = 1;   // vy (+0x14): liga/desliga os brilhos do objeto
        self->min_x = off_ticks;   // min_x (+0x40): ticks apagado
        self->state = !start_off;  // state (+0x20): aceso
        self->max_x = on_ticks;    // max_x (+0x34): ticks aceso
        self->p28 = on_ticks;      // p28 (+0x28): contador aceso
        self->p2c = off_ticks;     // p2c (+0x2c): contador apagado
    }
    return self;
}

// 0x004104c0 Controller_ctor_switch_hoover_glider
Controller *Controller_ctor_switch_hoover_glider(Controller *self, Room *room, Object *obj, int type,
                                                 int a, bool b, bool c) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_SWITCH) {
        self->p28 = a;  // canal
        self->state = b;  // ligado
        self->p2c = 0;  // espera entre toques
        if (!b) Sprite_set_anim_frame(obj, 0, 1);
    } else if (type == CTRL_HOOVER) {
        self->state = b;  // morre no chao mortal
        self->anim = a;   // anim de morte
        self->p28 = rand() % 10 + 3;  // ticks ate o proximo passo aleatorio
        self->max_y = self->obj->y;   // max_y (+0x38): altura de flutuacao
        return self;
    } else if (type == CTRL_GLIDER) {
        self->vy = a;  // speed
        self->vx = 0;
        self->vz = 0;
        self->p28 = -1;  // direcao atual (-1 = parado)
        self->push = b;
        // original: `param_6 & 0xff` (load_room passa p2 == 1 num registrador de lixo alto)
        self->anim = c ? 1 : 0;
        return self;
    }
    return self;
}

// 0x00410580 Controller_ctor_joystick
Controller *Controller_ctor_joystick(Controller *self, Room *room, Object *obj, int type, int channel,
                                     int fixed_dir, int speed) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_JOYSTICK) {
        self->p28 = channel;    // canal (load_room liga obj->joystick_target)
        self->p2c = speed;      // speed
        self->p30 = fixed_dir;  // direcao fixa (-1 livre)
        self->min_y = 1;        // min_y (+0x44) = 1 (nunca lido)
        if (fixed_dir == -1) {
            self->state = false;
        } else {
            self->state = true;  // modo fixo: velocidade pre-calculada
            self->vx = 0;
            self->vz = 0;
            switch (fixed_dir) {
            case 0:
                speed = -speed;
                // fallthrough (original)
            case 2:
                self->vx = speed;
                return self;
            case 1:
                speed = -speed;
                // fallthrough (original)
            case 3:
                self->vz = speed;
                return self;
            }
        }
    }
    return self;
}

// 0x00410620 Controller_ctor_extra_disappear
Controller *Controller_ctor_extra_disappear(Controller *self, Room *room, Object *obj, int type, int a,
                                            int b) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_EXTRA) {
        self->state = true;  // disponivel
        self->p30 = a;       // p30 (+0x30): tipo de bonus
        self->vx = b;        // vx (+0x10): quem pode pegar (0 qualquer, 1 Head, 2 Heels)
    } else if (type == CTRL_DISAPPEAR) {
        self->p28 = b;        // p28 (+0x28): quem dispara (0 Head/juntos, 1 Heels)
        self->state = false;  // disparado
        self->anim = a;       // anim de sumir
        return self;
    }
    return self;
}

// 0x00410680 Controller_ctor_teleporter
Controller *Controller_ctor_teleporter(Controller *self, Room *room, Object *obj, int type,
                                       int dest_col, int dest_row, int dest_x, int dest_y,
                                       int dest_z, int anim) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_TELEPORTER) {
        self->p28 = dest_col;  // sala destino = p28 + p2c*40 (lido por update_state_game)
        self->p2c = dest_row;
        self->vx = dest_x;     // vx/vy/vz (+0x10/14/18): posicao de chegada
        self->vy = dest_y;
        self->vz = dest_z;
        self->anim = anim;
        self->state = false;   // ocupado
        self->flag2 = false;
        obj->teleporter = true;
    }
    return self;
}

// 0x004106f0 Controller_ctor_random_hitturn_slider
Controller *Controller_ctor_random_hitturn_slider(Controller *self, Room *room, Object *obj, int type,
                                                  int speed, bool flag, int c, int d, bool push) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_RANDOM) {
        self->vy = speed;   // vy (+0x14): speed
        self->state = flag;  // diagonal
        self->anim = d;
        self->p2c = c;      // p2c (+0x2c): intervalo entre sorteios
        self->p28 = c;      // p28 (+0x28): ticks desde o ultimo sorteio
        self->push = push;
    } else if (type == CTRL_HIT_AND_TURN) {
        self->push = push;
        self->vy = speed;
        self->state = flag;  // sentido do giro
        self->anim = c;
        self->p28 = d;       // p28 (+0x28): direcao atual
        if (c == 1) Sprite_set_anim_frame(obj, 0, d);
        if (self->anim == 2) Sprite_play_anim(self->obj, self->p28, 0, -1);
        switch (self->p28) {
        case 0:
            self->vz = 0;
            self->vx = -self->vy;
            return self;
        case 1:
            self->vx = 0;
            self->vz = -self->vy;
            return self;
        case 2:
            self->vz = 0;
            self->vx = self->vy;
            return self;
        case 3:
            self->vx = 0;
            self->vz = self->vy;
            return self;
        }
    } else if (type == CTRL_SLIDER) {
        self->vy = speed;
        self->p2c = c;  // alcance (0 = qualquer)
        self->push = push;
        self->anim = d;
        return self;
    }
    return self;
}

// 0x00410830 Controller_ctor_pusher
Controller *Controller_ctor_pusher(Controller *self, Room *room, Object *obj, int type, int speed,
                                   bool vanish_on_hit, int vanish_anim, bool anim_while_moving,
                                   int move_anim) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_PUSHER) {
        self->p28 = speed;            // p28 (+0x28): speed
        self->flag2 = vanish_on_hit;  // flag2 (+0x21): some ao bater
        self->anim = vanish_anim;
        self->state = false;          // deslizando
        self->flag3 = anim_while_moving;
        self->max_x = move_anim;      // max_x (+0x34): anim de movimento
    }
    return self;
}

// 0x00410890 Controller_ctor_follower
Controller *Controller_ctor_follower(Controller *self, Room *room, Object *obj, int type, int speed,
                                     bool diagonal, int anim_mode, bool push, bool fears_light,
                                     int range, bool axis_mode) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_FOLLOWER) {
        self->vy = speed;
        self->anim = anim_mode;
        self->state = diagonal;
        self->push = push;
        self->p2c = range;
        self->flag2 = fears_light;
        self->flag3 = axis_mode;
        self->vz = -1;
        Controller_anim_by_velocity(self);
    }
    return self;
}

// 0x00410900 Controller_ctor_follower_extra
Controller *Controller_ctor_follower_extra(Controller *self, Room *room, Object *obj, int type,
                                           int speed, bool diagonal, int anim_mode, bool push,
                                           bool fears_light, int range, bool axis_mode,
                                           int block_offset, int walk_dir) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_FOLLOWER_EXTRA) {
        self->vy = speed;
        self->state = diagonal;
        self->anim = anim_mode;
        self->push = push;
        self->flag2 = fears_light;
        self->p2c = range;
        self->flag3 = axis_mode;
        self->vz = -1;
        self->vx = block_offset;  // vx (+0x10): soma ao block->id do novo grafico
        self->p30 = walk_dir;     // p30 (+0x30): direcao fixa ate cair
    }
    return self;
}

// 0x00410980 Controller_ctor_trap
Controller *Controller_ctor_trap(Controller *self, Room *room, Object *obj, int type, bool falls,
                                 int vanish_anim, bool vanish, bool any_contact,
                                 bool only_while_weighted) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_TRAP) {
        self->push = only_while_weighted;  // push (+0x51): so cai com peso em cima
        if (only_while_weighted) self->state = true;  // ja armada
        self->flag2 = falls;      // flag2 (+0x21): caindo
        self->flag3 = any_contact;
        self->p28 = vanish_anim;  // p28 (+0x28): anim de sumir
        if (!vanish) self->vx = -1;  // vx (+0x10) == -1: nao some
    }
    return self;
}

// 0x004109e0 Controller_ctor_mover
Controller *Controller_ctor_mover(Controller *self, Room *room, Object *obj, int type, int axis,
                                  int speed, int ramp, int max, int min, int pause,
                                  bool start_positive, int anim_mode, bool push,
                                  bool reverse_on_bump) {
    self->vtbl = g_vtbl_Controller;
    Controller_reset(self);
    self->obj = obj;
    self->type = type;
    self->room = room;
    if (type == CTRL_MOVER) {
        self->push = push;
        self->flag3 = reverse_on_bump;
        self->ramp_max = ramp;
        self->p28 = ramp;  // espera ate o proximo passo
        self->p2c = ramp;  // espera atual
        self->state = start_positive;
        self->flag2 = true;  // acelerando
        self->p24 = pause;
        self->p30 = 0;  // pausa restante
        self->anim = anim_mode;
        if (axis == 0) {
            self->vx = speed;
            self->max_x = max;
            self->min_x = min;
        } else if (axis == 1) {
            self->vy = speed;
            self->max_y = max;
            self->min_y = min;
        } else if (axis == 2) {
            self->vz = speed;
            self->max_z = max;
            self->min_z = min;
        }
        if (anim_mode > 0) Controller_anim_by_mover_dir(self);
    }
    return self;
}

// 0x00410ab0 Controller_reset
void Controller_reset(Controller *self) {
    // original: nao zera room (+0x08) nem vtbl
    self->disabled = false;
    self->type = 0;
    self->ramp_max = 0;
    self->vx = 0;
    self->vy = 0;
    self->vz = 0;
    self->max_z = 0;
    self->max_y = 0;
    self->max_x = 0;
    self->min_z = 0;
    self->min_y = 0;
    self->min_x = 0;
    self->p28 = 0;
    self->p2c = 0;
    self->p30 = 0;
    self->obj = nullptr;
    self->state = false;
    self->flag2 = false;
    self->p24 = 0;
    self->anim = 0;
    self->push = false;
    self->flag3 = false;
}

// 0x00410b00 Controller_anim_by_velocity
void Controller_anim_by_velocity(Controller *self) {
    if (self->vx > 0) {
        if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 2);
        if (self->anim == 2) Sprite_play_anim(self->obj, 2, 0, -1);
    }
    if (self->vx < 0) {
        if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 0);
        if (self->anim == 2) Sprite_play_anim(self->obj, 0, 0, -1);
    }
    if (self->vz > 0) {
        if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 3);
        if (self->anim == 2) Sprite_play_anim(self->obj, 3, 0, -1);
    }
    if (self->vz < 0) {
        if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 1);
        if (self->anim == 2) Sprite_play_anim(self->obj, 1, 0, -1);
    }
}

// 0x00410bc0 Controller_anim_by_mover_dir
// Mover: as velocidades sao sempre >= 0; o sentido e state. O eixo y nao muda a anim.
void Controller_anim_by_mover_dir(Controller *self) {
    if (self->vx > 0) {
        if (!self->state) {
            if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 0);
            if (self->anim == 2) Sprite_play_anim(self->obj, 0, 0, -1);
        } else {
            if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 2);
            if (self->anim == 2) Sprite_play_anim(self->obj, 2, 0, -1);
        }
    }
    if (self->vz > 0) {
        if (!self->state) {
            if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 1);
            if (self->anim == 2) Sprite_play_anim(self->obj, 1, 0, -1);
        } else {
            if (self->anim == 1) Sprite_set_anim_frame(self->obj, 0, 3);
            if (self->anim == 2) Sprite_play_anim(self->obj, 3, 0, -1);
        }
    }
}

// 0x00410c80 Controller_set_lights
void Controller_set_lights(Controller *self, bool on) {
    if (self->vx > 0) {  // Flicker: liga luzes
        Room *room = self->room;
        const int n = room->n_lights;
        for (int i = 0; i < n; i++)
            if (room->light_obj[i] == self->obj) room->light_on[i] = on;
    }
    if (self->vy > 0) {  // Flicker: liga brilhos
        Room *room = self->room;
        const int n = room->n_glows;
        for (int i = 0; i < n; i++)
            if (room->glows[i]->obj == self->obj) room->glow_on[i] = on;
    }
}

// ================================================================== Controller_update

// 0x00410d00 Controller_update
void Controller_update(Controller *self) {
    if (self->disabled) return;
    Object *obj = self->obj;
    if (!obj->active) {
        obj->contact = nullptr;
        return;
    }
    if (obj->freezable && obj->freeze_timer > 0) {
        obj->freeze_timer--;
        obj = self->obj;
        if (obj->freeze_timer != 0) return;
        if (!obj->freeze_stops_anim) return;
        Sprite_play_anim(obj, obj->anim, 0, -1);
        return;
    }

    Room *room = self->room;
    // local_c: "moved" de Room_move_object. port: zerado (o original so o le depois de o escrever,
    // exceto no Mover com todas as velocidades <= 0, onde lia lixo da pilha).
    int moved = 0;

    switch (self->type) {
    case CTRL_MOVER: {
        // vx/vy/vz = speed no eixo (>= 0); state = sentido +; flag2 = acelerando; ramp_max = passos
        // de rampa; p2c = espera atual entre passos; p28 = contagem ate o proximo passo; p24 =
        // pausa nas pontas; p30 = pausa restante; max_*/min_* = limites; flag3 = inverte ao bater.
        if (self->p30 > 0) {
            self->p30--;
            return;
        }
        if (self->p28 > 0) {
            self->p28--;
            return;
        }
        snd_move_loop(room, obj);
        bool bumped = false;
        if (self->vx > 0) {
            int hit;
            if (self->state) {
                hit = Room_move_object(room, self->obj, self->vx, 0, 0, self->push, &moved);
                if (self->obj->x >= self->max_x) self->flag2 = false;
            } else {
                hit = Room_move_object(room, self->obj, -self->vx, 0, 0, self->push, &moved);
                if (self->obj->x <= self->min_x) self->flag2 = false;
            }
            if (hit != -1) bumped = true;
        }
        if (!bumped && self->vy > 0) {
            int hit;
            if (self->state) {
                hit = Room_move_object(room, self->obj, 0, self->vy, 0, self->push, &moved);
                if (self->obj->y >= self->max_y) self->flag2 = false;
            } else {
                hit = Room_move_object(room, self->obj, 0, -self->vy, 0, self->push, &moved);
                if (self->obj->y <= self->min_y) self->flag2 = false;
            }
            if (hit != -1) bumped = true;
        }
        if (!bumped && self->vz > 0) {
            int hit;
            if (self->state) {
                hit = Room_move_object(room, self->obj, 0, 0, self->vz, self->push, &moved);
                if (self->obj->z >= self->max_z) self->flag2 = false;
            } else {
                hit = Room_move_object(room, self->obj, 0, 0, -self->vz, self->push, &moved);
                if (self->obj->z <= self->min_z) self->flag2 = false;
            }
            if (hit != -1) bumped = true;
        }
        if (bumped) {  // 0x410e49 / 0x410f41
            if (self->flag3) {
                self->state = !self->state;
                if (self->anim > 0) Controller_anim_by_mover_dir(self);
                snd_bump_loop(room, self->obj);
            }
            self->obj->contact = nullptr;
            return;
        }
        // 0x410fa3: livre. Empurrado por alguem (sem ser de cima) enquanto arrastava: inverte.
        if (self->flag3 && self->obj->contact != nullptr && moved > 0 &&
            self->obj->contact_dy == 0) {
            self->state = !self->state;
            if (self->anim > 0) Controller_anim_by_mover_dir(self);
            snd_bump_loop(room, self->obj);
        }
        self->obj->contact = nullptr;
        if (!self->flag2) {  // desacelerando: espera cresce ate ramp_max, entao inverte e pausa
            if (self->p2c < self->ramp_max) {
                self->p2c++;
                self->p28 = self->p2c;
                return;
            }
            self->flag2 = true;
            self->state = !self->state;
            self->p30 = self->p24;
            if (self->anim > 0) Controller_anim_by_mover_dir(self);
        } else if (self->p2c > 0) {  // acelerando: espera diminui
            self->p2c--;
            self->p28 = self->p2c;
            return;
        }
        self->p28 = self->p2c;
        return;
    }

    case CTRL_TRAP: {
        // state = disparada; flag2 = caindo; push = so cai com peso em cima; flag3 = qualquer
        // contato dispara; vx == -1 = nao some; p28 = anim de sumir; p2c = ticks da anim (-1 fim);
        // p30 = atraso (nunca preenchido).
        if (self->p2c == -1) {
            obj->contact = nullptr;
            return;
        }
        if (!self->state && obj->contact != nullptr && (obj->contact_dy < 0 || self->flag3))
            self->state = true;
        if (self->state) {
            if (self->p30 > 0) {
                self->p30--;
                obj->contact = nullptr;
                return;
            }
            if (self->flag2) {
                if (!self->push || Room_has_weight_on(room, obj)) {
                    const int hit = Room_move_object(room, self->obj, 0, -2, 0, false, &moved);
                    if (hit == -1) {
                        snd_move_loop(room, self->obj);
                    } else {
                        self->flag2 = false;
                        snd_bump_throttled(room, self->obj);
                    }
                }
                self->obj->contact = nullptr;
                if (self->flag2) {
                    self->obj->contact = nullptr;
                    return;
                }
            }
            if (self->vx == -1) {  // nao some: para no chao
                Object *o = self->obj;
                if (o->y != 0 && o->y != 1) {
                    self->flag2 = true;
                    o->contact = nullptr;
                    return;
                }
                self->p2c = -1;
                snd_bump_throttled(room, o);
                self->obj->contact = nullptr;
                return;
            }
            if (self->p2c == 0) {
                Sprite_play_anim(self->obj, self->p28, 0, -1);
                snd_event(room, self->obj);
            }
            if (self->p2c >= anim_ticks(self->obj->block, self->p28) - 1) {
                self->p2c = -1;
                Room_destroy_object(room, self->obj);
                self->disabled = true;
                return;
            }
            self->p2c++;
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_RANDOM: {
        // vy = speed; vx/vz = velocidade atual; state = diagonal; p28 = ticks desde o sorteio;
        // p2c = intervalo.
        int moved_z = 0;  // local_8
        moved = 0;
        int hit = Room_move_object(room, obj, self->vx, 0, 0, self->push, &moved);
        const int hit_z = Room_move_object(room, self->obj, 0, 0, self->vz, self->push, &moved_z);
        if (hit_z != -1) hit = hit_z;
        if (moved_z != 0) moved = moved_z;
        if (hit == -1 && (moved == 0 || self->obj->contact_dy < 0) && self->p28 != self->p2c) {
            self->p28++;
            snd_move_loop(room, self->obj);  // original: sem zerar contact
            return;
        }
        if (self->p28 != self->p2c) snd_bump_loop(room, self->obj);
        self->vz = 0;
        self->vx = 0;
        do {  // original: speed 0 = laco infinito
            if (self->vz != 0) break;
            self->vx = (rand() % 3 - 1) * self->vy;
            self->vz = (rand() % 3 - 1) * self->vy;
        } while (self->vx == 0);
        self->obj->contact = nullptr;
        if (self->vx != 0 && self->vz != 0) {
            if (!self->state) {
                if (rand() % 2 == 0)
                    self->vx = 0;
                else
                    self->vz = 0;
            } else if (self->vy > 1) {
                self->vx /= 2;
                self->vz /= 2;
            }
        }
        if (self->anim > 0) Controller_anim_by_velocity(self);
        self->p28 = 0;
        return;
    }

    case CTRL_SPRING: {
        // state = comprimida (algo pressionando); flag2 = rebote tocando; p2c = ticks do rebote.
        if (!self->state) {
            if (self->flag2) {
                const Block *b = obj->block;
                if (self->p2c < b->anim_speed[0] * b->frame_count[0] - 1) {
                    self->p2c++;
                } else {
                    self->flag2 = false;
                    Sprite_stop_anim(obj);
                    Sprite_set_anim_frame(self->obj, 0, 0);
                }
            }
            if (self->obj->contact != nullptr && self->obj->contact_dy < 0) {
                self->state = true;
                Sprite_stop_anim(self->obj);
                Sprite_set_anim_frame(self->obj, 0, 1);
                self->obj->contact = nullptr;
                return;
            }
        } else if (obj->contact == nullptr) {
            self->state = false;
            self->flag2 = true;
            self->p2c = 0;
            Sprite_play_anim(obj, 0, 1, -1);
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_TELEPORTER: {
        // state = ocupado (ha objeto em cima). A troca de sala e feita em update_state_game.
        if (!self->state) {
            if (Room_has_rider(room, obj)) {
                self->state = true;
                Sprite_play_anim(self->obj, self->anim, 0, -1);
                self->obj->contact = nullptr;
                return;
            }
        } else {
            if (!Room_has_rider(room, obj)) {
                self->state = false;
                Sprite_stop_anim(self->obj);
                Sprite_set_anim_frame(self->obj, 0, 0);
                self->obj->contact = nullptr;
                return;
            }
            snd_move_loop(room, self->obj);  // zumbido
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_PUSHER: {
        // p28 = speed; state = deslizando; flag2 = some ao bater; flag3 = anima ao mover; max_x =
        // anim de movimento; anim = anim de sumir; p2c = -1 sumindo / -2 fim; p30 = ticks sumindo.
        if (!self->state && obj->contact != nullptr &&
            (obj->contact_dx != 0 || obj->contact_dz != 0)) {
            self->state = true;
            self->vx = 0;
            self->vz = 0;
            if (obj->contact_dx < 0) self->vx = -self->p28;
            if (obj->contact_dx > 0) self->vx = self->p28;
            if (obj->contact_dz < 0) self->vz = -self->p28;
            if (obj->contact_dz > 0) self->vz = self->p28;
            if (self->vx != 0 && self->vz != 0) {
                self->vx /= 2;
                self->vz /= 2;
            }
            if (self->flag3) Sprite_play_anim(obj, self->max_x, 0, -1);
        }
        self->obj->contact = nullptr;
        if (!self->state) return;
        if (self->p2c == -2) return;
        if (self->p2c == -1) {
            if (self->p30 >= anim_ticks(self->obj->block, self->anim) - 2) {
                self->p2c = -2;
                Room_destroy_object(room, self->obj);
                self->disabled = true;
                return;
            }
            self->p30++;
            return;
        }
        const int hit = Room_move_object(room, self->obj, self->vx, 0, self->vz, false, &moved);
        if (hit != -1) {
            if (self->flag3) Sprite_stop_anim(self->obj);
            if (self->flag2) {
                self->p2c = -1;
                Sprite_play_anim(self->obj, self->anim, 0, -1);
                self->p30 = 0;
                snd_event(room, self->obj);
                return;
            }
            self->state = false;
            snd_bump_throttled(room, self->obj);
            return;
        }
        snd_move_loop(room, self->obj);
        return;
    }

    case CTRL_EXTRA: {
        // state = disponivel; vx = quem pode pegar (0 qualquer, 1 Head ou juntos, 2 Heels ou
        // juntos); p30 = tipo de bonus; p2c = -1 tocando a anim 1 / -2 fim; p28 = ticks da anim.
        if (self->state) {
            Object *c = obj->contact;
            if (c == room->active_player && !c->touched_killer) {
                const int who = self->vx;
                const int id = room->active_player->block->id;
                if (who == 0 || (who == 1 && (id == 0 || id == 2)) ||
                    (who == 2 && (id == 1 || id == 2))) {
                    self->state = false;
                    room->active_player->bonus_pending = self->p30;
                    room->active_player->bonus_record_index = self->obj->record_index;
                    self->p28 = 0;
                    self->p2c = -1;
                    Sprite_play_anim(self->obj, 1, 0, -1);
                    self->obj->pickable = false;
                    snd_event(room, self->obj);
                }
            }
            self->obj->contact = nullptr;
            return;
        }
        if (self->p2c == -1) {
            if (self->p28 >= anim_ticks(obj->block, 1) - 2) {
                self->p2c = -2;
                Room_destroy_object(room, obj);
                self->disabled = true;
                return;
            }
            self->p28++;
            return;
        }
        return;  // original: sem zerar contact
    }

    case CTRL_PROJECTILE: {
        // Rosquinha disparada: vx/vz = velocidade (preenchida por quem dispara).
        const int hit = Room_move_object(room, obj, self->vx, 0, self->vz, true, &moved);
        if (hit == -2 || (hit >= 0 && !room->objects[hit]->particle) ||
            self->obj->contact != nullptr) {
            if (hit >= 0) {
                Object *t = room->objects[hit];
                if (t->freezable) {
                    snd_event(room, self->obj);
                    t->freeze_timer = 9000;
                    if (t->freeze_stops_anim) Sprite_stop_anim(t);
                }
            }
            Object *c = self->obj->contact;
            if (c != nullptr && c->freezable) {
                c->freeze_timer = 9000;
                if (self->obj->contact->freeze_stops_anim) Sprite_stop_anim(self->obj->contact);
            }
            Sprite_stop_anim(self->obj);
            Room_try_place_object(room, self->obj, -0x1000, 0x1000, 0x1000);
            self->disabled = true;
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_JOYSTICK: {
        // p28 = canal; p2c = speed; p30 = direcao fixa; state = modo fixo (vx/vz pre-calculados);
        // anim = ctl_anim_mode do alvo. Move obj->joystick_target.
        if (obj->contact != nullptr && (obj->contact_dx != 0 || obj->contact_dz != 0)) {
            const int cdx = obj->contact_dx;
            if (!self->state) {
                snd_bump_throttled(room, obj);
                Object *o = self->obj;
                self->vx = 0;
                self->vz = 0;
                if (o->contact_dx > 0)
                    self->vx = self->p2c;
                else if (o->contact_dx < 0)
                    self->vx = -self->p2c;
                else if (o->contact_dz > 0)
                    self->vz = self->p2c;
                else if (o->contact_dz < 0)
                    self->vz = -self->p2c;
                obj = o;
            } else {
                // modo fixo: so aceita empurrao no sentido configurado (original: sem zerar contact)
                if ((self->vx > 0) != (cdx > 0)) return;
                if ((self->vx < 0) != (cdx < 0)) return;
                if ((self->vz > 0) != (obj->contact_dz > 0)) return;
                if ((self->vz < 0) != (obj->contact_dz < 0)) return;
            }
            Object *tgt = obj->joystick_target;
            self->anim = tgt->ctl_anim_mode;
            if (self->anim > 0) {
                const int cur = (self->anim == 1) ? tgt->frame : tgt->anim;
                int dir;
                if (self->vx > 0)
                    dir = 2;
                else if (self->vx < 0)
                    dir = 0;
                else if (self->vz > 0)
                    dir = 3;
                else if (self->vz < 0)
                    dir = 1;
                else
                    // original: local_4 nao inicializado; com vx == vz == 0 o
                    // Controller_anim_by_velocity nao faz nada, entao o valor e irrelevante.
                    dir = cur;
                if (dir != cur) {
                    self->obj = tgt;  // anima o alvo com o ctl_anim_mode dele
                    Controller_anim_by_velocity(self);
                    self->obj = obj;
                }
            }
            const int hit = Room_move_object(room, self->obj->joystick_target, self->vx, 0,
                                             self->vz, true, &moved);
            if (hit == -1) {
                Object *t = self->obj->joystick_target;
                if (t->snd_move != nullptr && room->sound_mode <= t->snd_move_cat) {
                    voice_play_if_idle(t->snd_move, &t->voice_move, room->sfx_volume);
                    self->obj->contact = nullptr;
                    return;
                }
            } else {
                snd_bump_throttled(room, self->obj->joystick_target);
            }
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_SWITCH: {
        // p28 = canal; state = ligado; p2c = espera entre toques (3 ticks).
        if (obj->contact == nullptr) {
            if (self->p2c > 0) self->p2c--;
        } else if (self->p2c == 0) {
            self->p2c = 3;
            // inverte TODOS os switches do canal (inclusive este): quadro 0 = ligado, 1 = desligado
            if (self->state) {
                for (int i = 0; i < room->n_controllers; i++) {
                    Controller *c = room->controllers[i];
                    if (c->type == CTRL_SWITCH && c->p28 == self->p28) {
                        c->state = false;
                        Sprite_set_anim_frame(c->obj, 0, 1);
                        snd_bump_throttled(room, self->obj);
                    }
                }
            } else {
                for (int i = 0; i < room->n_controllers; i++) {
                    Controller *c = room->controllers[i];
                    if (c->type == CTRL_SWITCH && c->p28 == self->p28) {
                        c->state = true;
                        Sprite_set_anim_frame(c->obj, 0, 0);
                        snd_bump_throttled(room, self->obj);
                    }
                }
            }
            for (int i = 0; i < room->n_objects; i++) {
                Object *o = room->objects[i];
                if (o->channel == self->p28) {
                    o->active = self->state;
                    if (o->ctl_toggles_anim) {
                        if (self->state)
                            Sprite_play_anim(o, o->anim, 0, -1);
                        else
                            Sprite_stop_anim(o);
                    }
                }
            }
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_FOLLOWER: {
        // vy = speed; state = diagonal; flag2 = foge da luz; flag3 = modo por eixo; push; p2c =
        // alcance; p24 = +1 aproxima / -1 foge; max_x/max_z = dx/dz ate o alvo; max_y = face
        // atual; min_y = face nova; min_z = alternador (modo simples); p28/p30 = troca de eixo
        // (modo por eixo: p30 = 1 depois de bater).
        Object *target = room->active_player;  // local_4
        self->p24 = 1;
        if (self->flag2 && room->dark) {
            bool flee = false;
            if (room->n_lights > 0) {
                int best = 0;  // luz ligada mais forte presa a um objeto
                for (int i = 0; i < room->n_lights; i++) {
                    if (room->light_obj[i] != nullptr && room->light_on[i] &&
                        best < room->light_radius[i]) {
                        best = room->light_radius[i];
                        target = room->light_obj[i];
                    }
                }
                if (best > 0x31) {
                    self->p24 = -1;  // foge do dono da luz
                    flee = true;
                }
            }
            if (!flee) target = room->active_player;
        }
        if (room->four_crowns && room->id == 0x1bc) self->p24 = -1;
        self->max_x = target->x - obj->x;
        self->max_z = target->z - obj->z;
        if (self->p2c > 0) {  // fora do alcance (original: sem zerar contact)
            if (abs_cdq(self->max_x) > self->p2c) return;
            if (abs_cdq(self->max_z) > self->p2c) return;
        }
        if (self->anim == 1) self->max_y = obj->frame;
        if (self->anim == 2) self->max_y = obj->anim;
        const int speed = self->vy;
        if (self->state) {  // diagonal
            self->vz = 0;
            self->vx = 0;
            if (self->max_x > speed) self->vx = speed;
            if (self->max_x < -speed) self->vx = -speed;
            if (self->max_z > speed) self->vz = speed;
            if (self->max_z < -speed) self->vz = -speed;
        } else if (!self->flag3) {  // por eixo simples
            const int dx = self->max_x;
            const int adx = abs_cdq(dx);
            self->vz = 0;
            self->vx = 0;
            if (adx < speed) {
                self->min_z = 0;
                if (abs_cdq(self->max_z) >= speed) self->vz = self->max_z < 0 ? -speed : speed;
            } else {
                const int dz = self->max_z;
                const int adz = abs_cdq(dz);
                if (adz < speed) {
                    self->min_z = 0;
                    self->vx = dx < 0 ? -speed : speed;
                } else {
                    // os dois eixos longe: fica 1 tick parado, depois anda no eixo MENOR
                    // (original)
                    if (self->min_z == 0) {
                        self->min_z = 1;
                        return;
                    }
                    if (adx < adz)
                        self->vx = dx < 0 ? -speed : speed;
                    else
                        self->vz = dz < 0 ? -speed : speed;
                }
            }
        } else {  // por eixo "esperto": troca de eixo ao bater
            bool replan = true;  // cl
            if (self->p30 == 0) self->p28 = 0;
            if (self->p30 == 1 && self->p28 != 1) {
                self->p30 = 0;
                self->p28 = 1;
                if (abs_cdq(self->vx) > 0) {
                    self->vx = 0;
                    self->vz = self->max_z > 0 ? speed : -speed;
                } else {
                    self->vz = 0;
                    self->vx = self->max_x > 0 ? speed : -speed;
                }
            }
            if (self->vx > 0 && self->max_x >= speed) replan = false;
            if (self->vx < 0 && self->max_x <= -speed) replan = false;
            if (self->vz > 0 && self->max_z >= speed) replan = false;
            if (self->vz < 0 && self->max_z <= -speed) replan = false;
            if (replan) {
                const int dx = self->max_x;
                self->vz = 0;
                self->vx = 0;
                if (dx > 0) self->vx = speed;
                if (dx < 0) self->vx = -speed;
                if (self->max_z > 0) self->vz = speed;
                if (self->max_z < 0) self->vz = -speed;
                const int adx = abs_cdq(dx);
                if (adx < speed) {
                    self->vx = 0;
                } else {
                    const int adz = abs_cdq(self->max_z);
                    if (adz < speed)
                        self->vz = 0;
                    else if (adx < adz)
                        self->vz = 0;
                    else
                        self->vx = 0;
                }
            }
        }
        // 0x41201f. original: compara |dz| com vz (a velocidade), nao com speed.
        if (abs_cdq(self->max_x) < self->vy && abs_cdq(self->max_z) < self->vz) {
            self->vx = 0;
            self->vz = 0;
        }
        self->vx *= self->p24;
        self->vz *= self->p24;
        if (self->anim > 0) {
            if (self->vx > 0)
                self->min_y = 2;
            else if (self->vx < 0)
                self->min_y = 0;
            else if (self->vz > 0)
                self->min_y = 3;
            else if (self->vz < 0)
                self->min_y = 1;
            else
                self->min_y = self->max_y;
            if (self->min_y != self->max_y) Controller_anim_by_velocity(self);
        }
        // original: multiplica de novo por p24 (= +-1: volta a velocidade "para o alvo") e move
        // com velocidade * p24.
        self->vx *= self->p24;
        self->vz *= self->p24;
        const int hit_x =
            Room_move_object(room, self->obj, self->vx * self->p24, 0, 0, self->push, &moved);
        const int hit_z =
            Room_move_object(room, self->obj, 0, 0, self->vz * self->p24, self->push, &moved);
        if (hit_x != -1 || hit_z != -1) {
            self->p30 = 1;
            snd_bump_throttled(room, self->obj);
            return;
        }
        if (self->vx * self->p24 == 0 && self->vz * self->p24 == 0) return;
        snd_move_loop(room, self->obj);  // original: o Follower nunca zera contact
        return;
    }

    case CTRL_HIT_AND_TURN: {
        // vy = speed; p28 = direcao atual; state = sentido do giro (0: -1, 1: +1).
        const int hit = Room_move_object(room, obj, self->vx, 0, self->vz, self->push, &moved);
        Object *o = self->obj;
        if (hit == -1 && (o->contact == nullptr || moved < 1 || o->contact_dy < 0)) {
            snd_move_loop(room, o);
        } else {
            if (!self->state) {
                switch (self->p28) {
                case 0: self->p28 = 3; break;
                case 1: self->p28 = 0; break;
                case 2: self->p28 = 1; break;
                case 3: self->p28 = 2; break;
                }
            } else {
                switch (self->p28) {
                case 0: self->p28 = 1; break;
                case 1: self->p28 = 2; break;
                case 2: self->p28 = 3; break;
                case 3: self->p28 = 0; break;
                }
            }
            switch (self->p28) {
            case 0:
                self->vz = 0;
                self->vx = -self->vy;
                break;
            case 1:
                self->vx = 0;
                self->vz = -self->vy;
                break;
            case 2:
                self->vz = 0;
                self->vx = self->vy;
                break;
            case 3:
                self->vx = 0;
                self->vz = self->vy;
                break;
            }
            Controller_anim_by_velocity(self);
            snd_bump_loop(room, self->obj);
        }
        self->obj->contact = nullptr;
        return;
    }

    case CTRL_DISAPPEAR: {
        // state = disparado; p28 = quem dispara (0 Head, ou Heels com bloco de Head+Heels; 1
        // Heels); p2c = ticks desde o disparo; anim = anim de sumir. Sem zerar contact.
        if (self->state) {
            if (self->p2c == 7) {
                Sprite_play_anim(obj, self->anim, 0, -1);
                snd_event(room, self->obj);
            }
            if (self->p2c >= anim_ticks(self->obj->block, self->anim) + 5) {
                self->disabled = true;
                Room_destroy_object(room, self->obj);
                return;
            }
            self->p2c++;
            return;
        }
        if (self->p28 == 0) {
            if (room->objects[0]->x != kOutOfRoom) self->state = true;
            Object *heels = room->objects[1];
            if (heels->x != kOutOfRoom && heels->block->id == 2) {
                self->state = true;
                return;
            }
        } else if (room->objects[1]->x != kOutOfRoom) {
            self->state = true;
            return;
        }
        return;
    }

    case CTRL_HOOVER: {
        // state = morre no chao mortal; anim = anim de morte; max_x (+0x34) = 1 morrendo; p30 =
        // ticks da morte; max_y (+0x38) = altura de flutuacao; p2c = espera para subir; p28 =
        // ticks ate o proximo passo aleatorio; vx/vz = passo.
        if (self->max_x == 1) {
            if (self->p30 >= anim_ticks(obj->block, self->anim) - 2) {
                self->disabled = true;
                Room_destroy_object(room, obj);
                return;
            }
            self->p30++;
            return;
        }
        if (obj->contact != nullptr && obj->contact_dy < 0) {  // peso em cima: afunda 1
            Room_move_object(room, obj, 0, -1, 0, false, &moved);
            if (self->state && room->killer_floor && self->obj->y == 0) {
                self->max_x = 1;
                Sprite_play_anim(self->obj, self->anim, 0, -1);
                snd_event(room, self->obj);
            }
        } else if (obj->y < self->max_y) {  // sem peso: sobe 1 a cada 2 ticks
            if (self->p2c > 0) {
                self->p2c--;
            } else {
                Room_move_object(room, obj, 0, 1, 0, false, &moved);
                self->p2c = 1;
            }
        }
        self->obj->contact = nullptr;
        if (--self->p28 == 0) {
            self->p28 = rand() % 10 + 3;
            self->vx = rand() % 3 - 1;
            self->vz = rand() % 3 - 1;
            if (self->vx != 0 && self->vz != 0) {
                if (rand() % 2 == 0)
                    self->vx = 0;
                else
                    self->vz = 0;
            }
            Room_move_object(room, self->obj, self->vx, 0, self->vz, true, &moved);
            return;
        }
        return;
    }

    case CTRL_FLICKER: {
        // state = aceso; p28 = ticks aceso; max_x = duracao acesa; p2c = ticks apagado; min_x =
        // duracao apagada. Sem zerar contact.
        if (self->state) {
            if (self->p28 < self->max_x) {
                self->p28++;
                return;
            }
            self->p2c = 0;
            self->state = false;
            Controller_set_lights(self, false);
            return;
        }
        if (self->p2c >= self->min_x) {
            self->p28 = 0;
            self->state = true;
            Controller_set_lights(self, true);
            return;
        }
        self->p2c++;
        return;
    }

    case CTRL_SNOW: {
        // vy = velocidade de queda (-3..-6, desce vy/3); max_x (+0x34) = ticks restantes do
        // "splash"; p28 = ticks ate trocar a deriva; p2c/p30 = atraso entre passos (p30 0 ou 1).
        if (self->max_x > 0) {
            self->max_x--;
            if (self->max_x < 2 ||
                (obj->contact != nullptr &&
                 (obj->contact_dx != 0 || obj->contact_dz != 0 || obj->contact_dy < 0))) {
                Sprite_stop_anim(obj);
                Object *o = self->obj;
                Sprite_set_anim_frame(o, 0, rand() % o->block->frame_count[0]);
                Controller_respawn_particle(self, false);
                self->max_x = 0;
            }
            self->obj->contact = nullptr;
            return;
        }
        if (obj->y < 5 || obj->contact != nullptr) {  // chegou ao chao: splash do quadro atual
            self->max_x = (obj->block->frame_count[0] - obj->frame) * obj->block->anim_speed[0];
            Sprite_play_anim(obj, 0, obj->frame, -1);
        }
        self->obj->contact = nullptr;
        self->vx = 0;
        self->vz = 0;
        if (self->p28 == 0) {
            self->p28 = 4;
            self->vx = rand() % 3 - 1;
            self->vz = rand() % 3 - 1;
            if (self->vx != 0 && self->vz != 0) {
                if (rand() % 2 == 0)
                    self->vx = 0;
                else
                    self->vz = 0;
            }
        } else {
            self->p28--;
        }
        if (self->p2c < self->p30) {
            self->p2c++;
            return;
        }
        self->p2c = 0;
        Room_move_object(room, self->obj, self->vx, self->vy / 3, self->vz, false, &moved);
        return;
    }

    case CTRL_RAIN: {
        // vy = velocidade de queda (-5..-8); p28 = ticks restantes do splash (anim a partir do
        // quadro 4).
        if (self->p28 > 0) {
            self->p28--;
            if (self->p28 < 2 ||
                (obj->contact != nullptr &&
                 (obj->contact_dx != 0 || obj->contact_dz != 0 || obj->contact_dy < 0))) {
                Sprite_stop_anim(obj);
                Sprite_set_anim_frame(self->obj, 0, rand() % 4);
                Controller_respawn_particle(self, false);
                self->p28 = 0;
            }
            self->obj->contact = nullptr;
            return;
        }
        if (obj->y < 10 || obj->contact != nullptr) {
            self->p28 = (obj->block->frame_count[0] - 4) * obj->block->anim_speed[0];
            Sprite_play_anim(obj, 0, 4, -1);
        }
        self->obj->contact = nullptr;
        Room_move_object(room, self->obj, 0, self->vy, 0, false, &moved);
        return;
    }

    case CTRL_SLIDER: {
        // vy = speed; p2c = alcance (0 = qualquer); state = deslizando; max_x/max_z = dx/dz ate
        // Head/Heels. Sem zerar contact.
        if (!self->state) {
            for (int i = 0; i < 2; i++) {  // objects[0] Head, objects[1] Heels
                if (self->state) return;
                Object *p = room->objects[i];
                if (p->x == kOutOfRoom) continue;
                self->max_x = p->x - self->obj->x;
                self->max_z = p->z - self->obj->z;
                const int range = self->p2c;
                if (range != 0 &&
                    (abs_cdq(self->max_x) > range || abs_cdq(self->max_z) > range))
                    continue;
                self->vx = 0;
                self->vz = 0;
                if (abs_cdq(self->max_x) < 5) {  // alinhado em x: desliza em z
                    self->vz = self->max_z < 0 ? -self->vy : self->vy;
                } else if (abs_cdq(self->max_z) < 5) {  // alinhado em z: desliza em x
                    self->vx = self->max_x < 0 ? -self->vy : self->vy;
                }
                if (self->vx != 0 || self->vz != 0) {
                    if (self->anim > 0) Controller_anim_by_velocity(self);
                    self->state = true;
                }
            }
            return;
        }
        const int hit = Room_move_object(room, obj, self->vx, 0, self->vz, self->push, &moved);
        if (hit != -1) {
            self->state = false;
            if (self->anim == 2) Sprite_stop_anim(self->obj);
            snd_bump_throttled(room, self->obj);
            return;
        }
        snd_move_loop(room, self->obj);
        return;
    }

    case CTRL_GLIDER: {
        // vy = speed; p28 = direcao (-1 parado); quando o jogador ativo pressiona de cima, desliza
        // para onde ele esta virado (room->player_dir) ate bater.
        if (self->p28 == -1 && obj->contact == room->active_player && obj->contact_dy < 0) {
            int ch = room->active_player->block->id;
            if (ch == 2) ch = 1;  // Head+Heels juntos: usa a direcao do slot 1
            self->vx = 0;
            self->vz = 0;
            self->p28 = room->player_dir[ch];
            switch (self->p28) {
            case 0:
                self->vz = 0;
                self->vx = -self->vy;
                break;
            case 1:
                self->vx = 0;
                self->vz = -self->vy;
                break;
            case 2:
                self->vz = 0;
                self->vx = self->vy;
                break;
            case 3:
                self->vx = 0;
                self->vz = self->vy;
                break;
            }
            if (self->anim > 0) Controller_anim_by_velocity(self);
        }
        self->obj->contact = nullptr;
        const int hit =
            Room_move_object(room, self->obj, self->vx, 0, self->vz, self->push, &moved);
        if (hit != -1) {
            self->vx = 0;
            self->vz = 0;
            self->p28 = -1;
            snd_bump_loop(room, self->obj);
            return;
        }
        snd_move_loop(room, self->obj);
        return;
    }

    case CTRL_BUBBLE: {
        // vy = -3..-6 (sobe -vy/3); max_x = ticks ate parar a anim; p2c/p30 = atraso entre passos;
        // p28 = ticks ate trocar a deriva.
        if (self->max_x > 0) {
            self->max_x--;
            if (self->max_x < 1) {
                Sprite_stop_anim(obj);
                self->max_x = 0;
            }
        }
        if (self->obj->y > 400 || self->obj->contact != nullptr) {
            Controller_respawn_particle(self, false);
            Object *o = self->obj;
            const Block *b = o->block;
            self->max_x = rand() % (b->anim_speed[0] * b->frame_count[0] - 1) + 1;
            Sprite_play_anim(o, 0, 0, -1);
        }
        self->obj->contact = nullptr;
        self->vx = 0;
        self->vz = 0;
        if (self->p2c < self->p30) {
            self->p2c++;
            return;
        }
        if (self->p28 == 0) {
            self->p28 = 8;
            self->vx = rand() % 3 - 1;
            self->vz = rand() % 3 - 1;
            if (self->vx != 0 && self->vz != 0) {
                if (rand() % 2 == 0)
                    self->vx = 0;
                else
                    self->vz = 0;
            }
        } else {
            self->p28--;
        }
        self->p2c = 0;
        // original: imul 0x55555555 / sub / sar 1 = vy / -3 (truncado)
        Room_move_object(room, self->obj, self->vx, self->vy / -3, self->vz, false, &moved);
        return;
    }

    case CTRL_FOLLOWER_EXTRA: {
        // Campos do Follower + vx = offset de bloco, p30 = direcao de caminhada. max_x/max_z =
        // dx/dz ate o jogador e, depois, o passo. Ao achar vazio embaixo, cai, troca o grafico para
        // block_table[block->id + vx] e vira Follower. Sem zerar contact.
        Object *pl = room->active_player;
        self->max_x = pl->x - obj->x;
        self->max_z = pl->z - obj->z;
        if (self->p2c > 0) {
            if (abs_cdq(self->max_x) > self->p2c) return;
            if (abs_cdq(self->max_z) > self->p2c) return;
        }
        self->max_x = 0;
        self->max_z = 0;
        switch (self->p30) {  // original: 0 = +x e 2 = -x (ao contrario da convencao de direcao)
        case 0: self->max_x = self->vy / 2; break;
        case 1: self->max_z = -(self->vy / 2); break;
        case 2: self->max_x = -(self->vy / 2); break;
        case 3: self->max_z = self->vy / 2; break;
        }
        Room_move_object(room, obj, self->max_x, 0, self->max_z, self->push, &moved);
        Object *o = self->obj;
        if (Room_is_box_free(room, o->block, o->x, o->y - o->block->size_y, o->z)) {
            Room_move_object(room, self->obj, 0, -self->obj->block->size_y, 0, self->push, &moved);
            Sprite_set_block(self->obj, room->block_table[self->obj->block->id + self->vx]);
            self->vx = 0;
            self->p30 = 0;
            Controller_anim_by_velocity(self);
            self->type = CTRL_FOLLOWER;
            return;
        }
        return;
    }

    default:  // 2 e > 0x15
        return;
    }
}

// 0x00412cf0 Controller_respawn_particle
// Sorteia velocidade, atraso e uma posicao livre (x, z na sala; y 10..399 no inicio, 400 nos
// respawns, 0 para bolhas). Sempre devolve 0.
int Controller_respawn_particle(Controller *self, bool initial) {
    const int type = self->type;
    if (type == CTRL_SNOW)
        self->vy = -3 - rand() % 4;
    else if (type == CTRL_RAIN)
        self->vy = -5 - rand() % 4;
    else if (type == CTRL_BUBBLE)
        self->vy = -3 - rand() % 4;
    self->p30 = (rand() % 3 == 0) ? 1 : 0;
    int hit;
    do {
        const int rx = rand() % (self->room->width_px - 7);
        const int rz = rand() % (self->room->depth_px - 7);
        int y;
        if (!initial)
            y = (self->type != CTRL_BUBBLE) ? 400 : 0;
        else
            y = rand() % 390 + 10;
        Object *obj = self->obj;
        int moved;  // original: reaproveita o slot de `this` na pilha
        hit = Room_move_object(self->room, obj, rx - obj->x, y - obj->y, rz - obj->z, false,
                               &moved);
    } while (hit != -1);  // original: sem limite de tentativas
    return 0;
}

// ================================================================== Room: criacao de controladores

// 0x00416de0 Room_add_ctrl_teleporter
void Room_add_ctrl_teleporter(Room *self, Object *obj, int type, int dest_col, int dest_row,
                              int dest_x, int dest_y, int dest_z, int anim) {
    room_push_controller(self, Controller_ctor_teleporter(new Controller(), self, obj, type, dest_col,
                                                          dest_row, dest_x, dest_y, dest_z, anim));
}

// 0x00416e80 Room_add_ctrl_wanderer
void Room_add_ctrl_wanderer(Room *self, Object *obj, int type, int speed, bool flag, int c, int d,
                            bool push) {
    room_push_controller(self, Controller_ctor_random_hitturn_slider(new Controller(), self, obj,
                                                                     type, speed, flag, c, d, push));
}

// 0x00416f10 Room_add_ctrl_flicker
void Room_add_ctrl_flicker(Room *self, Object *obj, int type, bool lights, bool glows, int on_ticks,
                           int off_ticks, bool start_off) {
    room_push_controller(self, Controller_ctor_flicker(new Controller(), self, obj, type, lights,
                                                       glows, on_ticks, off_ticks, start_off));
}

// 0x00416fa0 Room_add_ctrl_trap
void Room_add_ctrl_trap(Room *self, Object *obj, int type, bool falls, int vanish_anim, bool vanish,
                        bool any_contact, bool only_while_weighted) {
    room_push_controller(self, Controller_ctor_trap(new Controller(), self, obj, type, falls,
                                                    vanish_anim, vanish, any_contact,
                                                    only_while_weighted));
}

// 0x00417030 Room_add_ctrl_mover
void Room_add_ctrl_mover(Room *self, Object *obj, int type, int axis, int speed, int ramp, int max,
                         int min, int pause, bool start_positive, int anim_mode, bool push,
                         bool reverse_on_bump) {
    room_push_controller(self, Controller_ctor_mover(new Controller(), self, obj, type, axis, speed,
                                                     ramp, max, min, pause, start_positive,
                                                     anim_mode, push, reverse_on_bump));
}

// 0x004170e0 Room_add_ctrl_simple
Controller *Room_add_ctrl_simple(Room *self, Object *obj, int type) {
    room_push_controller(self, Controller_ctor_basic(new Controller(), self, obj, type));
    return self->controllers[self->n_controllers - 1];
}

// 0x00417160 Room_add_ctrl_switch
void Room_add_ctrl_switch(Room *self, Object *obj, int type, int a, bool b, bool c) {
    room_push_controller(
        self, Controller_ctor_switch_hoover_glider(new Controller(), self, obj, type, a, b, c));
}

// 0x004171f0 Room_add_ctrl_joystick
void Room_add_ctrl_joystick(Room *self, Object *obj, int type, int channel, int fixed_dir,
                            int speed) {
    room_push_controller(self, Controller_ctor_joystick(new Controller(), self, obj, type, channel,
                                                        fixed_dir, speed));
}

// 0x00417280 Room_add_ctrl_extra
void Room_add_ctrl_extra(Room *self, Object *obj, int type, int a, int b) {
    room_push_controller(self,
                         Controller_ctor_extra_disappear(new Controller(), self, obj, type, a, b));
}

// 0x00417300 Room_add_ctrl_pusher
void Room_add_ctrl_pusher(Room *self, Object *obj, int type, int speed, bool vanish_on_hit,
                          int vanish_anim, bool anim_while_moving, int move_anim) {
    room_push_controller(self, Controller_ctor_pusher(new Controller(), self, obj, type, speed,
                                                      vanish_on_hit, vanish_anim,
                                                      anim_while_moving, move_anim));
}

// 0x00417390 Room_add_ctrl_follower
void Room_add_ctrl_follower(Room *self, Object *obj, int type, int speed, bool diagonal,
                            int anim_mode, bool push, bool fears_light, int range,
                            bool axis_mode) {
    room_push_controller(self, Controller_ctor_follower(new Controller(), self, obj, type, speed,
                                                        diagonal, anim_mode, push, fears_light,
                                                        range, axis_mode));
}

// 0x00417430 Room_add_ctrl_follower_extra
void Room_add_ctrl_follower_extra(Room *self, Object *obj, int type, int speed, bool diagonal,
                                  int anim_mode, bool push, bool fears_light, int range,
                                  bool axis_mode, int block_offset, int walk_dir) {
    room_push_controller(self, Controller_ctor_follower_extra(new Controller(), self, obj, type,
                                                              speed, diagonal, anim_mode, push,
                                                              fears_light, range, axis_mode,
                                                              block_offset, walk_dir));
}

// 0x004174e0 Room_update_controllers
void Room_update_controllers(Room *self) {
    // original: relê n_controllers a cada volta
    for (int i = 0; i < self->n_controllers; i++) Controller_update(self->controllers[i]);
}

}  // namespace hoh

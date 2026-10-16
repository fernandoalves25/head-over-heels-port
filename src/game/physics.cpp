// Modulo physics: Sprite/Object (construcao, posicao, animacao), lista de objetos da sala,
// colisao (AABB), movimento com cadeia de empurroes, gravidade, esteiras, sons de objeto.
//
// Fidelidade: as comparacoes, a ordem de varredura dos objetos e os efeitos colaterais seguem o
// pseudo-C (re/decomp/game.c) conferido com a desmontagem do HoH.exe (Room_move_object,
// Room_move_object_at, Room_update_physics, Room_update_conveyor, Room_animate). Todas as
// comparacoes de coordenadas sao com sinal (jl/jg/jle/jge no asm).
#include "game/physics.h"

#include "game/globals.h"
#include "game/room_render.h"

namespace hoh {

namespace {

// abs() como o MSVC gera (cdq; xor; sub), sem UB para INT_MIN.
int abs_cdq(int v) {
    const unsigned sign = v < 0 ? 0xffffffffu : 0u;
    return static_cast<int>((static_cast<unsigned>(v) ^ sign) - sign);
}

}  // namespace

// ================================================================== posicoes salvas

// 0x00405a10 room_save_object_positions
void room_save_object_positions() {
    const int n = g_char_rooms[g_active_room_slot]->n_objects;
    for (int i = 0; i < n; i++) {
        Object *obj = g_char_rooms[g_active_room_slot]->objects[i];
        obj->saved_x = obj->x;
        obj->saved_y = obj->y;
        obj->saved_z = obj->z;
    }
}

// 0x00405a60 room_restore_object_positions
void room_restore_object_positions() {
    const int n = g_char_rooms[g_active_room_slot]->n_objects;
    for (int i = 0; i < n; i++) {
        Object *obj = g_char_rooms[g_active_room_slot]->objects[i];
        obj->x = obj->saved_x;
        obj->y = obj->saved_y;
        obj->z = obj->saved_z;
    }
}

// ================================================================== Sprite

// 0x0040fc30 Sprite_ctor
Sprite *Sprite_ctor(Sprite *self) {
    self->vtbl = g_vtbl_Sprite;
    self->work_bmp = nullptr;
    self->dirty = true;
    self->overlap_count = 0;
    self->recv_shadow = nullptr;
    self->hidden = false;
    self->transparent = false;
    self->alpha = 0;
    return self;
}

// 0x0040fc60 Sprite_deleting_dtor
void *Sprite_deleting_dtor(Sprite *self, int flags) {
    Sprite_dtor(self);
    if ((flags & 1) != 0) delete self;
    return self;
}

// 0x0040fc80 Sprite_dtor
// work_bmp e recv_shadow sao criados pelo motor (pertencem ao sprite); destroy_bitmap aceita NULL.
void Sprite_dtor(Sprite *self) {
    self->vtbl = g_vtbl_Sprite;
    destroy_bitmap(self->work_bmp);
    destroy_bitmap(self->recv_shadow);
}

// 0x0040fcb0 Sprite_init
void Sprite_init(Sprite *self, Block *block, int x, int y, int z, int index) {
    self->block = block;
    self->hidden = block->hidden;
    self->x = x;
    self->y = y;
    self->z = z;
    self->overlap_count = 0;
    self->index = index;
    self->work_bmp = nullptr;
    self->dirty = true;
    self->cur_img = block->image;
    self->cur_shadow = block->shadow;
    Sprite_update_screen_pos(self);
    self->recv_shadow = create_bitmap(self->block->sprite_w, self->block->sprite_h);
    Sprite_create_work_bitmap(self);
    self->light = 0xff;
    self->animating = false;
    self->anim = 0;
    self->frame = 0;
    self->anim_speed = 1;
}

// 0x0040fd50 Sprite_set_block
void Sprite_set_block(Sprite *self, Block *block) {
    Sprite_invalidate(self);
    destroy_bitmap(self->work_bmp);
    destroy_bitmap(self->recv_shadow);
    self->dirty = true;
    self->work_bmp = nullptr;
    self->overlap_count = 0;
    self->block = block;
    self->cur_img = block->image;
    self->cur_shadow = block->shadow;
    Sprite_create_work_bitmap(self);
    Sprite_update_screen_pos(self);
    // original: recv_shadow do tamanho do work_bmp (= cur_img), nao de sprite_w x sprite_h.
    self->recv_shadow = create_bitmap(self->work_bmp->w, self->work_bmp->h);
}

// 0x0040ffd0 Sprite_set_position
void Sprite_set_position(Sprite *self, int x, int y, int z) {
    self->x = x;
    self->y = y;
    self->z = z;
    Sprite_invalidate(self);
}

// 0x0040fff0 Sprite_play_anim
void Sprite_play_anim(Sprite *self, int anim, int frame, int speed) {
    self->animating = true;
    self->anim = anim;
    self->frame = frame;
    if (speed == -1) speed = self->block->anim_speed[anim];
    self->anim_speed = speed;
    self->cur_img = self->block->frames[anim][frame];
    self->cur_shadow = self->block->shadows[anim][frame];
    self->anim_timer = 0;
    Sprite_invalidate(self);
}

// 0x00410060 Sprite_tick_anim
void Sprite_tick_anim(Sprite *self) {
    const int timer = self->anim_timer + 1;
    self->anim_timer = timer;
    if (self->anim_speed <= timer) {
        Block *block = self->block;
        const int frame = self->frame + 1;
        self->frame = frame;
        self->anim_timer = 0;
        // original: igualdade (nao >=) com frame_count.
        if (frame == block->frame_count[self->anim]) self->frame = 0;
        self->cur_img = block->frames[self->anim][self->frame];
        self->cur_shadow = block->shadows[self->anim][self->frame];
        Sprite_invalidate(self);
    }
}

// 0x004100e0 Sprite_stop_anim
void Sprite_stop_anim(Sprite *self) {
    self->animating = false;
}

// 0x004100f0 Sprite_set_anim_frame
void Sprite_set_anim_frame(Sprite *self, int anim, int frame) {
    const bool was_animating = self->animating;
    Sprite_play_anim(self, anim, frame, self->anim_speed);
    self->animating = was_animating;
}

// ================================================================== Object

// 0x00410120 Object_deleting_dtor
void *Object_deleting_dtor(Object *self, int flags) {
    Object_dtor(self);
    if ((flags & 1) != 0) delete self;
    return self;
}

// 0x00410140 Object_ctor
Object *Object_ctor(Object *self, Block *block, int x, int y, int z, bool pushable, bool sticky,
                    bool gravity, int index) {
    Sprite_ctor(self);
    self->vtbl = g_vtbl_Object;
    Sprite_init(self, block, x, y, z, index);
    self->pushable = pushable;
    self->sticky = sticky;
    self->gravity = gravity;
    self->standing_on = nullptr;
    self->is_conveyor = false;
    self->on_conveyor = false;
    self->conveyor = nullptr;
    self->conv_dx = 0;
    self->conv_dz = 0;
    self->door_side = 0;
    self->bonus_pending = -1;
    self->record_index = -1;
    self->channel = -1;
    self->snd_ambient_cat = -1;
    self->snd_move_cat = -1;
    self->snd_bump_cat = -1;
    self->snd_event_cat = -1;
    self->voice_ambient = -1;
    self->voice_move = -1;
    self->voice_bump = -1;
    self->bump_last_tick = -1;
    self->contact = nullptr;
    self->contact_dx = 0;
    self->contact_dy = 0;
    self->contact_dz = 0;
    self->pickable = false;
    self->freezable = false;
    self->freeze_stops_anim = false;
    self->freeze_timer = 0;
    self->touched_killer = false;
    self->killer = false;
    self->ctl_anim_mode = 0;
    self->ctl_toggles_anim = false;
    self->joystick_target = nullptr;
    self->active = true;
    self->particle = false;
    self->teleporter = false;
    self->snd_ambient = nullptr;
    self->snd_move = nullptr;
    self->snd_bump = nullptr;
    self->snd_event = nullptr;
    self->trap = false;
    // original: bonus_record_index (+0x4a0) e saved_x/y/z nao sao inicializados (new Object() zera).
    return self;
}

// 0x004102b0 Object_dtor
void Object_dtor(Object *self) {
    self->vtbl = g_vtbl_Object;
    if (self->snd_ambient != nullptr && self->voice_ambient != -1) {
        release_voice(self->voice_ambient);
    }
    Sprite_dtor(self);
}

// 0x004102f0 Object_set_conveyor
void Object_set_conveyor(Object *self, int dx, int dz) {
    self->is_conveyor = true;
    self->conv_dx = dx;
    self->conv_dz = dz;
}

// 0x00410310 Object_set_door_side
void Object_set_door_side(Object *self, int side) {
    self->door_side = side;
}

// port: destrutor pela vtable ((***obj)(1)).
void Object_destroy(Object *self) {
    Object_deleting_dtor(self, 1);
}

// ================================================================== Room: lista de objetos

// 0x00412ff0 Room_append_object
void Room_append_object(Room *self, Object *obj) {
    self->objects[self->n_objects] = obj;
    self->n_objects = self->n_objects + 1;
    Room_invalidate_objects(self);
}

// 0x00413aa0 Room_remove_object
void Room_remove_object(Room *self, Object *obj) {
    if (obj != nullptr) Room_remove_object_at(self, obj->index);
}

// 0x00413ac0 Room_remove_object_at
void Room_remove_object_at(Room *self, int index) {
    Object *removed = self->objects[index];
    for (; index < self->n_objects - 1; index++) {
        self->objects[index] = self->objects[index + 1];
        self->objects[index]->index = index;
    }
    self->n_objects = self->n_objects - 1;
    if (removed != nullptr) Object_destroy(removed);
    self->objects[self->n_objects] = nullptr;
    Room_invalidate_objects(self);
}

// 0x00416ac0 Room_create_object
Object *Room_create_object(Room *self, Block *block, int x, int y, int z, bool pushable,
                           bool sticky, bool gravity) {
    Object *obj = Object_ctor(new Object(), block, x, y, z, pushable, sticky, gravity,
                              self->n_objects);
    Room_append_object(self, obj);
    return obj;
}

// 0x00417700 Room_destroy_object
void Room_destroy_object(Room *self, Object *obj) {
    for (int i = 0; i < self->n_controllers; i++) {
        if (self->controllers[i]->obj == obj) self->controllers[i]->disabled = true;
    }
    for (int i = 0; i < self->n_lights; i++) {
        if (self->light_obj[i] == obj) self->light_on[i] = false;
    }
    for (int i = 0; i < self->n_glows; i++) {
        if (self->glows[i]->obj == obj) self->glow_on[i] = false;
    }
    for (int i = 0; i < self->n_objects; i++) {
        Object *o = self->objects[i];
        if (o != obj && o->standing_on == obj) o->standing_on = nullptr;
    }
    Room_remove_object(self, obj);
}

// ================================================================== Room: colisao

// 0x00413d40 Room_is_box_free
// original: testa TODOS os objetos (nenhum e ignorado); a caixa e a de `box` (size_x/y/z).
bool Room_is_box_free(Room *self, const Block *box, int x, int y, int z) {
    const int n = self->n_objects;
    for (int i = 0; i < n; i++) {
        const Object *o = self->objects[i];
        const Block *ob = o->block;
        if (o->x < box->size_x + x && x < ob->size_x + o->x &&
            o->y < box->size_y + y && y < ob->size_y + o->y &&
            o->z < box->size_z + z && z < ob->size_z + o->z) {
            return false;
        }
    }
    return true;
}

// 0x00413e10 Room_find_collision
// Ignora o objeto pelo INDICE (i != obj->index), nao pelo ponteiro.
bool Room_find_collision(Room *self, Object *obj, int x, int y, int z, Object **hit) {
    *hit = nullptr;
    const Block *b = obj->block;
    for (int i = 0; i < self->n_objects; i++) {
        if (i == obj->index) continue;
        Object *o = self->objects[i];
        const Block *ob = o->block;
        if (o->x < b->size_x + x && x < ob->size_x + o->x &&
            o->y < b->size_y + y && y < ob->size_y + o->y &&
            o->z < b->size_z + z && z < ob->size_z + o->z) {
            *hit = o;
            return false;
        }
    }
    return true;
}

// 0x00413f10 Room_try_place_object
bool Room_try_place_object(Room *self, Object *obj, int x, int y, int z) {
    Object *hit;
    if (Room_find_collision(self, obj, x, y, z, &hit)) {
        Sprite_set_position(obj, x, y, z);
        return true;
    }
    return false;
}

// 0x00416910 object_stands_on
// top->y exatamente no topo de base e as caixas se cruzam em x e z.
bool object_stands_on(const Object *top, const Object *base) {
    const Block *bb = base->block;
    if (top->y == bb->size_y + base->y) {
        if (base->x < top->block->size_x + top->x && top->x < bb->size_x + base->x &&
            base->z < top->block->size_z + top->z && top->z < bb->size_z + base->z) {
            return true;
        }
    }
    return false;
}

// 0x00416980 object_stands_on_tol
bool object_stands_on_tol(const Object *top, const Object *base) {
    const Block *bb = base->block;
    const int base_top = base->y + bb->size_y;
    if (top->y == base_top || top->y - 1 == base_top) {
        if (base->x < top->block->size_x + top->x && top->x < bb->size_x + base->x &&
            base->z < top->block->size_z + top->z && top->z < bb->size_z + base->z) {
            return true;
        }
    }
    return false;
}

// 0x004169f0 Room_has_weight_on
bool Room_has_weight_on(Room *self, Object *obj) {
    const int index = obj->index;
    for (int i = 0; i < self->n_objects; i++) {
        if (i != index && self->objects[i]->gravity) {
            if (object_stands_on_tol(self->objects[i], obj)) return true;
        }
    }
    return false;
}

// 0x00417890 Room_has_rider
bool Room_has_rider(Room *self, Object *obj) {
    for (int i = 0; i < self->n_objects; i++) {
        const Object *o = self->objects[i];
        if (o != obj && o->standing_on == obj) return true;
    }
    return false;
}

// ================================================================== Room: movimento e fisica

// 0x004160d0 Room_move_object
int Room_move_object(Room *self, Object *obj, int dx, int dy, int dz, bool push, int *moved) {
    if (obj == nullptr) return 0;  // original: 0 (nao -1) e *moved intocado
    self->moved_count = 0;
    const int result = Room_move_object_at(self, obj->index, dx, dy, dz, push);
    const int count = self->moved_count;
    *moved = count;
    if (count > 0) *moved = count - 1;
    // Jogador ativo batendo num batente de porta a 2 px/tick: desliza para a abertura.
    if (obj == self->active_player && result >= 0 &&
        (abs_cdq(dx) == 2 || abs_cdq(dy) == 2 || abs_cdq(dz) == 2)) {
        Object *post = self->objects[result];
        if (post->door_side != 0) {
            int ignored;  // original: reaproveita a pilha do parametro obj
            switch (post->door_side) {
            case 1:
                if (dz == 0 &&
                    post->block->size_z + post->z < obj->block->size_z + obj->z) {
                    Room_move_object(self, obj, 0, 0, 2, false, &ignored);
                    return result;
                }
                break;
            case 2:
                if (dz == 0 && obj->z < post->z) {
                    Room_move_object(self, obj, 0, 0, -2, false, &ignored);
                    return result;
                }
                break;
            case 3:
                if (dx == 0 &&
                    post->block->size_x + post->x < obj->block->size_x + obj->x) {
                    Room_move_object(self, obj, 2, 0, 0, false, &ignored);
                    return result;
                }
                break;
            case 4:
                if (dx == 0 && obj->x < post->x) {
                    Room_move_object(self, obj, -2, 0, 0, false, &ignored);
                }
                break;
            default:
                break;
            }
        }
    }
    return result;
}

// 0x00416290 Room_move_object_at
// Aplica o deslocamento; -2 se sai da sala (limites ou recortes), indice do objeto atingido se
// colide (posicao desfeita; com push, tenta empurrar o atingido recursivamente), -1 se moveu.
int Room_move_object_at(Room *self, int index, int dx, int dy, int dz, bool push) {
    Object *obj = self->objects[index];
    const int ny = obj->y + dy;
    const int nx = obj->x + dx;
    const int nz = obj->z + dz;
    const Block *b = obj->block;
    obj->y = ny;
    obj->x = nx;
    obj->z = nz;
    // original: estes valores ficam em cache durante toda a varredura
    const int size_y = b->size_y;
    const int end_x = b->size_x + nx;
    const int end_z = b->size_z + nz;
    if (nx < 0 || ny < 0 || nz < 0 || self->width_px < end_x || self->depth_px < end_z ||
        (!self->rectangular &&
         ((self->cut_a_x < end_x && self->cut_a_z < end_z) ||
          (nx < self->cut_b_x && self->cut_b_z < end_z) ||
          (self->notch_x0 < end_x && nx < self->notch_x1 && nz < self->notch_z)))) {
        obj->z = nz - dz;
        obj->x = nx - dx;
        obj->y = ny - dy;
        return -2;
    }
    for (int i = 0; i < self->n_objects; i++) {
        if (i == index) continue;
        Object *o = self->objects[i];
        const Block *ob = o->block;
        if (o->x < end_x && nx < ob->size_x + o->x &&
            o->y < size_y + ny && ny < ob->size_y + o->y &&
            o->z < end_z && nz < ob->size_z + o->z) {
            if (!obj->particle) {
                o->contact = obj;
                o->contact_dx = dx;
                o->contact_dy = dy;
                o->contact_dz = dz;
            }
            if (!o->particle) {
                obj->contact = o;
                obj->contact_dx = 0;
                obj->contact_dy = 0;
                obj->contact_dz = 0;
            }
            if (o->killer) obj->touched_killer = true;
            if (obj->killer) o->touched_killer = true;
            obj->x = obj->x - dx;
            obj->y = obj->y - dy;
            obj->z = obj->z - dz;
            if (!push || !o->pushable || (o->freezable && o->freeze_timer > 0)) {
                return i;
            }
            const int r = Room_move_object_at(self, i, dx, dy, dz, true);
            if (!obj->particle) {
                o->contact = obj;
                o->contact_dx = dx;
                o->contact_dy = dy;
                o->contact_dz = dz;
            }
            if (!o->particle) {
                obj->contact = o;
                obj->contact_dx = 0;
                obj->contact_dy = 0;
                obj->contact_dz = 0;
            }
            if (r != -1) return i;
            // empurrou: volta a ocupar a posicao nova e continua a varredura
            obj->y = obj->y + dy;
            obj->x = obj->x + dx;
            obj->z = obj->z + dz;
        }
    }
    Room_carry_riders(self, index, dx, dz);
    Sprite_invalidate(obj);
    if (!obj->particle) self->moved_count = self->moved_count + 1;
    return -1;
}

// 0x00416600 Room_update_physics
void Room_update_physics(Room *self) {
    for (int i = self->n_objects - 1; i >= 0; i--) {
        Object *obj = self->objects[i];
        if (obj->gravity && obj->y > 0) {
            // a queda nao conta como contato: salva e restaura contact*
            const int save_dy = obj->contact_dy;
            const int save_dx = obj->contact_dx;
            const int save_dz = obj->contact_dz;
            Object *save_contact = obj->contact;
            // original: cai 2; se bater (ou sair da sala), tenta 1. Submerso: so 1.
            int r;
            if (self->underwater) {
                r = Room_move_object_at(self, i, 0, -1, 0, false);
            } else {
                r = Room_move_object_at(self, i, 0, -2, 0, false);
                if (r != -1) r = Room_move_object_at(self, i, 0, -1, 0, false);
            }
            obj->contact_dy = save_dy;
            obj->contact_dx = save_dx;
            obj->contact = save_contact;
            obj->contact_dz = save_dz;
            if (!obj->sticky || r < 0) {
                obj->standing_on = nullptr;
            } else {
                Object *cur = obj->standing_on;
                Object *base = self->objects[r];
                if (cur != nullptr && cur != base && object_stands_on(obj, cur)) base = cur;
                if (!base->is_conveyor && !base->particle) {
                    obj->standing_on = base;
                } else {
                    obj->standing_on = nullptr;
                }
            }
        }
        if (obj->is_conveyor && obj->active) Room_update_conveyor(self, obj);
    }
    // Saida pelo chao: objetos com gravidade que chegam a y == 0 somem (exceto Head/Heels, i <= 1).
    for (int i = self->n_objects - 1; i >= 0; i--) {
        Object *obj = self->objects[i];
        if (self->has_down_exit && i > 1 && obj->gravity && obj->y == 0) {
            Room_destroy_object(self, obj);
        }
    }
}

// 0x004167a0 Room_update_conveyor
void Room_update_conveyor(Room *self, Object *conveyor) {
    const int ci = conveyor->index;
    for (int i = 0; i < self->n_objects; i++) {
        Object *o = self->objects[i];
        if (i == ci) continue;
        if (!o->on_conveyor) {
            if (o->pushable && o->standing_on == nullptr && object_stands_on(o, conveyor)) {
                o->on_conveyor = true;
                o->conveyor = conveyor;
                Room_move_object_at(self, i, conveyor->conv_dx, 0, conveyor->conv_dz, false);
            }
        } else if (o->conveyor == conveyor) {
            if (object_stands_on(o, conveyor)) {
                Room_move_object_at(self, i, conveyor->conv_dx, 0, conveyor->conv_dz, false);
            } else {
                // saiu desta esteira: procura outra entre os objetos de indice > ci (do fim
                // para o comeco); a 1a encontrada ja o leva neste tick.
                o->on_conveyor = false;
                o->conveyor = nullptr;
                const int last = self->n_objects - 1;
                if (ci < last) {
                    for (int k = last; k > ci; k--) {
                        Object *c = self->objects[k];
                        if (c->is_conveyor && object_stands_on(o, c) && !o->on_conveyor) {
                            o->on_conveyor = true;
                            o->conveyor = c;
                            Room_move_object_at(self, i, c->conv_dx, 0, c->conv_dz, false);
                        }
                    }
                }
            }
        }
    }
}

// 0x00416a50 Room_carry_riders
void Room_carry_riders(Room *self, int index, int dx, int dz) {
    for (int i = 0; i < self->n_objects; i++) {
        if (i != index && self->objects[i]->sticky &&
            self->objects[i]->standing_on == self->objects[index]) {
            Room_move_object_at(self, i, dx, 0, dz, false);
        }
    }
}

// 0x00415b50 Room_animate
// Objetos com `animating`; celulas animadas de piso e paredes (so a animacao 0 do Block).
void Room_animate(Room *self) {
    for (int i = 0; i < self->n_objects; i++) {
        if (self->objects[i]->animating) Sprite_tick_anim(self->objects[i]);
    }
    for (int i = 0; i < self->width + self->border; i++) {
        for (int j = 0; j < self->depth + self->border; j++) {
            const Block *cell = self->floor_cell[i][j];
            if (cell != nullptr && cell->animated && self->floor_anim_on[i][j]) {
                const int tick = self->floor_anim_tick[i][j] + 1;
                self->floor_anim_tick[i][j] = tick;
                if (cell->anim_speed[0] <= tick) {
                    self->floor_anim_tick[i][j] = 0;
                    const int frame = self->floor_anim_frame[i][j] + 1;
                    self->floor_anim_frame[i][j] = frame;
                    if (cell->frame_count[0] <= frame) self->floor_anim_frame[i][j] = 0;
                }
            }
        }
    }
    for (int i = 0; i < self->width + self->border; i++) {
        const int h = self->wall0_height[i];
        for (int j = 0; j < h; j++) {
            const Block *cell = self->wall0_cell[i][j];
            if (cell != nullptr && cell->animated && self->wall0_anim_on[i][j] != 0) {
                const int tick = self->wall0_anim_tick[i][j] + 1;
                self->wall0_anim_tick[i][j] = tick;
                if (cell->anim_speed[0] <= tick) {
                    self->wall0_anim_tick[i][j] = 0;
                    const int frame = self->wall0_anim_frame[i][j] + 1;
                    self->wall0_anim_frame[i][j] = frame;
                    if (cell->frame_count[0] <= frame) self->wall0_anim_frame[i][j] = 0;
                }
            }
        }
    }
    for (int i = 0; i < self->depth + self->border; i++) {
        const int h = self->wall1_height[i];
        for (int j = 0; j < h; j++) {
            const Block *cell = self->wall1_cell[i][j];
            if (cell != nullptr && cell->animated && self->wall1_anim_on[i][j] != 0) {
                const int tick = self->wall1_anim_tick[i][j] + 1;
                self->wall1_anim_tick[i][j] = tick;
                if (cell->anim_speed[0] <= tick) {
                    self->wall1_anim_tick[i][j] = 0;
                    const int frame = self->wall1_anim_frame[i][j] + 1;
                    self->wall1_anim_frame[i][j] = frame;
                    if (cell->frame_count[0] <= frame) self->wall1_anim_frame[i][j] = 0;
                }
            }
        }
    }
}

// ================================================================== sons de objeto

// 0x004178d0 Room_update_object_sounds
void Room_update_object_sounds(Room *self, int tick) {
    self->sound_tick = tick;
    for (int i = 0; i < self->n_objects; i++) {
        Object *obj = self->objects[i];
        if (obj->snd_ambient != nullptr && self->sound_mode <= obj->snd_ambient_cat) {
            voice_play_if_idle(obj->snd_ambient, &obj->voice_ambient, self->sfx_volume);
        }
    }
}

// 0x00417940 voice_play_if_idle
void voice_play_if_idle(const SAMPLE *spl, int *voice, int volume) {
    if (*voice != -1 && voice_get_position(*voice) != -1) return;
    if (*voice != -1) release_voice(*voice);
    const int v = allocate_voice(spl);
    if (v < 0) {
        *voice = -1;
        return;
    }
    voice_set_volume(v, volume * 0x11);
    voice_set_pan(v, 0x80);
    voice_set_frequency(v, spl->freq);
    voice_set_playmode(v, PLAYMODE_PLAY);
    voice_start(v);
    *voice = v;
}

// 0x004179e0 Room_play_sample
// original: `volume` nao e lido; usa o volume da sala.
void Room_play_sample(Room *self, const SAMPLE *spl, int volume, int category) {
    if (self->sound_mode <= category) {
        play_sample(spl, self->sfx_volume * 0x11, 0x80, 1000, 0);
    }
}

// 0x00417a20 Room_play_sample_throttled
// original: *last_tick recebe o tick atual mesmo quando nao toca (chamadas seguidas silenciam).
void Room_play_sample_throttled(Room *self, const SAMPLE *spl, int *last_tick) {
    if (*last_tick < self->sound_tick - 3) {
        play_sample(spl, self->sfx_volume * 0x11, 0x80, 1000, 0);
        *last_tick = self->sound_tick;
        return;
    }
    *last_tick = self->sound_tick;
}

}  // namespace hoh

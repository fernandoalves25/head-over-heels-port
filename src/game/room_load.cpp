// Modulo room_load: montagem da sala a partir do registro do blob (load_room), Door,
// RoomBase/Room (construcao/destruicao), piso, paredes, recortes da planta, luzes fixas/presas e
// portas.
//
// Fidelidade: logica, constantes e ordem das chamadas seguem o pseudo-C (re/decomp/game.c)
// conferido com a desmontagem do HoH.exe (load_room inteiro, a tabela de saltos 0x404b90 do switch
// de atributos, Door_build_objects, Room_apply_shape_cuts). Em particular a ordem das leituras do
// registro (cada room_read_* vai para uma variavel local antes de ser usada: a ordem de avaliacao
// de argumentos em C++ nao e definida).
//
// Grades [255][255]: o original indexa um array plano (linha*255 + coluna); aqui viram
// grade[linha][coluna] (o mesmo endereco). Os indices de linha/coluna sao os do original, inclusive
// os deslocamentos estranhos de Room_apply_shape_cuts (+0x180/+0x578/... = linha anterior).
#include "game/room_load.h"

#include "game/controllers.h"
#include "game/engine_data.h"
#include "game/globals.h"
#include "game/physics.h"
#include "game/room_render.h"
#include "game/sound.h"

#include <cstring>

namespace hoh {

// 0x00431f88 g_sound_index_map: parametro de MOBILE-Sound (1..2) -> id da amostra no Sound.dat.
// No exe e o ultimo int de g_sample_category ([0] = 1, nunca usado: p == 0 = sem som) seguido de
// 16 (Heli) e 35; depois vem outro dado (ponteiros), que o jogo nunca indexa.
int32_t g_sound_index_map[3] = {1, 16, 35};

namespace {

// Amostra do id no Sound.dat (original: *(SAMPLE **)(g_sound_datafile + id*0x10)).
SAMPLE *sound_sample(int id) { return static_cast<SAMPLE *>(g_sound_datafile[id].dat); }

// Laco comum a RoomBase_init e Room_build_walls: zera alturas e as 6 grades das paredes.
void clear_wall_grids(RoomBase *self) {
    for (int i = 0; i < kRoomCells; i++) {
        self->wall0_height[i] = 0;
        self->wall1_height[i] = 0;
        for (int j = 0; j < kRoomCells; j++) {
            self->wall0_z[i][j] = 0;
            self->wall1_x[i][j] = 0;
            self->wall0_cell[i][j] = nullptr;
            self->wall0_light[i][j] = 0;
            self->wall1_cell[i][j] = nullptr;
            self->wall1_light[i][j] = 0;
        }
    }
}

// Peca k do array plano de World (+0x04 portal / +0x34 pedestal): k = espelhado + 6*lado + 2*peca.
Block *door_piece(const World *w, int k) { return w->door_part[k / 2][k % 2]; }
Block *pillar_piece(const World *w, int k) { return w->pillar_part[k / 2][k % 2]; }

}  // namespace

// ================================================================== Door

// 0x00401260 Door_deleting_dtor
void *Door_deleting_dtor(Door *self, int flags) {
    Door_dtor(self);
    if ((flags & 1) != 0) delete self;
    return self;
}

// 0x00401280 Door_dtor: so restaura a vtable (os objetos de sala da porta pertencem a sala).
void Door_dtor(Door *self) { self->vtbl = g_vtbl_Door; }

void Door_destroy(Door *self) { Door_deleting_dtor(self, 1); }

// 0x00401290 Door_build_objects: pedestal (peca Q2 na base, Q1 repetida, Q0 no topo), os dois
// batentes (P0, P1) e a verga (P2) por cima. k = mirrored + 6*side; pecas em k + 2*peca.
void Door_build_objects(Door *self, int x, int pillar_blocks, int z, int side, int mirrored) {
    const int k = mirrored + side * 6;
    int y = 0;
    int side_code = 0;  // local_c: 0 (batentes ao longo de z) ou 2 (ao longo de x)
    int off_x = 0;      // local_4
    int off_z = 0;      // local_8
    Object *obj = nullptr;
    self->n_objects = 0;
    if (pillar_blocks > 1) {
        obj = Room_create_object(self->room, pillar_piece(self->world, k + 4), x, 0, z, false, false,
                                 false);
        y = pillar_piece(self->world, k + 4)->size_y;
        self->n_objects = self->n_objects + 1;
    }
    // original: com pillar_blocks == 1 grava aqui o proprio int 1 como ponteiro; e sobrescrito
    // logo abaixo (peca do topo) sem ser lido.
    if (pillar_blocks > 0) self->bottom_obj = obj;
    if (pillar_blocks > 2) {
        for (int n = pillar_blocks - 2; n != 0; n--) {
            obj = Room_create_object(self->room, pillar_piece(self->world, k + 2), x, y, z, false,
                                     false, false);
            y = y + pillar_piece(self->world, k + 2)->size_y;
            self->n_objects = self->n_objects + 1;
        }
    }
    if (pillar_blocks > 0) {
        obj = Room_create_object(self->room, pillar_piece(self->world, k), x, y, z, false, false,
                                 false);
        y = y + pillar_piece(self->world, k)->size_y;
        self->n_objects = self->n_objects + 1;
    }
    if (pillar_blocks == 1) self->bottom_obj = obj;
    Object *post = Room_create_object(self->room, door_piece(self->world, k), x, y, z, false, false,
                                      false);
    self->post_obj = post;
    if (pillar_blocks == 0) self->bottom_obj = post;
    if (mirrored == 0) {
        off_z = self->post2_off;
    } else {
        off_x = self->post2_off;
        side_code = 2;
    }
    self->base_y = y;
    Object_set_door_side(post, side_code + 1);
    Object *post2 = Room_create_object(self->room, door_piece(self->world, k + 2), off_x + x, y,
                                       off_z + z, false, false, false);
    Object_set_door_side(post2, side_code + 2);
    Room_create_object(self->room, door_piece(self->world, k + 4), x, self->post_h + y, z, false,
                       false, false);
    self->n_objects = self->n_objects + 3;
}

// 0x004014a0 Door_ctor_vertical: saidas 4 (teto) e 5 (chao), sem graficos.
Door *Door_ctor_vertical(Door *self, Room *room, int type, int ceiling) {
    self->room = room;
    self->vtbl = g_vtbl_Door;
    self->world = nullptr;
    self->type = type;
    self->elevation = ceiling;
    return self;
}

// 0x004014d0 Door_ctor: portas 0..3 com as pecas do World. Usa 8 fixo (nao room->tile).
Door *Door_ctor(Door *self, Room *room, World *world, int type, int p, int pillar_blocks, int q) {
    self->vtbl = g_vtbl_Door;
    self->room = room;
    self->world = world;
    self->type = type;
    self->elevation = pillar_blocks;
    if (type == 2 || type == 3) {
        self->ent_x = p * 8;
        self->x = p * 8;
        self->ent_z = q << 3;
        self->z = q << 3;
    } else if (type == 0) {
        int v = (room->width - p) * 8 - world->door_part[2][0]->size_z;
        self->ent_x = v;
        self->x = v;
        v = (room->depth - q) * 8 - world->depth;
        self->z = v;
        self->ent_z = v + world->depth;
    } else {
        self->x = (room->width - p) * 8 - world->depth;
        const int v = (room->depth - q) * 8 - world->door_part[2][0]->size_z;
        self->ent_z = v;
        self->z = v;
        self->ent_x = world->depth + self->x;
    }
    self->post_h = world->door_part[1][0]->size_y;
    self->post2_off = world->door_part[2][0]->size_z - world->door_part[1][0]->size_z;
    self->top_y = world->door_part[2][0]->size_y + self->post_h;
    self->width = world->door_part[2][0]->size_z;
    int build_side_mirrored;
    switch (type) {
    case 0: {
        Door_build_objects(self, self->x, pillar_blocks, self->z, 1, 1);
        const Object *o = self->post_obj;
        self->img_sx = (o->x - self->world->door_part[2][0]->size_x) - o->z;
        self->img_sy = ((o->z + o->x) >> 1) + self->world->image->h + o->y;
        return self;
    }
    case 1: {
        Door_build_objects(self, self->x, pillar_blocks, self->z, 1, 0);
        const Object *o = self->post_obj;
        self->img_sx = (o->x - self->world->door_part[2][0]->size_z) - o->z;
        self->img_sy = ((o->z + o->x) >> 1) + self->world->image->h + o->y;
        return self;
    }
    case 2:
        build_side_mirrored = 0;
        break;
    case 3:
        build_side_mirrored = 1;
        break;
    default:
        return self;
    }
    Door_build_objects(self, self->x, pillar_blocks, self->z, 0, build_side_mirrored);
    self->img_sx = 0;
    self->img_sy = 0;
    return self;
}

// ================================================================== leitura do registro

// 0x00402de0 room_peek_int
int room_peek_int(int offset) {
    int32_t v;
    std::memcpy(&v, g_room_blob + offset, sizeof v);
    return v;
}

// 0x00402df0 room_peek_bool
bool room_peek_bool(int offset) { return g_room_blob[offset] != 0; }

// 0x00402e00 room_read_int
int room_read_int() {
    const int v = room_peek_int(g_room_cursor);
    g_room_cursor = g_room_cursor + 4;
    return v;
}

// 0x00402e20 room_read_bool
bool room_read_bool() {
    const bool v = room_peek_bool(g_room_cursor);
    g_room_cursor = g_room_cursor + 1;
    return v;
}

// 0x00403c00 room_init_scroll
void room_init_scroll(Room *room) {
    g_view_ext_x = (g_screen_w - room->base_origin_x) + -4;
    g_view_ext_z = room->base_origin_x + -4;
    if (g_view_ext_x <= room->width_px) room->scroll_x = true;
    if (g_view_ext_z <= room->depth_px) room->scroll_z = true;
}

// 0x00403c50 load_room
Room *load_room(int id) {
    release_voice_slots();
    if (id == 0x2b0) {
        g_in_final_room = 1;
        g_frame_counter = 0;
    }
    if (g_room_visited[id] == 0) {
        g_room_visited[id] = 1;
        g_rooms_visited_count = g_rooms_visited_count + 1;
        // x87: fild visitadas / fild total * float 100.0 (0x430268), _ftol (trunca); precisao
        // de 53 bits = double.
        g_percent_explored = static_cast<int>(static_cast<double>(g_rooms_visited_count) /
                                              static_cast<double>(g_room_count) * 100.0);
    }
    int ceiling = 500;  // local_10: ROOM-Ceiling (altura das saidas 4/5)
    g_room_cursor = g_room_table[id];
    room_read_int();  // id do registro (descartado)
    const int origin_x = room_read_int();
    const int origin_y = room_read_int();
    Room *room = Room_ctor(new Room(), origin_x, origin_y);
    room->id = id;
    room->block_table = g_blocks;
    room->four_crowns = g_four_crowns != 0;
    room->sound_mode = g_sound_mode;
    room->sfx_volume = g_sfx_volume;

    // ---- geometria e piso
    const int w = room_read_int();
    const int d = room_read_int();
    const int phase_x = room_read_int();
    const int phase_z = room_read_int();
    room->floor_phase_z = phase_z;
    room->floor_phase_x = phase_x;
    // original: sem efeito (Room_ctor ja deixou base_origin_x == origin_x == a).
    if ((w + d) * 8 < g_screen_w) room->base_origin_x = room->origin_x;
    const int f0 = room_read_int();
    const int f1 = room_read_int();
    const int f2 = room_read_int();
    const int f3 = room_read_int();
    const int f4 = room_read_int();
    const int f5 = room_read_int();
    const int f6 = room_read_int();
    const int border = room_read_int();
    Room_set_border(room, border);
    const int floor_set = room_read_int();
    int n_items = room_read_int();
    Room_build_floor(room, w, d, g_mobiles[floor_set]);
    for (; n_items > 0; n_items--) {
        const int set = room_read_int();
        const int item_w = room_read_int();
        const int item_d = room_read_int();
        const int fx = room_read_int();
        const int fz = room_read_int();
        const int phase = room_read_int();
        if (set == -1) {
            Room_clear_floor_rect(room, fx, fz, item_w, item_d);
        } else {
            Room_place_floor_pattern(room, fx, fz, g_mobiles[set], phase);
        }
    }
    if (f0 == 0 && f1 == 0 && f2 == 0 && f3 == 0 && f4 == 0 && f5 == 0 && f6 == 0) {
        room->rectangular = true;
    } else {
        Room_apply_shape_cuts(room, f0, f4, f1, f5, f2, f3, f6);
    }

    // ---- paredes (o Named da parede 1 e lido e ignorado: as duas usam o da parede 0)
    for (int wall = 0; wall < 2; wall++) {
        const int named = room_read_int();
        if (wall == 0) {
            room->wall_named = named;
            Room_build_walls(room, g_named[named], 0);
        }
        for (int n = room_read_int(); n > 0; n--) {
            const int item_named = room_read_int();
            const int len = room_read_int();
            const int h = room_read_int();
            const int pos = room_read_int();
            const int hpos = room_read_int();
            const int phase = room_read_int();
            if (item_named == -1) {
                if (wall == 0) {
                    Room_clear_wall0_rect(room, pos, hpos, len, h);
                } else {
                    Room_clear_wall1_rect(room, pos, hpos, len, h);
                }
            } else if (wall == 0) {
                Room_place_wall0_pattern(room, pos, hpos, g_named[item_named], phase);
            } else {
                Room_place_wall1_pattern(room, pos, hpos, g_named[item_named], phase);
            }
        }
    }
    // original: os recortes sao aplicados de novo (Room_build_walls refez as paredes).
    if (f0 == 0 && f1 == 0 && f2 == 0 && f3 == 0 && f4 == 0 && f5 == 0 && f6 == 0) {
        room->rectangular = true;
    } else {
        Room_apply_shape_cuts(room, f0, f4, f1, f5, f2, f3, f6);
    }

    // ---- personagens e rosquinha (objects[0..2])
    Object *obj = Room_create_object(room, g_blocks[0], 0x1000, 0x1000, 0x1000, true, true, true);
    Room_attach_light(room, obj, false, 0, g_blocks[0]->size_x >> 2, g_blocks[0]->size_y >> 2,
                      g_blocks[0]->size_z >> 2);
    room->light_on[0] = false;
    obj = Room_create_object(room, g_blocks[1], 0x1000, 0x1000, 0x1000, true, true, true);
    // original: o deslocamento da luz de Heels usa as dimensoes do bloco de Head (g_blocks[0]).
    Room_attach_light(room, obj, false, 0, g_blocks[0]->size_x >> 2, g_blocks[0]->size_y >> 2,
                      g_blocks[0]->size_z >> 2);
    room->light_on[1] = false;
    room->dark = false;
    obj = Room_create_object(room, g_blocks[6], -0x1000, 0x1000, 0x1000, false, false, false);
    room->projectile = Room_add_ctrl_simple(room, obj, CTRL_PROJECTILE);
    obj->snd_event = sound_sample(0xb);
    obj->snd_event_cat = get_sample_category(0xb);

    // ---- objetos do registro
    const int n_objects = room_read_int();
    for (int rec = 0; rec < n_objects; rec++) {
        const int block_index = room_read_int();
        const int x = room_read_int();
        const int y = room_read_int();
        const int z = room_read_int();
        const bool alt = room_read_bool();
        Block *block = alt ? g_blocks_alt[block_index] : g_blocks[block_index];
        obj = Room_create_object(room, block, x, y, z, false, false, false);
        obj->record_index = rec;
        const int glow_image = room_read_int();
        const int glow_x = room_read_int();
        const int glow_y = room_read_int();
        if (glow_image != -1) {
            Room_add_glow(room, obj, 1, glow_x, glow_y, 0xff, g_images[glow_image]);
        }
        const bool has_light = room_read_bool();
        const int light_radius = room_read_int();
        const bool light_flicker = room_read_bool();
        if (has_light) {
            Room_attach_light(room, obj, light_flicker, light_radius, block->size_x >> 2,
                              block->size_y >> 2, block->size_z >> 2);
        }
        Behaviour *beh = Behaviour_ctor(new Behaviour());
        room_read_bool();  // "inherit" (ignorado)
        const bool use_default = room_read_bool();
        if (!use_default) {
            int count = room_read_int();
            beh->count = count;
            for (int a = 0; count > 0; a++, count--) {
                beh->attr_id[a] = room_read_int();
                for (int k = 0; k < kBehaviourParams; k++) beh->params[a][k] = room_read_int();
            }
        } else {
            if (beh != nullptr) Behaviour_destroy(beh);
            beh = g_block_behaviours[block_index];
        }

        // switch de 35 casos (0x404306, tabela 0x404b90): indice = g_attr_map[id do arquivo].
        for (int a = 0; a < beh->count; a++) {
            const int *p = beh->params[a];
            switch (g_attr_map[beh->attr_id[a]]) {
            case ATTR_OBJ_GRAVITY:
                obj->gravity = true;
                break;
            case ATTR_OBJ_PUSHABLE:
                obj->pushable = true;
                break;
            case ATTR_OBJ_STICKY:
                obj->sticky = true;
                break;
            case ATTR_OBJ_ANIMATED:
                Sprite_play_anim(obj, p[0], p[1], p[2]);
                break;
            case ATTR_OBJ_KILLER:
                obj->killer = true;
                break;
            case ATTR_MOBILE_RANDOM:
                Room_add_ctrl_wanderer(room, obj, CTRL_RANDOM, p[0], p[1] == 1, p[2], p[3],
                                       p[4] == 1);
                break;
            case ATTR_ROOM_CEILING:
                ceiling = p[0];
                break;
            case ATTR_MOBILE_MOVER: {
                const int ramp_dist = p[4] * p[1];
                Room_add_ctrl_mover(room, obj, CTRL_MOVER, p[0], p[1], p[4], p[3] - ramp_dist,
                                    p[2] + ramp_dist, p[5], p[6] == 1, p[7], p[8] == 1,
                                    p[9] == 1);
                break;
            }
            case ATTR_OBJ_SPRING:
                Room_add_ctrl_simple(room, obj, CTRL_SPRING);
                break;
            case ATTR_OBJ_CONVEYOR:
                Object_set_conveyor(obj, p[0], p[1]);
                break;
            case ATTR_ROOM_KILLER_FLOOR:
                room->killer_floor = true;
                break;
            case ATTR_OBJ_TRAP:
                Room_add_ctrl_trap(room, obj, CTRL_TRAP, p[0] == 1, p[2], p[1] == 1, p[3] == 1,
                                   p[4] == 1);
                obj->snd_event = sound_sample(0xb);
                obj->snd_event_cat = get_sample_category(0xb);
                obj->trap = true;
                break;
            case ATTR_OBJ_TELEPORTER:
                Room_add_ctrl_teleporter(room, obj, CTRL_TELEPORTER, p[0], p[1], p[2], p[3], p[4],
                                         p[5]);
                obj->snd_move = sound_sample(0x26);
                obj->snd_move_cat = get_sample_category(0x26);
                break;
            case ATTR_MOBILE_PUSHER:
                Room_add_ctrl_pusher(room, obj, CTRL_PUSHER, p[0], p[1] == 1, p[2], p[3] == 1, p[4]);
                break;
            case ATTR_OBJ_PICKABLE:
                obj->pickable = true;
                break;
            case ATTR_OBJ_EXTRA:
                Room_add_ctrl_extra(room, obj, CTRL_EXTRA, p[0], p[1]);
                break;
            case ATTR_OBJ_FREEZABLE:
                obj->freezable = true;
                obj->freeze_stops_anim = p[0] == 1;
                break;
            case ATTR_OBJ_JOYSTICK:
                Room_add_ctrl_joystick(room, obj, CTRL_JOYSTICK, p[0], p[1], p[2]);
                break;
            case ATTR_MOBILE_CONTROLLED:
                obj->channel = p[0];
                obj->ctl_anim_mode = p[1];
                obj->ctl_toggles_anim = p[2] == 1;
                break;
            case ATTR_OBJ_SWITCH:
                Room_add_ctrl_switch(room, obj, CTRL_SWITCH, p[0], p[1] == 1, false);
                obj->snd_bump = sound_sample(4);
                obj->snd_bump_cat = get_sample_category(4);
                break;
            case ATTR_MOBILE_FOLLOWER:
                Room_add_ctrl_follower(room, obj, CTRL_FOLLOWER, p[0], p[1] == 1, p[2], p[3] == 1,
                                       p[4] == 1, p[5], p[6] == 1);
                break;
            case ATTR_MOBILE_HIT_AND_TURN:
                Room_add_ctrl_wanderer(room, obj, CTRL_HIT_AND_TURN, p[0], p[1] == 1, p[2], p[3],
                                       p[4] == 1);
                break;
            case ATTR_OBJ_DISAPPEAR:
                Room_add_ctrl_extra(room, obj, CTRL_DISAPPEAR, p[0], p[1]);
                break;
            case ATTR_MOBILE_HOOVER:
                Room_add_ctrl_switch(room, obj, CTRL_HOOVER, p[1], p[0] == 1, false);
                break;
            case ATTR_OBJ_FLICKER:
                Room_add_ctrl_flicker(room, obj, CTRL_FLICKER, p[0] == 1, p[1] == 1, p[2], p[3],
                                      p[4] == 1);
                break;
            case ATTR_ROOM_SNOWING:
                room->snowing = true;
                room->weather_amount = p[0];
                break;
            case ATTR_ROOM_RAIN:
                room->raining = true;
                room->weather_amount = p[0];
                break;
            case ATTR_ROOM_DARK:
                room->dark = true;
                break;
            case ATTR_MOBILE_SLIDER:
                Room_add_ctrl_wanderer(room, obj, CTRL_SLIDER, p[0], false, p[3], p[1], p[2] == 1);
                break;
            case ATTR_MOBILE_GLIDER:
                Room_add_ctrl_switch(room, obj, CTRL_GLIDER, p[0], p[1] == 1, p[2] == 1);
                break;
            case ATTR_ROOM_UNDER_WATER:
                room->underwater = true;
                break;
            case ATTR_ROOM_VERTICAL_SCROLL:
                room->vertical_scroll = true;
                break;
            case ATTR_MOBILE_SOUND:
                if (p[0] != 0) {
                    obj->snd_ambient = sound_sample(g_sound_index_map[p[0]]);
                    obj->snd_ambient_cat = get_sample_category(g_sound_index_map[p[0]]);
                }
                if (p[1] != 0) {
                    obj->snd_move = sound_sample(g_sound_index_map[p[1]]);
                    obj->snd_move_cat = get_sample_category(g_sound_index_map[p[1]]);
                }
                if (p[2] != 0) {
                    obj->snd_bump = sound_sample(g_sound_index_map[p[2]]);
                    obj->snd_bump_cat = get_sample_category(g_sound_index_map[p[2]]);
                }
                if (p[3] != 0) {
                    obj->snd_event = sound_sample(g_sound_index_map[p[3]]);
                    obj->snd_event_cat = get_sample_category(g_sound_index_map[p[3]]);
                }
                break;
            case ATTR_OBJ_TRANSPARENT:
                obj->transparent = true;
                obj->alpha = p[0];
                break;
            case ATTR_MOBILE_FOLLOWER_EXTRA:
                Room_add_ctrl_follower_extra(room, obj, CTRL_FOLLOWER_EXTRA, p[0], p[1] == 1, p[2],
                                             p[3] == 1, p[4] == 1, p[5], p[6] == 1, p[7], p[8]);
                break;
            default:
                break;
            }
        }
        if (!use_default && beh != nullptr) Behaviour_destroy(beh);
    }

    // ---- religa joysticks (tipo 9) e interruptores desligados (tipo 10) pelo canal
    for (int c = 0, n = room->n_controllers; n > 0; c++, n--) {
        if (room->controllers[c]->type == CTRL_JOYSTICK) {
            const int channel = room->controllers[c]->p28;
            for (int i = 0; i < room->n_objects; i++) {
                Object *o = room->objects[i];
                if (o->channel == channel) room->controllers[c]->obj->joystick_target = o;
            }
        }
        Controller *ctrl = room->controllers[c];
        if (ctrl->type == CTRL_SWITCH && !ctrl->state) {
            const int channel = ctrl->p28;
            for (int i = 0; i < room->n_objects; i++) {
                Object *o = room->objects[i];
                if (o->channel == channel) {
                    o->active = false;
                    if (o->ctl_toggles_anim) Sprite_stop_anim(o);
                }
            }
        }
    }

    // ---- remove os objetos ja recolhidos nesta sala (de tras para frente)
    for (int k = 0; k < g_room_taken_count[id]; k++) {
        const int taken = g_room_taken_ids[id][k];
        for (int i = room->n_objects - 1; i >= 0; i--) {
            Object *o = room->objects[i];
            if (o->record_index == taken) {
                for (int c = 0; c < room->n_controllers; c++) {
                    if (room->controllers[c]->obj == o) room->controllers[c]->disabled = true;
                }
                Room_destroy_object(room, room->objects[i]);
            }
        }
    }
    // Sala final: tira 5 - g_crowns objetos a partir de objects[36].
    if (g_in_final_room != 0) {
        for (int n = 5 - g_crowns; n > 0; n--) Room_destroy_object(room, room->objects[36]);
    }

    // ---- saidas: 6 lados x 2 entradas (tipo, mundo, sala_dest, porta_dest, p, altura, q)
    for (int side = 6; side != 0; side--) {
        room_read_int();  // n de portas usadas no lado (ignorado)
        for (int j = 2; j != 0; j--) {
            const int type = room_read_int();
            const int world = room_read_int();
            const int dest_room = room_read_int();
            const int dest_door = room_read_int();
            const int p = room_read_int();
            const int pillar = room_read_int();
            const int q = room_read_int();
            if (type == -1) continue;
            Door *door;
            if (type < 4) {
                door = Room_add_door(room, g_worlds[world], type, p, pillar, q);
            } else {
                door = Room_add_vertical_exit(room, type, ceiling);
                if (type == 4) {
                    room->has_up_exit = true;
                    room->up_exit_y = ceiling;
                } else if (type == 5) {
                    room->has_down_exit = true;
                }
            }
            door->dest_room = dest_room;
            door->dest_door = dest_door;
        }
    }

    // ---- luz ambiente e luzes fixas (raio, x, y, z, cintila)
    room->ambient = room_read_int();
    for (int n = room_read_int(); n > 0; n--) {
        const int radius = room_read_int();
        const int x = room_read_int();
        const int y = room_read_int();
        const int z = room_read_int();
        const bool flicker = room_read_bool();
        Room_add_light(room, x, y, z, flicker, radius);
    }
    room_init_scroll(room);
    return room;
}

// ================================================================== RoomBase / Room

// 0x00412e30 RoomBase_ctor
RoomBase *RoomBase_ctor(RoomBase *self) {
    self->vtbl = g_vtbl_RoomBase;
    RoomBase_init(self, 0x140, 0x1d6);
    return self;
}

// 0x00412e50 RoomBase_scalar_dtor
void *RoomBase_scalar_dtor(RoomBase *self, int flags) {
    RoomBase_dtor(self);
    if ((flags & 1) != 0) delete self;
    return self;
}

// 0x00412e70 RoomBase_init
void RoomBase_init(RoomBase *self, int origin_x, int origin_y) {
    self->origin_x = origin_x;
    self->origin_y = origin_y;
    self->n_objects = 0;
    self->dark = false;
    self->ambient = 0;
    self->n_lights = 0;
    for (int i = 0; i < kRoomMaxLights; i++) {
        self->light_flicker[i] = false;
        self->light_x[i] = 0;
        self->light_y[i] = 0;
        self->light_z[i] = 0;
        self->light_on[i] = false;
        self->light_radius[i] = 1;
    }
    for (int i = 0; i < kRoomCells; i++) {
        for (int j = 0; j < kRoomCells; j++) {
            self->floor_cell[i][j] = nullptr;
            self->floor_light[i][j] = 0;
            self->floor_height[i][j] = 0;
        }
    }
    clear_wall_grids(self);
    self->rectangular = true;
    self->owns_extra = false;
    self->n_glows = 0;
    self->border = 0;
    self->floor_phase_x = 0;
    self->floor_phase_z = 0;
}

// 0x00412f80 RoomBase_dtor
void RoomBase_dtor(RoomBase *self) {
    self->vtbl = g_vtbl_RoomBase;
    for (int i = 0; i < self->n_objects; i++) {
        if (self->objects[i] != nullptr) Object_destroy(self->objects[i]);
    }
    // original: se owns_extra, destroi extra[0..n_extra) pela vtable (classe desconhecida).
    // owns_extra so e escrito por RoomBase_init (= 0): codigo morto, nada a fazer no port.
}

// 0x00415f50 Room_set_border
void Room_set_border(Room *self, int border) { self->border = border; }

// 0x00415f60 Room_scalar_dtor
void *Room_scalar_dtor(Room *self, int flags) {
    Room_dtor(self);
    if ((flags & 1) != 0) delete self;
    return self;
}

void Room_destroy(Room *self) { Room_scalar_dtor(self, 1); }

// 0x00415f80 Room_ctor: RoomBase_ctor ja chama RoomBase_init(320, 470); o original chama de novo
// com (a, b).
Room *Room_ctor(Room *self, int origin_x, int origin_y) {
    RoomBase_ctor(self);
    self->base_origin_y = origin_y;
    self->vtbl = g_vtbl_Room;
    self->has_up_exit = false;
    self->has_down_exit = false;
    self->base_origin_x = origin_x;
    RoomBase_init(self, origin_x, origin_y);
    for (int i = 0; i < kRoomMaxLights; i++) self->light_obj[i] = nullptr;
    self->player_dir[1] = -1;
    self->player_dir[0] = -1;
    self->n_controllers = 0;
    self->n_doors = 0;
    self->killer_floor = false;
    self->snowing = false;
    self->raining = false;
    self->underwater = false;
    self->scroll_x = false;
    self->scroll_z = false;
    self->vertical_scroll = false;
    self->four_crowns = false;
    self->wall_named = 0;
    return self;
}

// 0x00416050 Room_dtor: destroi as portas (nao os controladores: vazamento do original).
void Room_dtor(Room *self) {
    self->vtbl = g_vtbl_Room;
    for (int i = 0; i < self->n_doors; i++) {
        if (self->doors[i] != nullptr) Door_destroy(self->doors[i]);
    }
    RoomBase_dtor(self);
}

// ================================================================== piso e paredes

// 0x00414150 Room_place_floor_pattern
void Room_place_floor_pattern(Room *self, int fx, int fz, FloorSet *set, int phase) {
    const int n = set->n;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            self->floor_cell[self->border + i + fx][self->border + j + fz] = set->tile_block[i][j];
            self->floor_anim_frame[self->border + i + fx][self->border + j + fz] = phase;
        }
    }
}

// 0x004141f0 Room_build_floor: ladrilha a grade desde 0 (inclui a borda) com o padrao n x n
// deslocado pelas fases; celulas com indice negativo sao puladas.
void Room_build_floor(Room *self, int w, int d, FloorSet *set) {
    const int phase_x = self->floor_phase_x;
    self->width = w;
    self->depth = d;
    const int n = set->n;
    const int phase_z = self->floor_phase_z;
    for (int r = 0; r < self->floor_phase_x + self->border + w; r += n) {
        for (int c = 0; c < self->border + self->floor_phase_z + d; c += n) {
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < n; j++) {
                    const int row = (i - phase_x) + r;
                    const int col = (j - phase_z) + c;
                    if (row > -1 && col > -1) {
                        self->floor_cell[row][col] = set->tile_block[i][j];
                        self->floor_anim_frame[row][col] = 0;
                        self->floor_anim_tick[row][col] = 0;
                        self->floor_anim_on[row][col] = true;
                    }
                }
            }
        }
    }
    self->tile = set->tile_block[0][0]->sprite_w >> 1;
    self->floor_cell_h = set->tile_block[0][0]->size_y;
    self->width_px = self->tile * w;
    self->depth_px = self->tile * d;
}

// 0x004143d0 Room_clear_floor_rect
void Room_clear_floor_rect(Room *self, int fx, int fz, int w, int d) {
    for (int row = self->border + fx; row < fx + self->border + w; row++) {
        for (int col = self->border + fz; col < fz + d + self->border; col++) {
            self->floor_cell[row][col] = nullptr;
        }
    }
}

// 0x00414460 Room_clear_wall0_rect (hpos sem a borda)
void Room_clear_wall0_rect(Room *self, int pos, int hpos, int len, int h) {
    for (int row = pos + self->border; row < pos + len + self->border; row++) {
        for (int k = hpos; k < h + hpos; k++) self->wall0_cell[row][k] = nullptr;
    }
}

// 0x004144e0 Room_clear_wall1_rect
void Room_clear_wall1_rect(Room *self, int pos, int hpos, int len, int h) {
    for (int row = pos + self->border; row < pos + len + self->border; row++) {
        for (int k = hpos; k < h + hpos; k++) self->wall1_cell[row][k] = nullptr;
    }
}

// 0x00414560 Room_place_wall0_pattern: celulas A do Named.
void Room_place_wall0_pattern(Room *self, int pos, int hpos, Named *named, int phase) {
    const int cols = named->cols;
    const int rows = named->rows;
    for (int i = 0; i < cols; i++) {
        int &height = self->wall0_height[i + pos + self->border];
        if (height < hpos + rows) height = hpos + rows;
        for (int k = 0; k < rows; k++) {
            const int row = i + pos + self->border;
            self->wall0_cell[row][k + hpos] = named->blocks_a[i][k];
            self->wall0_anim_frame[row][k + hpos] = phase;
            if (i + pos > -1) self->wall0_z[row][k + hpos] = self->depth_px;
        }
    }
}

// 0x00414670 Room_place_wall1_pattern: celulas B (espelhadas) do Named.
void Room_place_wall1_pattern(Room *self, int pos, int hpos, Named *named, int phase) {
    const int cols = named->cols;
    const int rows = named->rows;
    for (int i = 0; i < cols; i++) {
        int &height = self->wall1_height[i + pos + self->border];
        if (height < hpos + rows) height = hpos + rows;
        for (int k = 0; k < rows; k++) {
            const int row = i + pos + self->border;
            self->wall1_cell[row][k + hpos] = named->blocks_b[i][k];
            self->wall1_anim_frame[row][k + hpos] = phase;
            if (i + pos > -1) self->wall1_x[row][k + hpos] = self->width_px;
        }
    }
}

// 0x00414780 Room_build_walls
void Room_build_walls(Room *self, Named *named, int base_h) {
    clear_wall_grids(self);
    const int cols = named->cols;
    const int rows = named->rows;
    for (int x = 0; x < self->width; x += cols) {
        for (int i = 0; i < cols; i++) {
            self->wall0_height[self->border + i + x] = rows + base_h;
            for (int k = 0; k < rows; k++) {
                const int row = self->border + i + x;
                self->wall0_z[row][k] = self->depth_px;  // original: sem + base_h
                self->wall0_cell[row][k + base_h] = named->blocks_a[i][k];
                self->wall0_anim_frame[row][k + base_h] = 0;
                self->wall0_anim_tick[row][k + base_h] = 0;
                self->wall0_anim_on[row][k + base_h] = 1;
            }
        }
    }
    for (int z = 0; z < self->depth; z += cols) {
        for (int i = 0; i < cols; i++) {
            self->wall1_height[self->border + i + z] = rows + base_h;
            for (int k = 0; k < rows; k++) {
                const int row = self->border + i + z;
                self->wall1_x[row][k] = self->width_px;  // original: sem + base_h
                self->wall1_cell[row][k + base_h] = named->blocks_b[i][k];
                self->wall1_anim_frame[row][k + base_h] = 0;
                self->wall1_anim_tick[row][k + base_h] = 0;
                self->wall1_anim_on[row][k + base_h] = 1;
            }
        }
    }
}

// 0x00414d70 Room_apply_shape_cuts (ordem do registro: f0, f4, f1, f5, f2, f3, f6)
void Room_apply_shape_cuts(Room *self, int cut_a_w, int cut_a_d, int cut_b_w, int cut_b_d,
                           int notch_x0, int notch_x1, int notch_d) {
    self->rectangular = false;
    // canto A: piso [B + i][B + j]
    for (int i = 0; i < cut_a_w; i++) {
        for (int j = 0; j < cut_a_d; j++) self->floor_cell[self->border + i][self->border + j] = nullptr;
    }
    // canto B: piso [W + B - 1 - i][0 .. B + cut_b_d) e a coluna inteira da parede 0 na mesma
    // posicao (original: +0x180 / +0xbe9d0 = linha anterior; a coluna e apagada de novo a cada j).
    for (int i = 0; i < cut_b_w; i++) {
        for (int j = 0; j < self->border + cut_b_d; j++) {
            self->floor_cell[(self->width - i) + self->border - 1][j] = nullptr;
            for (int k = 0; k < kRoomCells; k++) {
                self->wall0_cell[(self->width - i) + self->border - 1][k] = nullptr;
            }
        }
    }
    // entalhe: linhas [notch_x1 (ou -B se 0) .. W - notch_x0), colunas B + D - 1 - i; com
    // notch_x1 == 0 tambem apaga a parede 1 nessas posicoes (+0x578 / +0xfe1d4 = linha anterior).
    for (int r = notch_x1 - (notch_x1 == 0 ? self->border : 0); r < self->width - notch_x0; r++) {
        for (int i = 0; i < notch_d; i++) {
            self->floor_cell[self->border + r][(self->border - i) + self->depth - 1] = nullptr;
            if (notch_x1 == 0) {
                for (int k = 0; k < kRoomCells; k++) {
                    self->wall1_cell[(self->border - i) + self->depth - 1][k] = nullptr;
                }
            }
        }
    }
    const int tile = self->tile;
    self->cut_a_x = self->width_px - tile * cut_a_w;
    self->cut_b_x = tile * cut_b_w;
    self->cut_a_z = self->depth_px - tile * cut_a_d;
    self->cut_b_z = self->depth_px - tile * cut_b_d;
    self->notch_x0 = tile * notch_x0;
    self->notch_x1 = self->width_px - tile * notch_x1;
    self->notch_z = tile * notch_d;
    for (int i = 0; i < cut_a_w; i++) {
        for (int k = 0; k < kRoomCells; k++) self->wall0_z[i + self->border][k] = self->cut_a_z;
    }
    for (int i = 0; i < cut_a_d; i++) {
        for (int k = 0; k < kRoomCells; k++) self->wall1_x[i + self->border][k] = self->cut_a_x;
    }
    // linhas da borda (0 .. B): recuo do canto B / do entalhe.
    for (int i = 0; i < self->border; i++) {
        for (int k = 0; k < kRoomCells; k++) self->wall1_x[i][k] += cut_b_w * self->tile;
    }
    if (notch_x1 == 0) {
        for (int i = 0; i < self->border; i++) {
            for (int k = 0; k < kRoomCells; k++) self->wall0_z[i][k] += notch_d * self->tile;
        }
    }
}

// ================================================================== luzes e portas

// 0x00415810 Room_add_light: sem limite (o original nao confere n_lights < 16).
int Room_add_light(Room *self, int x, int y, int z, bool flicker, int radius) {
    self->dark = true;
    self->light_x[self->n_lights] = x;
    self->light_y[self->n_lights] = y;
    self->light_z[self->n_lights] = z;
    self->light_flicker[self->n_lights] = flicker;
    self->light_radius[self->n_lights] = radius;
    self->light_on[self->n_lights] = true;
    const int index = self->n_lights;
    self->n_lights = index + 1;
    return index;
}

// 0x00416d00 Room_attach_light
void Room_attach_light(Room *self, Object *obj, bool flicker, int radius, int dx, int dy, int dz) {
    const Block *block = obj->block;
    dx = (block->size_x >> 1) + dx;
    dy = (block->size_y >> 1) + dy;
    dz = (block->size_z >> 1) + dz;
    const int index = Room_add_light(self, obj->x + dx, obj->y + dy, obj->z + dz, flicker, radius);
    self->light_obj[index] = obj;
    self->light_dx[index] = dx;
    self->light_dy[index] = dy;
    self->light_dz[index] = dz;
}

// 0x00416d80 Room_update_attached_lights
void Room_update_attached_lights(Room *self) {
    for (int i = 0; i < self->n_lights; i++) {
        const Object *obj = self->light_obj[i];
        if (obj != nullptr) {
            self->light_x[i] = obj->x + self->light_dx[i];
            self->light_y[i] = self->light_dy[i] + obj->y;
            self->light_z[i] = self->light_dz[i] + obj->z;
        }
    }
}

// 0x00417520 Room_add_door: sem limite (o original nao confere n_doors < 10).
Door *Room_add_door(Room *self, World *world, int type, int p, int pillar_blocks, int q) {
    self->doors[self->n_doors] = Door_ctor(new Door(), self, world, type, p, pillar_blocks, q);
    self->n_doors = self->n_doors + 1;
    return self->doors[self->n_doors - 1];
}

// 0x004175b0 Room_add_vertical_exit
Door *Room_add_vertical_exit(Room *self, int type, int ceiling) {
    self->doors[self->n_doors] = Door_ctor_vertical(new Door(), self, type, ceiling);
    self->n_doors = self->n_doors + 1;
    return self->doors[self->n_doors - 1];
}

}  // namespace hoh

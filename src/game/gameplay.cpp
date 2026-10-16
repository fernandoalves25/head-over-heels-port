// Modulo gameplay: estado 1 (jogo) — tick dos personagens (andar, pular, carregar, rosquinha,
// troca/juncao Head+Heels, extras, teleporte, portas, morte e vidas), troca de sala, jogo novo e
// luz dos personagens. Portado de re/decomp/game.c, conferido na desmontagem do HoH.exe.
#include "game/gameplay.h"

#include "game/controllers.h"
#include "game/globals.h"
#include "game/physics.h"
#include "game/room_load.h"
#include "game/room_render.h"
#include "game/screens.h"
#include "game/sound.h"
#include "game/water.h"

namespace hoh {

// ------------------------------------------------------------------ inicializadores dinamicos
// 0x00404cb0/0x00404cc0, 0x00404ce0/0x00404cf0, 0x00404da0/0x00404db0, 0x00404dc0/0x00404dd0:
// os 4 inicializadores dinamicos do CRT (tabela __xc em 0x431004..0x431010, rodam antes de main;
// o Ghidra nao os listou como funcoes). Derivam de g_ticks_per_second (30 no .data) duracoes que
// o gerador de globais leu como 0 (BSS). g_ticks_per_second tem inicializacao constante, entao ja
// vale 30 quando este inicializador dinamico roda.
namespace {
struct GameplayDynamicInit {
    GameplayDynamicInit() {
        g_shield_bunny_ticks = g_ticks_per_second * 25;  // 0x00404cc0 escudo (extra 5): 750
        DAT_00a5a9a0 = g_ticks_per_second * 50;          // 0x00404cf0 luz (extra 8): 1500
        g_light_fade_ticks = g_ticks_per_second * 10;    // 0x00404db0 luz comeca a diminuir: 300
        g_carry_cooldown_ticks = g_ticks_per_second;     // 0x00404dd0 espera apos pegar/soltar: 30
    }
};
GameplayDynamicInit s_dynamic_init;

// port: raio da luz enquanto o timer esta abaixo de g_light_fade_ticks. Original (x87, precisao
// de 53 bits do MSVC): fild g_light_radius_full; fidiv g_light_fade_ticks; fimul timer; _ftol.
int light_radius_fading(int timer) {
    return static_cast<int>(static_cast<double>(g_light_radius_full) /
                            static_cast<double>(g_light_fade_ticks) * static_cast<double>(timer));
}
}  // namespace

// 0x00403a70 update_character_lights
void update_character_lights() {
    if (!g_char_rooms[g_active_room_slot]->dark) return;
    if (g_joined != 0) {
        // Juntos: a luz 0 apaga e a 1 (presa ao Heels/Head over Heels) soma os dois raios.
        g_char_rooms[g_active_room_slot]->light_on[0] = false;
        int sum = 0;
        for (int i = 0; i < 2; i++) {
            int r = 0;
            if (g_light_timer[i] > 1) {
                r = g_light_radius_full;
                if (g_light_timer[i] < g_light_fade_ticks) r = light_radius_fading(g_light_timer[i]);
            }
            sum += r;
        }
        if (sum > 1) {
            g_char_rooms[g_active_room_slot]->light_on[1] = true;
            g_char_rooms[g_active_room_slot]->light_dy[1] = g_blocks[2]->size_y >> 1;
            g_char_rooms[g_active_room_slot]->light_radius[1] = sum;
            return;
        }
        g_char_rooms[g_active_room_slot]->light_on[1] = false;
        return;
    }
    for (int i = 0; i < 2; i++) {
        Room *room = g_char_rooms[g_active_room_slot];
        if (g_char_room_id[i] == room->id && g_light_timer[i] >= 2) {
            int timer = g_light_timer[i];
            room->light_dy[i] = g_blocks[i]->size_y >> 1;
            g_char_rooms[g_active_room_slot]->light_on[i] = true;
            int r = g_light_radius_full;
            if (timer < g_light_fade_ticks) r = light_radius_fading(timer);
            g_char_rooms[g_active_room_slot]->light_radius[i] = r;
        } else {
            room->light_on[i] = false;
        }
    }
}

// 0x00404e40 other_character
int other_character() {
    return g_cur_char == 0;
}

// 0x00404e50 park_character
void park_character(int ch) {
    Room_try_place_object(g_char_rooms[g_active_room_slot], g_char_obj[ch], kOutOfRoom, kOutOfRoom,
                          kOutOfRoom);
}

// 0x00404e80 opposite_exit_type
int opposite_exit_type(int type) {
    switch (type) {
        case EXIT_WALL_ZMAX: return EXIT_SIDE_ZMIN;
        case EXIT_WALL_XMAX: return EXIT_SIDE_XMIN;
        case EXIT_SIDE_XMIN: return EXIT_WALL_XMAX;
        case EXIT_SIDE_ZMIN: return EXIT_WALL_ZMAX;
        case EXIT_CEILING: return EXIT_FLOOR;
        case EXIT_FLOOR: return EXIT_CEILING;
    }
    return -1;
}

// 0x00404ed0 place_character_at_entry
bool place_character_at_entry(int dest_door, int exit_type, int dy, int x, int z, int anim) {
    Room *room = g_char_rooms[g_active_room_slot];
    // Correcao de x/z nas saidas 4/5 e no teleporte: metade da diferenca de tamanho entre as salas
    // (zerada se |dif| >= 128).
    int ext_dx = (room->width_px - g_prev_room_ext_x) / 2;
    int ext_dz = (room->depth_px - g_prev_room_ext_z) / 2;
    if ((ext_dx < 0 ? -ext_dx : ext_dx) >= 0x80) ext_dx = 0;
    if ((ext_dz < 0 ? -ext_dz : ext_dz) >= 0x80) ext_dz = 0;
    // original: o quadro mostrado ao entrar e other_character() (1 para o Head, 0 para o Heels).
    int frame = other_character();
    int count = -1;
    int opp = opposite_exit_type(exit_type);
    Door *door = nullptr;
    int px, py, pz, panim;

    if (exit_type != -1) {
        room = g_char_rooms[g_active_room_slot];
        int n = room->n_doors;
        for (int i = 0; i < n; i++) {
            if (room->doors[i]->type == opp && ++count == dest_door) door = room->doors[i];
        }
        if (door != nullptr) {
            Object *pl = g_char_obj[g_cur_char];
            switch (opp) {
                case 0:
                    px = (door->width / 2 - pl->block->size_x / 2) + door->ent_x;
                    py = door->base_y + dy;
                    pz = door->ent_z - pl->block->size_z;
                    panim = DIR_NEG_Z;
                    break;
                case 1:
                    pz = (door->width / 2 - pl->block->size_z / 2) + door->ent_z;
                    py = door->base_y + dy;
                    px = door->ent_x - pl->block->size_x;
                    panim = DIR_NEG_X;
                    break;
                case 2:
                    px = door->ent_x;
                    panim = DIR_POS_X;
                    pz = (door->width / 2 - pl->block->size_z / 2) + door->ent_z;
                    py = door->base_y + dy;
                    break;
                case 3:
                    pz = door->ent_z;
                    panim = DIR_POS_Z;
                    px = (door->width / 2 - pl->block->size_x / 2) + door->ent_x;
                    py = door->base_y + dy;
                    break;
                case 4:
                    // Chega pela saida de teto (veio de baixo): desce a partir do teto ate caber.
                    px = ext_dx + x;
                    pz = ext_dz + z;
                    py = door->elevation - 1;
                    while (!Room_try_place_object(g_char_rooms[g_active_room_slot],
                                                  g_char_obj[g_cur_char], px, py, pz)) {
                        py--;
                    }
                    panim = anim;
                    break;
                case 5:
                    // Chega pela saida de chao (veio de cima): sobe a partir de y = 24 ate caber.
                    pz = ext_dz + z;
                    px = ext_dx + x;
                    py = 0x18;
                    while (!Room_try_place_object(g_char_rooms[g_active_room_slot],
                                                  g_char_obj[g_cur_char], px, py, pz)) {
                        py++;
                    }
                    panim = anim;
                    break;
                default:
                    // original: tipo fora de 0..5 usa dy em tudo (inalcancavel na pratica).
                    px = py = pz = panim = dy;
                    break;
            }
            goto place;
        }
    }
    // Teleporte (exit_type -1) ou porta de chegada inexistente: (x, dy, z), subindo ate caber.
    py = dy;
    while (!Room_try_place_object(g_char_rooms[g_active_room_slot], g_char_obj[g_cur_char], x, py,
                                  z)) {
        py++;
    }
    px = x;
    pz = z;
    panim = anim;

place:
    if (!Room_try_place_object(g_char_rooms[g_active_room_slot], g_char_obj[g_cur_char], px, py,
                               pz)) {
        return false;
    }
    Sprite_set_anim_frame(g_char_obj[g_cur_char], panim, frame);
    g_move_dir = -1;
    g_prev_dir = -1;
    g_move_delay = 0;
    g_move_bonus_dx = 0;
    g_move_bonus_dy = 0;
    g_move_bonus_dz = 0;
    return true;
}

// 0x004052e0 set_carried_object_suspended
void set_carried_object_suspended(bool suspend) {
    if (!suspend) {
        if (g_carried_light_on) {
            for (int i = 0; i < g_char_rooms[g_active_room_slot]->n_lights; i++) {
                Room *room = g_char_rooms[g_active_room_slot];
                if (room->light_obj[i] == g_carried_obj) room->light_on[i] = true;
            }
        }
        for (int i = 0; i < g_char_rooms[g_active_room_slot]->n_controllers; i++) {
            Controller *c = g_char_rooms[g_active_room_slot]->controllers[i];
            if (c->obj == g_carried_obj) c->disabled = false;
        }
        return;
    }
    g_carried_light_on = 0;
    for (int i = 0; i < g_char_rooms[g_active_room_slot]->n_lights; i++) {
        Room *room = g_char_rooms[g_active_room_slot];
        if (room->light_obj[i] == g_carried_obj && room->light_on[i]) {
            g_carried_light_on = 1;
            room->light_on[i] = false;
        }
    }
    for (int i = 0; i < g_char_rooms[g_active_room_slot]->n_controllers; i++) {
        Controller *c = g_char_rooms[g_active_room_slot]->controllers[i];
        if (c->obj == g_carried_obj) c->disabled = true;
    }
}

// 0x00405470 reset_idle_animation
void reset_idle_animation() {
    g_idle_delay = 100;
    g_idle_timer = 0;
    g_idle_state = 0;
}

// 0x00405490 change_room
void change_room(int door_index, Controller *teleporter) {
    int anim = g_char_obj[g_cur_char]->anim;
    int x = 0;
    int z = 0;
    stop_voice_slot(g_walk_voice);
    if (anim == 5) anim = g_char_obj[g_cur_char]->frame < 4;  // ocioso: olhando para 0 ou 1
    reset_idle_animation();
    int entry_dy = 0;  // altura relativa a base da porta (descontada da posicao de entrada salva)
    Room *room = g_char_rooms[g_active_room_slot];
    g_prev_room_ext_x = room->width_px;
    g_prev_room_ext_z = room->depth_px;

    int dest_room, dest_door, exit_type, dy;
    if (door_index == -1) {
        dest_door = -1;
        exit_type = -1;
        dy = teleporter->vy;
        dest_room = teleporter->p28 + teleporter->p2c * 40;
        x = teleporter->vx;
        z = teleporter->vz;
    } else {
        Door *door = room->doors[door_index];
        exit_type = door->type;
        if (exit_type < EXIT_CEILING) {
            entry_dy = g_char_obj[g_cur_char]->y - door->base_y;
        } else {
            x = g_char_obj[g_cur_char]->x;
            z = g_char_obj[g_cur_char]->z;
        }
        dest_room = door->dest_room;
        if (dest_room == -1) return;
        dest_door = door->dest_door;
        dy = entry_dy;
    }

    int other = other_character();
    int saved_slot = g_active_room_slot;
    Room *weather_room;
    if (g_char_room_id[0] == g_char_room_id[1] && g_joined == 0) {
        // (a) Os dois na mesma sala, separados: quem sai estaciona o objeto e a sala fica para o
        // outro.
        park_character(g_cur_char);
        if (g_carrying && g_cur_char == 1) {
            while (!Room_try_place_object(g_char_rooms[g_active_room_slot], g_carried_obj,
                                          g_carry_origin_x, g_carried_orig_y, g_carried_orig_z)) {
                g_carried_orig_y++;
            }
            g_carried_obj->standing_on = nullptr;
            g_carrying = 0;
            set_carried_object_suspended(false);
        }
        if (other == 1) g_char_rooms[1] = g_char_rooms[0];
        Room *r = load_room(dest_room);
        g_active_room_slot = g_cur_char;
        g_char_rooms[g_cur_char] = r;
        g_char_room_id[g_cur_char] = dest_room;
        g_char_obj[g_cur_char] = r->objects[g_cur_char];
        Room_set_active_player(r, g_char_obj[g_cur_char]);
        place_character_at_entry(dest_door, exit_type, dy, x, z, anim);
        weather_room = g_char_rooms[g_cur_char];
    } else if (g_char_room_id[other] == dest_room) {
        // (b) Destino = sala do outro: reaproveita a sala carregada dele.
        Object *saved_obj = g_char_obj[g_cur_char];
        g_active_room_slot = other;
        g_char_obj[g_cur_char] = g_char_rooms[other]->objects[g_cur_char];
        if (!place_character_at_entry(dest_door, exit_type, dy, x, z, anim)) {
            g_active_room_slot = saved_slot;
            g_char_obj[g_cur_char] = saved_obj;
            play_sfx_throttled(0x13, &g_hitwall_last_frame);
            return;
        }
        if (g_cur_char == 1 && g_carrying) g_carrying = 0;
        if (g_char_rooms[g_cur_char] != nullptr) Room_destroy(g_char_rooms[g_cur_char]);
        g_char_rooms[g_cur_char] = nullptr;
        if (other == 1) {
            g_char_rooms[0] = g_char_rooms[1];
            g_char_rooms[1] = nullptr;
        }
        g_char_room_id[g_cur_char] = dest_room;
        g_active_room_slot = 0;
        g_char_obj[g_cur_char] = g_char_rooms[0]->objects[g_cur_char];
        Room_set_active_player(g_char_rooms[0], g_char_obj[g_cur_char]);
        goto save_entry;  // original: sem room_spawn_weather (a sala ja estava montada)
    } else {
        if (g_cur_char == 1 || g_joined != 0) {
            if (g_carrying) g_carrying = 0;
            if (g_joined != 0) {
                // (c) Juntos: recarrega a sala com os dois.
                if (g_char_rooms[0] != nullptr) Room_destroy(g_char_rooms[0]);
                g_char_rooms[0] = load_room(dest_room);
                g_char_obj[0] = g_char_rooms[0]->objects[0];
                g_char_obj[1] = g_char_rooms[0]->objects[1];
                g_char_room_id[0] = dest_room;
                g_char_room_id[1] = dest_room;
                Room_set_active_player(g_char_rooms[0], g_char_obj[1]);
                Sprite_set_block(g_char_obj[1], g_blocks[2]);
                place_character_at_entry(dest_door, exit_type, dy, x, z, anim);
                weather_room = g_char_rooms[0];
                goto weather;
            }
        }
        // (d) Caso geral: destroi a sala do personagem e carrega a nova.
        if (g_char_rooms[g_cur_char] != nullptr) Room_destroy(g_char_rooms[g_cur_char]);
        Room *r = load_room(dest_room);
        g_char_rooms[g_cur_char] = r;
        g_char_room_id[g_cur_char] = dest_room;
        g_char_obj[g_cur_char] = r->objects[g_cur_char];
        Room_set_active_player(r, g_char_obj[g_cur_char]);
        place_character_at_entry(dest_door, exit_type, dy, x, z, anim);
        weather_room = g_char_rooms[g_cur_char];
    }
weather:
    room_spawn_weather(weather_room);
save_entry : {
    // Posicao de entrada (usada para renascer na sala).
    int c = g_cur_char;
    Object *pl = g_char_obj[c];
    g_entry_x[c] = pl->x;
    g_entry_y[c] = pl->y - entry_dy;
    g_entry_z[c] = pl->z;
    g_entry_anim[c] = pl->anim;
    g_joined_at_entry = g_joined != 0;
    if (g_joined_at_entry) {
        g_entry_x[0] = pl->x;
        g_entry_y[0] = pl->y - entry_dy;
        g_entry_z[0] = pl->z;
        g_entry_anim[0] = pl->anim;
    }
    g_jumping = 0;
    g_char_obj[0]->gravity = true;
    g_char_obj[1]->gravity = true;
    start_screen_transition(1);
}
}

// 0x004059c0 dir_to_delta
void dir_to_delta(int dir, int *dx, int *dz) {
    *dx = 0;
    *dz = 0;
    switch (dir) {
        case DIR_NEG_X: *dx = -1; break;
        case DIR_NEG_Z: *dz = -1; return;
        case DIR_POS_X: *dx = 1; return;
        case DIR_POS_Z: *dz = 1; return;
    }
}

// 0x00405b00 new_game
void new_game() {
    for (int i = 0; i < 1600; i++) g_room_taken_count[i] = 0;
    for (int i = 0; i < 1600; i++) g_room_visited[i] = 0;
    g_active_room_slot = 0;
    g_rooms_visited_count = 0;
    g_char_rooms[0] = load_room(0x4f);  // Head: sala 79
    g_char_rooms[1] = load_room(0x27);  // Heels: sala 39
    g_cur_char = 0;
    g_char_room_id[0] = g_char_rooms[0]->id;
    g_char_room_id[1] = g_char_rooms[1]->id;
    g_joined = 0;
    g_char_obj[0] = g_char_rooms[0]->objects[0];
    g_char_obj[1] = g_char_rooms[1]->objects[1];
    Sprite_set_anim_frame(g_char_obj[0], 0, 1);
    Sprite_set_anim_frame(g_char_obj[1], 0, 1);
    Room_set_active_player(g_char_rooms[0], g_char_obj[0]);
    Room_set_active_player(g_char_rooms[1], g_char_obj[1]);
    Room_try_place_object(g_char_rooms[0], g_char_obj[0], 0x74, 0, 100);
    Room_try_place_object(g_char_rooms[1], g_char_obj[1], 0x70, 0, 0x46);
    Room_update_camera(g_char_rooms[g_active_room_slot], g_view_ext_x, g_view_ext_z);
    g_move_delay = 0;
    g_move_dir = -1;
    g_held_dir = -1;
    g_prev_dir = -1;
    g_move_bonus_dx = 0;
    g_move_bonus_dy = 0;
    g_move_bonus_dz = 0;
    g_jumping = 0;
    g_jump_power = 1;
    g_hit_room_edge = 0;
    g_teleporting = 0;
    g_hud_wipe = -1;
    g_carry_latch = 0;
    g_carrying = 0;
    g_carry_cooldown = 0;
    g_lives[0] = 8;
    g_lives[1] = 8;
    g_head_speed_timer = 0;
    g_heels_big_jumps = 0;
    g_shield_timer[0] = 0;
    g_shield_timer[1] = 0;
    g_has_hooter = 0;
    g_donuts = 0;
    g_has_bag = 0;
    g_fire_latch = 0;
    g_fish_count = 0;
    g_crowns = 0;
    g_dying = 0;
    g_death_frame = 0;
    for (int i = 0; i < 2; i++) {
        Object *pl = g_char_obj[i];
        g_entry_x[i] = pl->x;
        g_entry_y[i] = pl->y;
        g_entry_z[i] = pl->z;
        g_entry_anim[i] = pl->anim;
    }
    g_joined_at_entry = 0;
    g_transition_active = 0;
    g_light_timer[0] = 0;
    g_light_timer[1] = 0;
    g_jumpcarry_held = 0;
    DAT_00a5bc32 = 1;  // extras "coelho" tambem entram na lista de pegos da sala
    g_four_crowns = 0;
    g_percent_explored = 0;
    DAT_0091e470 = 0;  // trava do SWAP (byte no original)
    room_spawn_weather(g_char_rooms[g_active_room_slot]);
    g_life_transfer_anim = 0;
    g_in_final_room = 0;
    g_jump_after_pickup = 0;
    reset_idle_animation();
    g_swap_mode = 0;
    g_rain_volume = 0;
    g_rain_target = 0;
}

// 0x00405d70 free_character_rooms
void free_character_rooms() {
    // original: nao zera g_char_rooms[] (o chamador volta ao menu; new_game/load_game sobrescrevem).
    for (int i = 0; i < 2; i++) {
        if (g_char_rooms[i] != nullptr) Room_destroy(g_char_rooms[i]);
    }
}

// 0x004074b0 update_state_game
void update_state_game() {
    Room *room;
    Object *pl;
    int moved;      // *moved de Room_move_object (objetos empurrados; somado em g_move_delay)
    int dir;        // direcao pedida neste tick (-1 nenhuma)
    bool walked;    // algum eixo andou livre neste tick (vira g_moved_last_frame)
    int hres;       // resultado do avanco horizontal do Heels no pulo
    int steps, dx, dz, n, r, cost;
    bool push;

    // 1. ESC/PAUSE: pausa (estado 3); no final do jogo vai ao resultado (estado 4).
    if (key[KEY_ESC] != 0 || g_input_pause != 0) {
        if (!g_in_final_room) {
            pause_menu_open();
            return;
        }
        gameover_screen_start();  // original: push 0 (argumento nao lido)
        return;
    }
    room = g_char_rooms[g_active_room_slot];
    pl = g_char_obj[g_cur_char];

    // 2. Final do jogo: personagens parados; a cada 151 ticks um objeto da festa (g_blocks[10]).
    if (g_in_final_room) {
        if (!g_teleporting) {
            Sprite_set_anim_frame(room->objects[0], 1, 1);
            Sprite_set_anim_frame(room->objects[1], 1, 1);
        }
        if (g_frame_counter > 0x96) {
            g_frame_counter = 0;
            Object *o = Room_create_object(room, g_blocks[10], 0xaa, 0xe, 0x7c, false, false, false);
            Room_add_ctrl_pusher(room, o, CTRL_PUSHER, 4, true, 1, true, 0);
            o->contact = o;
            o->contact_dz = 1;
        }
    }

    // 3. Sala: controladores, fisica, camera, sons.
    Room_update_controllers(room);
    Room_update_physics(room);
    Room_update_camera(room, g_view_ext_x, g_view_ext_z);
    Room_update_object_sounds(room, g_frame_counter);

    // 4. Fim da queda: restaura a animacao salva.
    if (g_falling) {
        Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
        if (g_saved_anim_playing) Sprite_play_anim(pl, g_saved_anim, g_saved_frame, -1);
        g_falling = 0;
    }

    // 5. Timers de escudo e de luz.
    for (int i = 0; i < 2; i++) {
        if (g_shield_timer[i] > 0) g_shield_timer[i]--;
        if (g_light_timer[i] > 0) g_light_timer[i]--;
    }

    // 6. Teletransporte em curso.
    if (g_teleporting) {
        if (!g_teleport_out) {
            g_teleport_timer++;
            if (g_teleport_timer >= (pl->block->frame_count[4] / 2) * 3) {
                g_teleporting = 0;
                Sprite_stop_anim(pl);
                Sprite_set_anim_frame(pl, g_teleport_saved_anim, 1);
                Room_animate(room);
                return;
            }
        } else {
            g_teleport_timer++;
            if (g_teleport_timer >= (pl->block->frame_count[4] / 2) * 3 - 3) {
                g_teleport_out = 0;
                g_teleport_timer = 0;
                g_teleport_ctrl->state = false;
                Sprite_stop_anim(g_teleport_ctrl->obj);
                Sprite_set_anim_frame(g_teleport_ctrl->obj, 0, 0);
                Sprite_set_anim_frame(pl, g_teleport_saved_anim, 1);
                Room_animate(room);
                change_room(-1, g_teleport_ctrl);
                Object *np = g_char_obj[g_cur_char];
                Sprite_play_anim(np, 4, np->block->frame_count[4] / 2, 2);
                return;
            }
        }
        Room_animate(room);
        return;
    }

    // 7. Morte.
    if (g_dying) {
        int who = g_char_obj[1]->touched_killer ? 1 : 0;
        if (g_death_frame == 0) {
            play_sfx(10);
            g_death_frame++;
            Sprite_play_anim(g_char_obj[who], 4, 0, -1);
            Room_animate(room);
            return;
        }
        Block *b = g_char_obj[who]->block;
        if (g_death_frame < (b->frame_count[4] / 2) * b->anim_speed[4]) {
            g_death_frame++;
            Room_animate(room);
            return;
        }
        other_character();  // original: resultado descartado
        g_dying = 0;
        cost = 1;
        stop_voice_slot(g_walk_voice);
        if (!g_cheat) {
            if (g_joined == 0) {
                cost = 2;
                g_lives[who]--;
            } else {
                g_lives[0]--;
                g_lives[1]--;
            }
        }
        // Transferencia de vidas: quem ficou com 0 recebe 1 se o outro tiver mais que `cost`.
        if (g_lives[0] == 0) {
            if (cost < g_lives[1]) {
                g_lives[1] -= cost;
                g_lives[0] = 1;
                g_life_transfer_anim = 1;
                g_life_transfer_from = 1;
                g_life_transfer_amount = cost;
                goto restart;
            }
            if (g_lives[1] != 0) goto game_over;
        } else if (g_lives[1] != 0) {
            goto restart;
        }
        if (cost < g_lives[0]) {
            g_lives[0] -= cost;
            g_lives[1] = 1;
            g_life_transfer_anim = 1;
            g_life_transfer_from = 0;
            g_life_transfer_amount = cost;
            goto restart;
        }
        goto game_over;

    restart : {
        // Recarrega a sala e recoloca nas posicoes de entrada (subindo y ate caber).
        Room *old = g_char_rooms[g_active_room_slot];
        int id = old->id;  // original: le o id antes de testar NULL
        if (old != nullptr) Room_destroy(old);
        Room *nr = load_room(id);
        g_char_rooms[g_active_room_slot] = nr;
        if (g_char_room_id[0] == g_char_room_id[1]) {
            g_carrying = 0;
            for (int ch = 1; ch >= 0; ch--) {
                Object *o = nr->objects[ch];
                g_char_obj[ch] = o;
                if (!g_joined_at_entry || ch == 1) {
                    int ex = g_entry_x[ch];
                    int ey = g_entry_y[ch];
                    int ez = g_entry_z[ch];
                    while (!Room_try_place_object(nr, o, ex, ey, ez)) ey++;
                }
                Sprite_invalidate(o);
                Sprite_set_anim_frame(o, g_entry_anim[ch], 1);
                g_jumping = 0;
                o->gravity = true;
                g_char_room_id[ch] = id;
                g_prev_dir = -1;
            }
            Room_set_active_player(nr, g_char_obj[g_cur_char]);
            if (!g_joined_at_entry) {
                g_joined = 0;
            } else {
                Object *heels = g_char_obj[1];
                g_joined = 1;
                g_cur_char = 1;
                g_char_obj[0]->gravity = true;
                g_char_obj[1]->gravity = true;
                g_prev_dir = -1;
                Room_set_active_player(nr, heels);
                g_char_obj[0]->x = kOutOfRoom;
                g_char_obj[0]->y = kOutOfRoom;
                g_char_obj[0]->z = kOutOfRoom;
                Sprite_invalidate(g_char_obj[0]);
                Sprite_set_block(g_char_obj[1], g_blocks[2]);
            }
        } else {
            if (g_cur_char == 1) g_carrying = 0;
            Object *o = nr->objects[g_cur_char];
            int ex = g_entry_x[g_cur_char];
            int ey = g_entry_y[g_cur_char];
            g_char_obj[g_cur_char] = o;
            int ez = g_entry_z[g_cur_char];
            while (!Room_try_place_object(nr, o, ex, ey, ez)) ey++;
            Sprite_invalidate(o);
            Sprite_set_anim_frame(o, g_entry_anim[g_cur_char], 1);
            g_jumping = 0;
            o->gravity = true;
            g_prev_dir = -1;
            Room_set_active_player(nr, o);
            g_char_room_id[g_cur_char] = id;
        }
        room_spawn_weather(g_char_rooms[g_active_room_slot]);
        start_screen_transition(-1);
        if (g_joined == 0) {
            g_swap_mode = g_cur_char;
            Room_animate(nr);
            return;
        }
        g_swap_mode = 2;
        Room_animate(nr);
        return;
    }
    game_over:
        gameover_screen_start();  // original: push 0
        return;
    }

    // 8. SWAP: ciclo g_swap_mode 0 (Head) -> 1 (Heels) -> 2 (tenta juntar) -> 0.
    g_idle_timer++;
    DAT_00529080 = 0;  // trocar para o Head
    DAT_00529081 = 0;  // trocar para o Heels
    DAT_00529082 = 0;  // tentativa de juntar
    if (g_input_swap == 0) {
        DAT_0091e470 = 0;
    } else if (DAT_0091e470 == 0) {
        DAT_0091e470 = 1;
        if (g_char_room_id[0] != g_char_room_id[1]) g_hud_wipe = -1;
        if (g_swap_mode == 0) {
            DAT_00529081 = 1;
            g_swap_mode = 1;
        } else if (g_swap_mode == 1) {
            DAT_00529082 = 1;
            g_swap_mode = 2;
            if (g_char_room_id[0] == g_char_room_id[1]) {
                if (g_joined != 0) goto lights;
                if (object_stands_on(g_char_obj[0], g_char_obj[1])) {
                    if (g_idle_state != 0) Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
                    reset_idle_animation();
                    r = Room_move_object(room, g_char_obj[0], g_char_obj[1]->x - g_char_obj[0]->x, 0,
                                         g_char_obj[1]->z - g_char_obj[0]->z, false, &moved);
                    if (r == -1) {
                        // Head em cima do Heels e coube na posicao dele: Head over Heels.
                        Object *heels = g_char_obj[1];
                        g_joined = 1;
                        g_cur_char = 1;
                        g_jumping = 0;
                        g_char_obj[0]->gravity = true;
                        g_char_obj[1]->gravity = true;
                        g_prev_dir = -1;
                        Room_set_active_player(room, heels);
                        g_char_obj[0]->x = kOutOfRoom;
                        g_char_obj[0]->y = kOutOfRoom;
                        g_char_obj[0]->z = kOutOfRoom;
                        Sprite_invalidate(g_char_obj[0]);
                        Sprite_set_block(g_char_obj[1], g_blocks[2]);
                        pl = heels;
                    }
                }
            }
            if (g_joined == 0) {
                DAT_00529080 = 1;
                g_swap_mode = 0;
            } else if (!DAT_00529080 && !DAT_00529081) {
                goto lights;
            }
        } else if (g_swap_mode == 2) {
            DAT_00529080 = 1;
            g_swap_mode = 0;
        } else {
            goto lights;
        }
        if (!g_in_final_room) {
            if (g_idle_state != 0) Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
            reset_idle_animation();
            if (g_joined != 0) {
                // Separa: Heels volta ao bloco 1 e o Head e posto em cima dele.
                g_joined = 0;
                Sprite_set_block(g_char_obj[1], g_blocks[1]);
                g_char_obj[0]->x = g_char_obj[1]->x;
                g_char_obj[0]->y = g_char_obj[0]->block->size_y + g_char_obj[1]->y;
                g_char_obj[0]->z = g_char_obj[1]->z;
                Sprite_set_anim_frame(g_char_obj[0], g_char_obj[1]->anim, 1);
                Room_move_object(room, g_char_obj[1], 0, -2, 0, false, &moved);
            }
            Object *prev;
            if (DAT_00529080) {
                g_active_room_slot = 0;
                g_cur_char = 0;
                Sprite_stop_anim(g_char_obj[1]);
                prev = g_char_obj[1];
            } else {
                g_active_room_slot = g_char_room_id[0] != g_char_room_id[1];
                g_cur_char = 1;
                Sprite_stop_anim(g_char_obj[0]);
                prev = g_char_obj[0];
            }
            Sprite_set_anim_frame(prev, prev->anim, 1);
            g_jumping = 0;
            room = g_char_rooms[g_active_room_slot];
            pl = g_char_obj[g_cur_char];
            g_char_obj[0]->gravity = true;
            g_char_obj[1]->gravity = true;
            g_prev_dir = -1;
            Room_set_active_player(room, pl);
            room_init_scroll(g_char_rooms[g_active_room_slot]);
        }
    }

lights:
    // 9. Luz dos personagens (sala escura).
    update_character_lights();
    g_hit_room_edge = 0;

    // 10. Pulo em curso.
    if (g_jumping) {
        hres = -1;
        pl->standing_on = nullptr;
        pl->gravity = false;
        int vdir = 1;
        n = g_jump_profile[g_cur_char - g_joined][g_jump_frame] * g_jump_power;
        if (n < 0) {
            vdir = -1;
            n = -n;
        }
        if (pl->contact != nullptr && pl->contact_dy < 0 && n > 1) {
            n = n / 2;
            if (n == 0) n = 1;
            pl->contact = nullptr;
        }
        g_move_result = -1;
        while (n > 0) {
            g_move_result = Room_move_object(room, pl, 0, vdir, 0, true, &moved);
            n--;
            if (g_move_result != -1) break;
        }
        if (g_cur_char == 1 && g_joined == 0) {
            // Heels sozinho tambem avanca 2 (3 com pulo forte) na direcao do olhar.
            dir_to_delta(pl->anim, &dx, &dz);
            hres = 0;
            steps = g_jump_power > 1 ? 3 : 2;
            push = (g_jump_frame + 2) % 3 == 0;
            while (steps != 0) {
                hres = Room_move_object(room, pl, steps * dx, 0, steps * dz, push, &moved);
                steps--;
                if (hres != -1) break;
            }
        }
        if (g_move_result == -2 || hres == -2) g_hit_room_edge = 1;
        bool stop;
        if (g_cur_char == 1 && g_joined == 0) {
            if (g_move_result == -1) {
                stop = hres != -1 && g_jump_frame > 11;
            } else {
                stop = hres != -1 || g_jump_frame > 11;
            }
        } else if (g_move_result != -1) {
            stop = true;
        } else {
            stop = hres != -1 && g_jump_frame > 11;  // original: hres e sempre -1 aqui
        }
        if (stop) {
            g_jumping = 0;
            pl->gravity = true;
        }
        g_jump_frame++;
        if (g_jump_frame == g_jump_length[g_cur_char - g_joined]) {
            g_jumping = 0;
            pl->gravity = true;
        }
        if (g_cur_char == 1 && g_joined == 0 && !g_jumping) {
            Sprite_stop_anim(pl);
            Sprite_play_anim(pl, pl->anim, 0, -1);
        }
    }

    // 11. Inicio de pulo (JUMP ou JUMP-CARRY), apoiado no chao ou num objeto em y-1/y-2.
    if (!g_carrying) {
        g_jumpcarry_held = 0;
        if (g_input_jumpcarry != 0) g_jumpcarry_held = 1;
    }
    if ((g_input_jump != 0 || g_jumpcarry_held) && !g_jumping &&
        (pl->y == 0 || g_jump_after_pickup ||
         !Room_find_collision(room, pl, pl->x, pl->y - 1, pl->z, &g_support_obj1) ||
         !Room_find_collision(room, pl, pl->x, pl->y - 2, pl->z, &g_support_obj2)) &&
        !g_in_final_room) {
        if (g_cur_char == 1 && g_joined == 0 && g_move_dir != -1) {
            Sprite_set_anim_frame(pl, g_move_dir, 0);
            g_prev_dir = -1;
            g_move_dir = -1;
            room->player_dir[g_cur_char] = -1;
            reset_idle_animation();
        }
        g_jumping = 1;
        Room_find_collision(room, pl, pl->x, pl->y - 1, pl->z, &g_support_obj1);
        Room_find_collision(room, pl, pl->x, pl->y - 2, pl->z, &g_support_obj2);
        if ((g_support_obj1 != nullptr && g_support_obj1->killer) ||
            (g_support_obj2 != nullptr && g_support_obj2->killer)) {
            pl->touched_killer = true;
        }
        if (g_support_obj1 != nullptr && g_support_obj1->trap) {
            g_support_obj1->contact = pl;
            g_support_obj1->contact_dy = -1;
        }
        if (g_support_obj2 != nullptr && g_support_obj2->trap) {
            g_support_obj2->contact = pl;
            g_support_obj2->contact_dy = -1;
        }
        // Nao se pula de cima de particula nem de teletransportador.
        if ((g_support_obj1 == nullptr ||
             (!g_support_obj1->particle && !g_support_obj1->teleporter)) &&
            (g_support_obj2 == nullptr ||
             (!g_support_obj2->particle && !g_support_obj2->teleporter))) {
            if (g_idle_state != 0) Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
            reset_idle_animation();
            if (g_jumpcarry_held && !g_carrying) g_carry_latch = 0;
            g_jump_frame = 0;
            if (!Room_on_free_spring(room, pl)) {
                if (!pl->touched_killer) play_sfx_on_voice(0x16, g_voice_jump);
                g_jump_power = 1;
            } else {
                if (!pl->touched_killer) play_sfx(0x24);
                g_jump_power = 2;
            }
            if (g_cur_char == 1 && g_joined == 0) {
                if (g_heels_big_jumps > 0) {
                    g_jump_power = g_jump_power * 2;
                    g_heels_big_jumps--;
                }
                Sprite_play_anim(pl, pl->anim, pl->frame, -1);
            }
        } else {
            g_jumping = 0;
        }
    }

    // 12. Direcao pedida (0 -x, 1 -z, 2 +x, 3 +z; vetor +-2).
    dir = -1;
    g_move_dx = 0;
    g_move_dy = 0;
    g_move_dz = 0;
    g_move_dir = -1;
    if (!g_in_final_room) {
        if (g_prev_dir == -1) {
            if (g_input_left != 0) {
                g_move_dz = 2;
                g_move_dir = 3;
                g_held_dir = 3;
                dir = 3;
            } else if (g_input_right != 0) {
                g_move_dz = -2;
                g_move_dir = 1;
                g_held_dir = 1;
                dir = 1;
            } else if (g_input[0] != 0) {
                g_move_dx = 2;
                g_move_dir = 2;
                g_held_dir = 2;
                dir = 2;
            } else if (g_input_down != 0) {
                g_move_dx = -2;
                g_move_dir = 0;
                g_held_dir = 0;
                dir = 0;
            }
        } else if (!g_csens_high) {
            // Sensibilidade baixa: mantem a direcao segurada; senao a primeira na ordem L, R, U, D.
            if (g_input_left != 0 && g_held_dir == 3) {
                g_move_dz = 2;
                dir = g_move_dir = g_held_dir;
            } else if (g_input_right != 0 && g_held_dir == 1) {
                g_move_dz = -2;
                dir = g_move_dir = g_held_dir;
            } else if (g_input[0] != 0 && g_held_dir == 2) {
                dir = g_move_dir = g_held_dir;
                g_move_dx = g_held_dir;  // original: = 2
            } else if (g_input_down != 0 && g_held_dir == 0) {
                dir = 0;
                g_move_dx = -2;
                g_move_dir = 0;
            } else if (g_input_left != 0) {
                dir = 3;
                g_move_dz = 2;
                g_move_dir = 3;
                g_held_dir = 3;
            } else if (g_input_right != 0) {
                dir = 1;
                g_move_dz = -2;
                g_move_dir = 1;
                g_held_dir = 1;
            } else if (g_input[0] != 0) {
                dir = 2;
                g_move_dx = 2;
                g_move_dir = 2;
                g_held_dir = 2;
            } else if (g_input_down != 0) {
                dir = 0;
                g_move_dx = -2;
                g_move_dir = 0;
                g_held_dir = 0;
            }
        } else {
            // Sensibilidade alta: cada tecla vale, salvo se o eixo segurado tem uma perpendicular
            // apertada; a ultima na ordem L, R, U, D vence. (O original desvia do bloco RIGHT
            // direto para UP/DOWN; o resultado e o mesmo desta sequencia.)
            int left = g_input_left, right = g_input_right, up = g_input[0], down = g_input_down;
            if (left != 0) {
                if (!((g_held_dir == 3 || g_held_dir == 1) && (up != 0 || down != 0))) {
                    dir = 3;
                    g_move_dz = 2;
                    g_move_dir = 3;
                }
                if (g_held_dir != 3 && up == 0 && down == 0) g_held_dir = 3;
            }
            if (right != 0) {
                if (!((g_held_dir == 1 || g_held_dir == 3) && (up != 0 || down != 0))) {
                    dir = 1;
                    g_move_dz = -2;
                    g_move_dir = 1;
                }
                if (g_held_dir != 1 && up == 0 && down == 0) g_held_dir = 1;
            }
            if (up != 0) {
                if (!((g_held_dir == 2 || g_held_dir == 0) && (left != 0 || right != 0))) {
                    dir = 2;
                    g_move_dx = 2;
                    g_move_dir = 2;
                }
                if (g_held_dir != 2 && left == 0 && right == 0) g_held_dir = 2;
            }
            if (down != 0) {
                if (!((g_held_dir == 0 || g_held_dir == 2) && (left != 0 || right != 0))) {
                    dir = 0;
                    g_move_dx = -2;
                    g_move_dir = 0;
                }
                if (g_held_dir != 0 && left == 0 && right == 0) g_held_dir = 0;
            }
        }
        if (dir != -1) {
            room->player_dir[g_cur_char] = dir;
            reset_idle_animation();
            dir = g_move_dir;
        }
    }

    // 13. Movimento.
    walked = false;
    if (dir == -1) stop_voice_slot(g_walk_voice);
    if (g_move_delay == 0) {
        if (g_cur_char == 1 && g_joined == 0) {
            // Heels sozinho nao muda de rumo no ar.
            if (g_jumping) goto idle;
            if (pl->y != 0 &&
                Room_find_collision(room, pl, pl->x, pl->y - 1, pl->z, &g_support_obj1) &&
                Room_find_collision(room, pl, pl->x, pl->y - 2, pl->z, &g_support_obj2)) {
                if (g_jumping) goto idle;  // original: testa de novo (sempre falso aqui)
                Sprite_stop_anim(pl);
                Sprite_set_anim_frame(pl, pl->anim, 1);
                g_prev_dir = -1;
                stop_voice_slot(g_walk_voice);
                goto fall_check;
            }
        }
        // Bonus: Heels +50 % mantendo a direcao; Head com o coelho de velocidade +100 %.
        g_move_bonus_dx = 0;
        g_move_bonus_dy = 0;
        g_move_bonus_dz = 0;
        if (g_cur_char == 1 && g_move_dir != -1 && g_move_dir == g_prev_dir) {
            g_move_bonus_dx = g_move_dx / 2;
            g_move_bonus_dy = g_move_dy / 2;
            g_move_bonus_dz = g_move_dz / 2;
        }
        if (g_move_dir != -1 && g_head_speed_timer != 0 && g_cur_char == 0 && g_joined == 0 &&
            (g_jumping || pl->y == 0 ||
             !Room_find_collision(room, pl, pl->x, pl->y - 1, pl->z, &g_support_obj1))) {
            g_head_speed_timer--;
            g_move_bonus_dx = g_move_dx;
            g_move_bonus_dy = g_move_dy;
            g_move_bonus_dz = g_move_dz;
        }
        // Cada eixo: passo encurtado ate caber (-1 livre, -2 borda, >= 0 objeto). O empurrao usa o
        // byte baixo de DAT_00432390 (1).
        moved = 0;
        if (g_move_dx != 0) {
            play_sfx_on_voice((g_cur_char == 0 && g_head_speed_timer == 0) ? 0x2b : 0x2c,
                              g_walk_voice);
            g_move_result = 0;
            n = g_move_bonus_dx + g_move_dx;
            while (n != 0) {
                g_move_result = Room_move_object(room, pl, n, 0, 0,
                                                 static_cast<uint8_t>(DAT_00432390) != 0, &moved);
                if (n > 0) n--;
                if (n < 0) n++;
                if (g_move_result == -1) break;
            }
            if (g_move_result == -1) {
                walked = true;
            } else {
                if (g_move_result == -2) g_hit_room_edge = 1;
                if (g_move_dir != g_prev_dir) g_hitwall_last_frame = -1;
                if (g_move_result != -2 && g_moved_last_frame) {
                    play_sfx_throttled(0x13, &g_hitwall_last_frame);
                }
            }
        }
        g_move_delay += moved;
        moved = 0;
        if (g_move_dy != 0) {
            play_sfx_on_voice((g_cur_char == 0 && g_head_speed_timer == 0) ? 0x2b : 0x2c,
                              g_walk_voice);
            g_move_result = 0;
            n = g_move_bonus_dy + g_move_dy;
            while (n != 0) {
                g_move_result = Room_move_object(room, pl, 0, n, 0,
                                                 static_cast<uint8_t>(DAT_00432390) != 0, &moved);
                if (n > 0) n--;
                if (n < 0) n++;
                if (g_move_result == -1) break;
            }
            if (g_move_result == -1) {
                walked = true;
            } else {
                if (g_move_result == -2) g_hit_room_edge = 1;
                if (g_move_dir != g_prev_dir) g_hitwall_last_frame = -1;
                if (g_move_result != -2 && g_moved_last_frame) {
                    play_sfx_throttled(0x13, &g_hitwall_last_frame);
                }
            }
        }
        g_move_delay += moved;
        moved = 0;
        if (g_move_dz != 0) {
            play_sfx_on_voice((g_cur_char == 0 && g_head_speed_timer == 0) ? 0x2b : 0x2c,
                              g_walk_voice);
            g_move_result = 0;
            n = g_move_bonus_dz + g_move_dz;
            while (n != 0) {
                g_move_result = Room_move_object(room, pl, 0, 0, n,
                                                 static_cast<uint8_t>(DAT_00432390) != 0, &moved);
                if (n > 0) n--;
                if (n < 0) n++;
                if (g_move_result == -1) break;
            }
            if (g_move_result == -1) {
                walked = true;
            } else {
                if (g_move_result == -2) g_hit_room_edge = 1;
                if (g_move_dir != g_prev_dir) g_hitwall_last_frame = -1;
                if (g_move_result != -2 && g_moved_last_frame) {
                    play_sfx_throttled(0x13, &g_hitwall_last_frame);
                }
            }
        }
        g_move_delay += moved;
        // Animacao de andar.
        if (g_move_dir == -1 && g_idle_state == 0) {
            Sprite_stop_anim(pl);
            Sprite_set_anim_frame(pl, pl->anim, 1);  // original: mesmo codigo para Head e Heels
        } else if (g_move_dir != g_prev_dir) {
            Sprite_play_anim(pl, g_move_dir, 0, -1);
        }
        if (g_prev_dir == -1 && g_move_dir != -1 && g_cur_char == 1 && !g_jumping) g_move_delay = 1;
        g_prev_dir = g_move_dir;
    } else {
        if (g_move_delay > 2) g_move_delay = 2;
        g_move_delay--;
    }

fall_check:
    // 14. Queda: sem pulo, y > 0 e livre em y-1.
    if (!g_jumping && pl->y != 0 &&
        Room_find_collision(room, pl, pl->x, pl->y - 1, pl->z, &g_support_obj1)) {
        stop_voice_slot(g_walk_voice);
        g_saved_anim = pl->anim;
        g_saved_frame = pl->frame;
        g_saved_anim_playing = pl->animating;
        g_falling = 1;
        Sprite_stop_anim(pl);
        switch (g_saved_anim) {
            case 0: Sprite_set_anim_frame(pl, 5, 4); break;
            case 1: Sprite_set_anim_frame(pl, 5, 0); break;
            case 2: Sprite_set_anim_frame(pl, 2, 1); break;
            case 3: Sprite_set_anim_frame(pl, 3, 1); break;
        }
        reset_idle_animation();
    }

idle:
    // 15. Animacao ociosa (piscar/olhar). Quadros da anim 5: base 4 olhando para 0, base 0 para 1.
    if (g_idle_timer == g_idle_delay && !g_in_final_room &&
        (g_idle_state != 0 || pl->anim == 0 || pl->anim == 1)) {
        if (g_idle_state == 0) {
            g_saved_anim = pl->anim;
            g_saved_frame = pl->frame;
        }
        g_idle_timer = 0;
        int base = g_saved_anim == 0 ? 4 : 0;
        switch (g_idle_state) {
            case 0: {
                int c;
                if (g_idle_choice < 3) {
                    c = rand() % 3 + 3;
                } else {
                    c = rand() % 2 + 1;
                }
                g_idle_choice = c;
                switch (c) {
                    case 1:
                    case 2:
                        Sprite_set_anim_frame(pl, 5, base + 1);
                        g_idle_delay = 5;
                        break;
                    case 3:
                        Sprite_set_anim_frame(pl, 5, base + 2);
                        g_idle_delay = 0x14;
                        break;
                    case 4:
                        Sprite_set_anim_frame(pl, 5, base + 3);
                        g_idle_delay = 0x14;
                        break;
                    case 5:
                        Sprite_set_anim_frame(pl, 5, base + 2);
                        g_idle_delay = 0xf;
                        break;
                }
                g_idle_state = c;
                break;
            }
            case 1:
            case 3:
            case 4:
                Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
                g_idle_state = 0;
                g_idle_delay = rand() % 0x19 + 0x4b;
                break;
            case 2:
                Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
                g_idle_delay = 5;
                g_idle_state = 6;
                break;
            case 5:
                Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
                g_idle_delay = 4;
                g_idle_state = 7;
                break;
            case 6:
                Sprite_set_anim_frame(pl, 5, base + 1);
                g_idle_delay = 5;
                g_idle_state = 1;
                break;
            case 7:
                Sprite_set_anim_frame(pl, 5, base + 3);
                g_idle_delay = 0xf;
                g_idle_state = 8;
                break;
            case 8:
                Sprite_set_anim_frame(pl, g_saved_anim, g_saved_frame);
                g_idle_delay = 4;
                g_idle_state = 9;
                break;
            case 9:
                Sprite_set_anim_frame(pl, 5, base + 2);
                g_idle_delay = 0xf;
                g_idle_state = 1;
                break;
        }
    }

    // 16. Chao assassino e escudo.
    if (room->killer_floor) {
        for (int i = 0; i < 2; i++) {
            if (g_char_room_id[i] == room->id && g_char_obj[i]->y == 0) {
                g_dying = 1;
                g_death_frame = 0;
                g_char_obj[i]->touched_killer = true;
                reset_idle_animation();
            }
        }
    }
    if (g_joined == 0) {
        for (int i = 0; i < 2; i++) {
            if (g_char_obj[i]->touched_killer && g_shield_timer[i] > 0) {
                g_char_obj[i]->touched_killer = false;
                g_dying = 0;
            }
        }
    } else if (g_char_obj[1]->touched_killer && (g_shield_timer[0] > 0 || g_shield_timer[1] > 0)) {
        g_char_obj[0]->touched_killer = false;
        g_char_obj[1]->touched_killer = false;
        g_dying = 0;
        reset_idle_animation();
    }
    if (g_char_obj[0]->touched_killer || g_char_obj[1]->touched_killer) {
        g_dying = 1;
        g_death_frame = 0;
        reset_idle_animation();
        Room_animate(room);
        return;
    }

    // 17. FIRE: rosquinha (Head ou juntos, com a buzina).
    if (g_input_fire == 0) {
        g_fire_latch = 0;
    } else {
        if (!g_fire_latch && (g_cur_char == 0 || g_joined != 0) && g_has_hooter && g_donuts > 0 &&
            room->projectile->disabled) {
            play_sfx(0x14);
            g_donuts--;
            Controller *pc = room->projectile;
            Object *d = pc->obj;
            d->contact = nullptr;
            int fz = pl->z;
            int fx = pl->x;
            int fy = pl->y;
            int ox = d->block->size_x + 3;
            int oz = d->block->size_z + 3;
            if (g_joined != 0) fy += g_blocks[0]->size_y;
            int a = pl->anim;
            if (a == 5) a = pl->frame == 0;  // ocioso
            int vx = 0;
            int vz = 0;
            switch (a) {
                case 0: vx = -6; fx -= ox; break;
                case 1: vz = -6; fz -= oz; break;
                case 2: vx = 6; fx += ox; break;
                case 3: vz = 6; fz += oz; break;
            }
            r = Room_move_object(room, d, fx - d->x, fy - d->y, fz - d->z, false, &moved);
            if (r == -1) {
                pc->vx = vx;
                pc->vz = vz;
                pc->disabled = false;
                Sprite_play_anim(d, 0, 0, -1);
            } else if (r >= 0) {
                Object *hit = room->objects[r];
                if (hit->freezable) {
                    play_sfx(0xb);
                    hit->freeze_timer = 9000;
                    if (hit->freeze_stops_anim) Sprite_stop_anim(hit);
                }
            }
        }
        g_fire_latch = 1;
    }

    // 18. Porta: bateu na borda dentro do retangulo de uma abertura -> troca de sala.
    if (g_hit_room_edge) {
        int ex = pl->x;
        int ey = pl->y;
        int ez = pl->z;
        if (pl->anim == 2) ex += pl->block->size_x;
        if (pl->anim == 3) ez += pl->block->size_z;
        int found = -1;
        int nd = room->n_doors;
        for (int i = 0; i < nd; i++) {
            Door *d = room->doors[i];
            switch (d->type) {
                case 0:
                case 3:
                    if (d->base_y <= ey && ey < d->top_y + d->base_y && d->ent_x <= ex &&
                        ex < d->width + d->ent_x && ez == d->ent_z) {
                        found = i;
                    }
                    break;
                case 1:
                case 2:
                    if (d->base_y <= ey && ey < d->top_y + d->base_y && d->ent_z <= ez &&
                        ez < d->width + d->ent_z && ex == d->ent_x) {
                        found = i;
                    }
                    break;
            }
        }
        if (found != -1) {
            Room_animate(room);
            change_room(found, nullptr);
            return;
        }
        if (g_moved_last_frame) play_sfx_throttled(0x13, &g_hitwall_last_frame);
    }

    room = g_char_rooms[g_active_room_slot];
    pl = g_char_obj[g_cur_char];
    g_moved_last_frame = walked;

    // 19. Saidas pelo teto (tipo 4) e pelo chao (tipo 5).
    if (room->has_up_exit && pl->y >= room->up_exit_y) {
        for (int i = 0; i < room->n_doors; i++) {
            if (room->doors[i]->type == EXIT_CEILING) {
                n = i;
                goto exit_room;
            }
        }
    } else if (room->has_down_exit) {
        if (pl->y == 0) {
            for (int i = 0; i < room->n_doors; i++) {
                if (room->doors[i]->type == EXIT_FLOOR) {
                    n = i;
                    goto exit_room;
                }
            }
        } else if (g_joined == 0 && g_char_room_id[0] == g_char_room_id[1]) {
            // O outro personagem, no chao desta sala, cai pela saida (sem trocar o controle).
            int o = other_character();
            if (g_char_obj[o]->y == 0) {
                g_cur_char = o;
                for (int i = 0; i < room->n_doors; i++) {
                    if (room->doors[i]->type == EXIT_FLOOR) change_room(i, nullptr);
                }
                g_cur_char = other_character();
                g_active_room_slot = g_cur_char;
                Room_animate(room);
                return;
            }
        }
    }

    // 20. Teletransportador: JUMP sobre um controlador tipo 5 ativo.
    n = room->n_controllers;
    for (int i = 0; i < n; i++) {
        Controller *c = room->controllers[i];
        if (c->type == CTRL_TELEPORTER && c->state && pl->standing_on == c->obj &&
            g_input_jump != 0) {
            clear_to_color(g_hud_old, makecol(0xff, 0, 0xff));
            clear_to_color(g_hud_new, makecol(0xff, 0, 0xff));
            hud_draw_world_panels(g_hud_old, 0x140, -1);
            g_teleport_dest_room = c->p28 + c->p2c * 40;
            // Carrega a sala destino so para ler o mundo (paineis do HUD) e a destroi.
            Room *tr = load_room(g_teleport_dest_room);
            hud_draw_world_panels(g_hud_new, 0x140, tr->wall_named);
            if (tr != nullptr) Room_destroy(tr);
            reset_idle_animation();
            g_teleport_ctrl = c;
            g_teleporting = 1;
            g_teleport_out = 1;
            g_teleport_timer = 0;
            g_hud_wipe = 0;
            g_teleport_saved_anim = pl->anim;
            play_sfx(0x27);
            if (g_teleport_saved_anim == 5) g_teleport_saved_anim = pl->frame < 4;
            Sprite_play_anim(pl, 4, 0, 2);
        }
    }

    // 21. CARRY / JUMP-CARRY (Heels ou juntos, com a bolsa).
    if (g_carry_cooldown > 0) g_carry_cooldown--;
    if (g_carrying) {
        g_jumpcarry_held = 0;
        if (g_input_jumpcarry != 0) g_jumpcarry_held = 1;
    }
    g_jump_after_pickup = 0;
    if (g_input_carry == 0 && !g_jumpcarry_held) {
        g_carry_latch = 0;
    } else {
        if (!g_carry_latch) {
            if (!g_carrying) {
                // Pega um objeto pegavel sob os pes e o esconde em -0x1000.
                if (g_has_bag && g_carry_cooldown == 0 && (g_cur_char == 1 || g_joined != 0)) {
                    int no = room->n_objects;
                    for (int i = 0; i < no; i++) {
                        Object *o = room->objects[i];
                        if (o->pickable && object_stands_on(pl, o) && !g_carrying) {
                            g_carry_origin_x = o->x;
                            g_carried_orig_y = o->y;
                            g_carried_orig_z = o->z;
                            Room_try_place_object(room, o, -kOutOfRoom, -kOutOfRoom, -kOutOfRoom);
                            o->standing_on = nullptr;
                            g_carrying = 1;
                            g_carry_cooldown = g_carry_cooldown_ticks;
                            g_carried_obj = o;
                            set_carried_object_suspended(true);
                            if (g_jumpcarry_held) g_jump_after_pickup = 1;
                        }
                    }
                }
            } else if ((g_cur_char == 1 || g_joined != 0) && !g_jumping &&
                       (pl->y == 0 ||
                        !Room_find_collision(room, pl, pl->x, pl->y - 1, pl->z, &g_support_obj1))) {
                // Solta: sobe o personagem a altura do objeto e tenta po-lo embaixo (busca 8x8 ao
                // redor do centro); se nao couber desfaz tudo.
                room_save_object_positions();
                int h = g_carried_obj->block->size_y;
                r = Room_move_object(room, pl, 0, h, 0, true, &moved);
                if (r != 0) {  // original: compara com 0 (objects[0]), nao com -1
                    bool placed = false;
                    Object *co = g_carried_obj;
                    int x_up = ((pl->block->size_x / 2 - co->x) - co->block->size_x / 2) + pl->x;
                    int x_dn = x_up;
                    int z0 = ((pl->block->size_z / 2 - co->z) - co->block->size_z / 2) + pl->z;
                    int ddy = (pl->y - co->y) - h;
                    for (int i = 8; i != 0; i--) {
                        int z_up = z0;
                        int z_dn = z0;
                        for (int j = 8; j != 0; j--) {
                            if (!placed &&
                                (Room_move_object(room, g_carried_obj, x_up, ddy, z_up, false,
                                                  &moved) == -1 ||
                                 Room_move_object(room, g_carried_obj, x_up, ddy, z_dn, false,
                                                  &moved) == -1 ||
                                 Room_move_object(room, g_carried_obj, x_dn, ddy, z_up, false,
                                                  &moved) == -1 ||
                                 Room_move_object(room, g_carried_obj, x_dn, ddy, z_dn, false,
                                                  &moved) == -1)) {
                                placed = true;
                            }
                            z_up++;
                            z_dn--;
                        }
                        x_up++;
                        x_dn--;
                    }
                    if (placed) {
                        g_carried_obj->standing_on = nullptr;
                        g_carrying = 0;
                        set_carried_object_suspended(false);
                        if (g_jumpcarry_held) g_carry_cooldown = g_carry_cooldown_ticks;
                    } else {
                        Room_move_object(room, pl, 0, -h, 0, false, &moved);
                        room_restore_object_positions();
                    }
                }
            }
        }
        g_carry_latch = 1;
    }

    // 22. Extras recem pegos (Object::bonus_pending, posto pelo controlador tipo 7).
    if (pl->bonus_pending != -1) {
        play_sample_at_volume(8, g_music_volume);
        bool crown = false;
        bool bunny = false;
        switch (pl->bonus_pending) {
            case 0: g_has_bag = 1; break;    // bolsa
            case 1: g_donuts += 6; break;    // bandeja de rosquinhas
            case 2: g_has_hooter = 1; break; // buzina
            case 3:                          // coelho: pulos fortes
                bunny = true;
                if (g_cur_char == 1 || g_joined != 0) g_heels_big_jumps = g_bigjumps_per_bunny;
                break;
            case 4:                          // coelho: velocidade
                bunny = true;
                if (g_cur_char == 0 || g_joined != 0) g_head_speed_timer = g_speed_bunny_ticks;
                break;
            case 5:                          // coelho: escudo
                bunny = true;
                if (g_joined != 0) {
                    g_shield_timer[1] = g_shield_bunny_ticks;
                    g_shield_timer[0] = g_shield_bunny_ticks;
                } else {
                    g_shield_timer[g_cur_char] = g_shield_bunny_ticks;
                }
                break;
            case 6:                          // coelho: +2 vidas (registrado sempre)
                if (g_joined != 0) {
                    g_lives[0] += 2;
                    g_lives[1] += 2;
                } else {
                    g_lives[g_cur_char] += 2;
                }
                break;
            case 7: g_fish_count++; break;   // peixe
            case 8:                          // luz
                bunny = true;
                if (g_joined != 0) {
                    g_light_timer[1] = DAT_00a5a9a0;
                    g_light_timer[0] = DAT_00a5a9a0;
                } else {
                    g_light_timer[g_cur_char] = DAT_00a5a9a0;
                }
                break;
            case 9: {                        // coroa: liberta o planeta da sala
                g_crowns++;
                crown = true;
                int planet = 0;
                int id = room->id;
                if (id > 0x46b) {
                    if (id == 0x4f8) {
                        planet = 3;
                    } else if (id == 0x50a) {
                        planet = 1;
                    }
                } else if (id == 0x46b) {
                    planet = 0;
                } else if (id == 0x1b5) {
                    planet = 2;
                } else if (id == 0x1bb) {
                    planet = 4;
                }
                g_planets_freed[planet] = 1;
                if (g_crowns == 4) {
                    g_four_crowns = 1;
                } else if (g_crowns == 5) {
                    g_shield_timer[1] = 0x493c2;
                    g_shield_timer[0] = 0x493c2;
                }
                break;
            }
        }
        pl->bonus_pending = -1;
        if (!bunny || DAT_00a5bc32) {
            int id = room->id;
            int k = g_room_taken_count[id];
            g_room_taken_count[id] = k + 1;
            // original: indice plano id*10 + k (sem limite de 10 por sala)
            (&g_room_taken_ids[0][0])[id * 10 + k] = pl->bonus_record_index;
        }
        if (crown) {
            play_sample_at_volume(0xf, g_music_volume);
            planet_screen_start(false);
            return;
        }
    }

    // 23. Passo final da sala.
    Room_animate(room);
    return;

exit_room:
    g_jumping = 0;
    g_jump_power = 1;
    pl->gravity = true;
    Room_animate(room);
    change_room(n, nullptr);
}

// 0x00417510 Room_set_active_player
void Room_set_active_player(Room *self, Object *player) {
    self->active_player = player;
}

// 0x00417630 Room_on_free_spring
bool Room_on_free_spring(Room *self, Object *obj) {
    for (int i = 0; i < self->n_controllers; i++) {
        Controller *c = self->controllers[i];
        if (c->type != CTRL_SPRING || !object_stands_on_tol(obj, c->obj)) continue;
        bool other = false;
        for (int j = 0; j < self->n_objects && !other; j++) {
            if (self->objects[j] != obj && object_stands_on(self->objects[j], c->obj)) other = true;
        }
        if (!other) return true;
    }
    return false;
}

}  // namespace hoh

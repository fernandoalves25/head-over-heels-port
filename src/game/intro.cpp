// Abertura (estado 5): campo de estrelas + sala 0xde + "PRESENTS...", tela-titulo, e o ticker
// de creditos desenhado pelos menus.
#include "game/intro.h"

#include "game/crt.h"
#include "game/gfx_util.h"
#include "game/globals.h"
#include "game/menu.h"
#include "game/pending.h"

#include <cstring>

namespace hoh {

Star g_stars[600];  // 0x527458, .bss (zeros); preenchido por intro_start

// g_char_rooms[] e g_char_obj[] sao void* em globals.h (Room*/Object* no original).
static Room *char_room(int slot) { return static_cast<Room *>(g_char_rooms[slot]); }
static Object *char_obj(int i) { return static_cast<Object *>(g_char_obj[i]); }

// 0x004070f0 intro_start: sorteia as estrelas, carrega a sala 0xde (222) com Head e Heels e
// zera o estado de jogo que a cena usa.
void intro_start() {
    for (int i = 0; i < 600; i++) {
        g_stars[i].x = rand() % 1000;
        g_stars[i].y = rand() % 1000;
        g_stars[i].layer = i / 0xaa + 1;
    }
    g_menu_timer = 0;
    g_game_state = 5;
    g_menu_id = 0;
    g_select_latch = 0;
    g_start_room = 0;
    g_char_rooms[0] = load_room(0xde);
    g_cur_char = 0;
    g_char_room_id[0] = room_id_of(char_room(0));
    g_player_room_heels = room_id_of(char_room(0));
    g_joined = 0;
    g_char_obj[0] = room_object(char_room(0), 0);
    g_player_heels = room_object(char_room(0), 1);
    Sprite_set_anim_frame(char_obj(0), 0, 1);
    Sprite_set_anim_frame(char_obj(1), 0, 1);
    Room_set_active_player(char_room(0), char_obj(0));
    g_move_delay = 0;
    g_move_dir = -1;
    g_held_dir = -1;
    g_prev_dir = -1;
    g_hud_wipe = -1;
    g_move_bonus_dx = 0;
    g_move_bonus_dy = 0;
    g_move_bonus_dz = 0;
    g_jumping = 0;
    g_jump_power = 1;
    g_hit_room_edge = 0;
    g_teleporting = 0;
    g_carry_latch = 0;
    g_carrying = 0;
    g_carry_cooldown = 0;
    g_has_hooter = 0;
    g_donuts = 0;
    g_has_bag = 0;
    g_fire_latch = 0;
    g_dying = 0;
    g_death_frame = 0;
    for (int i = 0; i < 2; i++) {
        Object *obj = char_obj(i);
        g_entry_x[i] = object_pos_x(obj);
        g_entry_y[i] = object_pos_y(obj);
        g_entry_z[i] = object_pos_z(obj);
        g_entry_anim[i] = object_anim(obj);
    }
    g_joined_at_entry = 0;
    g_transition_active = 0;
    g_jumpcarry_held = 0;
    DAT_00a5bc32 = 1;
    g_four_crowns = 0;
    g_percent_explored = 0;
    DAT_0091e470 = 0;  // original: grava so o byte baixo (trava do SWAP)
    g_life_transfer_anim = 0;
    g_in_final_room = 0;
    g_jump_after_pickup = 0;
    reset_idle_animation();
    g_swap_mode = 0;
    g_rain_volume = 0;
    g_rain_target = 0;
}

// 0x00407450 intro_update_scene: um tick da sala da abertura e das estrelas.
void intro_update_scene() {
    Room *room = char_room(0);
    Room_update_controllers(room);
    Room_update_physics(room);
    Room_animate(room);
    for (Star &star : g_stars) {
        star.x = star.x + star.layer;
        if (star.x >= 1000) {
            star.x = 0;
            star.y = rand() % 1000;
        }
    }
}

// 0x0040bff0 update_state_intro (estado 5). Na subfase 1, apertar e soltar qualquer controle
// pula a cena (g_menu_id 0) ou sai da tela-titulo para o menu (g_menu_id 1).
void update_state_intro() {
    g_menu_timer = g_menu_timer + 1;
    if (g_intro_step == 1) {
        if (g_select_latch == 0) {
            if (input_any_pressed()) g_select_latch = 1;
        } else if (!input_any_pressed()) {
            if (g_menu_id == 0) {
                g_intro_step = 2;
                g_menu_timer = 0;
                return;
            }
            if (g_menu_id != 1) return;
            blit(g_backbuffer, g_buffer2, 0, 0, 0, 0, g_screen_w, g_screen_h);
            menu_return_to_main();
            return;
        }
    }
    if (g_menu_id == 0) {
        intro_update_scene();
        if (g_intro_step == 0) {
            if (g_menu_timer > 0x40) {
                g_menu_timer = 0;
                g_intro_step = 1;
                return;
            }
        } else if (g_intro_step == 1) {
            if (g_menu_timer > 0x30c) {
                g_menu_timer = 0;
                g_intro_step = 2;
                return;
            }
        } else if (g_intro_step == 2 && g_menu_timer > 0x20) {
            // fim da cena: tela-titulo (subfase 1 de novo, esperando tecla) e a sala e destruida
            g_intro_step = 1;
            g_menu_id = 1;
            g_select_latch = 0;
            g_menu_timer = 0;
            if (g_char_rooms[0] != nullptr) {
                Room_scalar_dtor(char_room(0), 1);
                g_char_rooms[0] = nullptr;
            }
        }
    } else if (g_menu_id == 1 && g_menu_timer > 800) {
        blit(g_backbuffer, g_buffer2, 0, 0, 0, 0, g_screen_w, g_screen_h);
        menu_return_to_main();
        return;
    }
}

// 0x0040dd10 intro_draw_floor_logo: 60 losangos translucidos (g_images[0], 48x29) no chao da
// sala, nas posicoes isometricas de g_intro_logo_tiles (0x432400).
void intro_draw_floor_logo(BITMAP *dst) {
    Room *room = char_room(g_active_room_slot);
    int ox = room_origin_x(room);
    int oy = room_origin_y(room);
    for (int i = 0; i < 60; i++) {
        int a = g_intro_logo_tiles[i][0];
        int b = g_intro_logo_tiles[i][1];
        fblend_trans(g_images[0], dst, a * 0x1c - b * 0x1c + ox - 0x2f,
                     (oy + 6) - ((b * 0x1c + a * 0x1c) >> 1), 0x80);
    }
}

// 0x0040dd90 draw_intro_screen (render do estado 5)
void draw_intro_screen(BITMAP *dst) {
    if (g_menu_id != 0) {
        if (g_menu_id != 1) return;
        if (g_menu_timer > 0x20) {
            // original: argumentos invertidos (confirmado no disassembly em 0x40ddcd): copia o
            // backbuffer PARA g_img_title. Sem efeito visivel: dst ja tem o titulo a 100%.
            blit(dst, g_img_title, 0, 0, 0, 0, g_screen_w, g_screen_h);
            return;
        }
        draw_sprite_lit(dst, g_img_title, 0, 0, g_menu_timer * 8);  // fade-in do titulo
        return;
    }

    clear_to_color(dst, 0);
    // estrelas: 4 pixels por estrela, cada um 40 niveis de cinza mais escuro
    for (const Star &star : g_stars) {
        int grey = star.layer * 0x1e + 0x87;
        for (int j = 0; j < 4; j++) {
            putpixel(dst, ((star.x - star.y) - j) + 0x96, (((star.y - j) + star.x) >> 1) - 0xf0,
                     makecol(grey, grey, grey));
            grey = grey - 0x28;
        }
    }
    // original: a rolagem da sala fica no render (+2,-1 por quadro desenhado), sempre no slot 0
    if (g_intro_step == 1 && g_menu_timer > 600 && g_menu_timer < 0x278) {
        room_origin_x(char_room(0)) = room_origin_x(char_room(0)) + 2;
        room_origin_y(char_room(0)) = room_origin_y(char_room(0)) - 1;
    }
    intro_draw_floor_logo(dst);
    Room_draw_objects(char_room(0), dst, g_shadows_on, g_shadow_buffer, g_shadows_complex);

    bool presents = false;
    if (g_intro_step == 1) {
        if (g_menu_timer > 600) {
            g_intro_presents_alpha = g_menu_timer - 600;
            presents = true;
        }
    } else if (g_intro_step == 2) {
        presents = true;  // mantem o alpha da subfase 1
    }
    if (presents) {
        int alpha = g_intro_presents_alpha;
        if (alpha > 0x1f) alpha = 0x1f;
        g_intro_presents_alpha = alpha;
        // escurece a cena inteira e redesenha os objetos 3..11 por cima, sem escurecer
        draw_sprite_lit(dst, dst, 0, 0, (0x20 - alpha) * 8);
        for (int i = 3; i < 12; i++) {
            Room *room = char_room(g_active_room_slot);
            Sprite_draw(room_object(room, i), dst, room_origin_x(room), room_origin_y(room));
        }
        Room_draw_glows(char_room(0), dst);
        clear_to_color(g_bmp_text_line, g_mask_color);
        draw_text_w18(g_bmp_text_line, g_font_medium, s_PRESENTS____00433374, 0, 0);
        if (alpha == 0x1f) {
            draw_sprite(dst, g_bmp_text_line, 0x78, 0x140);
        } else {
            fblend_trans(g_bmp_text_line, dst, 0x78, 0x140, alpha * 8);
        }
    } else {
        Room_draw_glows(char_room(0), dst);
    }

    if (g_intro_step != 1) {
        int light;
        if (g_intro_step == 2) {
            light = (0x20 - g_menu_timer) * 8;  // fade-out
        } else {
            light = g_menu_timer * 4;  // fade-in
        }
        draw_sprite_lit(dst, dst, 0, 0, light);
    }
}

// 0x0040cc20 draw_credits_ticker: frase g_ticker_index centrada em x=320, rodape da tela, com
// sombra (+3,+3) e matiz girando com g_frame_counter. Fade de 20 ticks no inicio e no fim dos
// 200 ticks da frase (g_ticker_timer).
void draw_credits_ticker(BITMAP *dst) {
    int shadow_alpha = 0x3c;
    int text_alpha = 0xff;
    int fade = 0x14;
    if (g_ticker_timer < 0x14) fade = g_ticker_timer;
    if (g_ticker_timer > 0xb4) fade = 200 - g_ticker_timer;
    if (fade != 0x14) {
        shadow_alpha = fade * 3;
        text_alpha = fade * 0xc;
    }
    int w = static_cast<int>(std::strlen(g_ticker_texts[g_ticker_index])) * 0x12;
    int x = 0x140 - (w >> 1);
    fblend_trans(g_bmp_ticker_shadow[g_ticker_index], dst, x + 3, g_screen_h - 0x13, shadow_alpha);
    if (fade != 0x14) {
        // translucido: tinge num rascunho e mistura
        clear_to_color(g_font_tinted, g_mask_color);
        tint_bitmap(g_bmp_ticker[g_ticker_index], g_font_tinted, (g_frame_counter / 0xc) % 256,
                    0x96, 0xff, 0, 0, 0, 0, 0, w, 0x12, 0);
        fblend_trans(g_font_tinted, dst, x, g_screen_h - 0x16, text_alpha);
        return;
    }
    // opaco: tinge direto no destino
    tint_bitmap(g_bmp_ticker[g_ticker_index], dst, (g_frame_counter / 0xc) % 256, 0x96, 0xff, 0, 0,
                0, x, g_screen_h - 0x16, w, 0x12, 0);
}

}  // namespace hoh

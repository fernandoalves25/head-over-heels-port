// Modulo screens (HoH.exe 0x405d90..0x40f5d0): HUD, quadro do jogo, Save%i.dat, pausa, mapa do
// imperio (estado 2) e tela de resultado/recorde (estado 4).
//
// O servidor de recordes da Retrospec (retrospec.sgn.net, WinInet) nao existe mais: o port nao
// abre sockets; highscore_net_open toma o caminho de falha do original (InternetOpenA falhou), o
// que leva highscore_submit a g_hiscore_state = 3 ("Error!  Press `S` to resend!"). O codigo de
// recorde continua sendo gerado, gravado em HighScoreCode.txt, copiado para a area de
// transferencia e mostrado na tela, como no original.
#include "game/screens.h"

#include "game/controllers.h"
#include "game/gameplay.h"
#include "game/gfx_util.h"
#include "game/globals.h"
#include "game/main_loop.h"
#include "game/menu.h"
#include "game/music.h"
#include "game/physics.h"
#include "game/room_load.h"
#include "game/room_render.h"
#include "game/sound.h"
#include "game/water.h"
#include "compat/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace hoh {

// Numeracao de teclas do Allegro 4.0 usada por highscore_name_input / update_state_result.
static_assert(KEY_S == 0x13 && KEY_Z == 0x1a && KEY_0 == 0x1b && KEY_1 == 0x1c && KEY_9 == 0x24,
              "teclas");
static_assert(KEY_ESC == 0x3b && KEY_BACKSPACE == 0x3f && KEY_ENTER == 0x43 && KEY_COLON == 0x44 &&
                  KEY_SPACE == 0x4b && KEY_DEL == 0x4d && KEY_LEFT == 0x52 && KEY_RIGHT == 0x53,
              "teclas");
static_assert(KEY_LSHIFT == 0x67 && KEY_RSHIFT == 0x68, "teclas");

// ------------------------------------------------------------------ globais do modulo (MODULE)
Clipboard g_clipboard = {nullptr, 0, 0};  // 0xa5a980 (.bss)
Clipboard *g_clipboard_ptr = nullptr;     // 0xa5a978 (.bss)

namespace {

// Byte de alinhamento 0xa5bc27 (depois de g_transition_active): o original o le/grava no save
// junto com g_joined_at_entry (ver joined_block_*). Guardado so para o save sair identico.
uint8_t g_pad_00a5bc27 = 0;

// pack_fread(&v, 4, f) do original (int32 little-endian): bytes que faltarem mantem o valor antigo.
void pack_read_i32(int32_t &v, PACKFILE *f) {
    uint32_t u = static_cast<uint32_t>(v);
    uint8_t b[4] = {uint8_t(u), uint8_t(u >> 8), uint8_t(u >> 16), uint8_t(u >> 24)};
    pack_fread(b, 4, f);
    v = static_cast<int32_t>(uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 |
                             uint32_t(b[3]) << 24);
}

// pack_fwrite(&v, 4, f) do original.
void pack_write_i32(int32_t v, PACKFILE *f) {
    uint32_t u = static_cast<uint32_t>(v);
    uint8_t b[4] = {uint8_t(u), uint8_t(u >> 8), uint8_t(u >> 16), uint8_t(u >> 24)};
    pack_fwrite(b, 4, f);
}

// original: pack_fread(&g_joined_at_entry, 4, f) com g_joined_at_entry sendo um byte (0xa5bc24):
// os 4 bytes sao g_joined_at_entry, g_cheat (0xa5bc25), g_transition_active (0xa5bc26) e o
// alinhamento 0xa5bc27. Carregar um save restaura portanto o estado da trapaca gravado com ele.
void joined_block_read(PACKFILE *f) {
    uint8_t b[4] = {g_joined_at_entry, g_cheat, g_transition_active, g_pad_00a5bc27};
    pack_fread(b, 4, f);
    g_joined_at_entry = b[0];
    g_cheat = b[1];
    g_transition_active = b[2];
    g_pad_00a5bc27 = b[3];
}

void joined_block_write(PACKFILE *f) {
    const uint8_t b[4] = {g_joined_at_entry, g_cheat, g_transition_active, g_pad_00a5bc27};
    pack_fwrite(b, 4, f);
}

// Comprimento como int (o original usa repne scasb e trata como int).
int str_len(const char *s) { return static_cast<int>(std::strlen(s)); }

}  // namespace

// ================================================================== HUD e quadro do jogo

// 0x00405d90 hud_draw_bar: fundo (linha y=16 de g_bar_image, 40x16) + parte cheia (y=0) com
// largura 1 + value*18/49.
void hud_draw_bar(BITMAP *dst, int x, int y, int value) {
    if (value > 0) {
        masked_blit(g_bar_image, dst, 0, 0x10, x, y, 0x28, 0x10);
        // original: _ftol(value * -0.3673469387755102) (double em 0x430278)
        const int full = static_cast<int>(static_cast<double>(value) * -0.3673469387755102);
        masked_blit(g_bar_image, dst, 0, 0, x, y, 1 - full, 0x10);
    }
}

// 0x00405e00 hud_draw_lives: vidas de Head e Heels centradas nas posicoes g_hud_lives_pos; durante
// a transferencia de vidas (g_life_transfer_anim) o '+N' voa de um contador para o outro.
void hud_draw_lives(BITMAP *dst) {
    char text[256];
    int lives[2] = {g_lives[0], g_lives[1]};
    if (g_transition_frame < 0x1d) {
        if (g_life_transfer_anim) {
            lives[g_life_transfer_from] = lives[g_life_transfer_from] + g_life_transfer_amount;
            lives[g_life_transfer_from == 0] = lives[g_life_transfer_from == 0] - 1;
        }
    } else {
        g_life_transfer_anim = 0;
    }
    std::sprintf(text, "%i", lives[0]);  // g_fmt_int (0x43306c)
    draw_text_w10(dst, g_font_w10, text, g_hud_lives_pos[0][0] - str_len(text) * 5, g_hud_lives_pos[0][1]);
    std::sprintf(text, "%i", lives[1]);
    draw_text_w10(dst, g_font_w10, text, g_hud_lives_pos[1][0] - str_len(text) * 5, g_hud_lives_pos[1][1]);
    if (g_life_transfer_anim) {
        std::sprintf(text, "%i", g_life_transfer_amount);
        const int heels_x = g_hud_lives_pos[1][0];
        const int head_x = g_hud_lives_pos[0][0];
        const int dx = heels_x - head_x;
        // original: _ftol((float)g_transition_frame * 0.03125f (0x430280) * dx)
        const int off = static_cast<int>(static_cast<double>(g_transition_frame) * 0.03125 *
                                         static_cast<double>(dx));
        int x;
        if (g_life_transfer_from == 0) x = head_x - str_len(text) * 5 + off;
        else x = heels_x - str_len(text) * 9 - off;
        clear_to_color(g_popup_bitmap, g_mask_color);
        draw_text_w10(g_popup_bitmap, g_font_w10, text, 0, 0);
        tint_bitmap(g_popup_bitmap, g_popup_bitmap, 0, 0, 0xff, 0x400, 0, 0, 0, 0, str_len(text) * 10,
                    0x1a, 0);
        fblend_trans(g_popup_bitmap, dst, x, g_hud_lives_pos[0][1], 0xff - g_transition_frame * 8);
    }
}

// 0x00406080 hud_draw_world_panels: os 3 paineis do HUD do mundo (g_images[19..21 + 3*mundo]).
// O original testava bitmap_color_depth == 8 (draw_256_sprite); o port so tem 16 bpp.
void hud_draw_world_panels(BITMAP *dst, int y_shift, int named_index) {
    if (named_index == -1) named_index = g_char_rooms[g_active_room_slot]->wall_named;
    const int k = world_index3_from_named(named_index);
    const int y = 0x140 - y_shift;
    draw_sprite(dst, g_images[19 + k], 0, y);
    draw_sprite(dst, g_images[20 + k], 0xd5, 0x1a3 - y_shift);
    draw_sprite(dst, g_images[21 + k], 0x1ab, y);
}

// 0x00406150 hud_draw: paineis (ou a transicao de mundo do teletransporte: 160 circulos magenta
// furando g_hud_old sobre g_hud_new), icones cinza do que falta, objeto carregado na bolsa,
// rosquinhas, barras de escudo, contadores de velocidade/pulos e vidas.
void hud_draw(BITMAP *dst) {
    char text[256];
    if (g_hud_wipe == -1) {
        hud_draw_world_panels(dst, 0, -1);
    } else {
        srand(g_teleport_dest_room);
        for (int n = 0xa0; n != 0; n--) {
            const int x = rand() % 0x280;
            const int y = rand() % 0xa0;
            const int r = g_hud_wipe - x / 0x14;
            if (r > 0) circlefill(g_hud_old, x, y, r, makecol(0xff, 0, 0xff));
        }
        if (g_hud_wipe > 0x50) g_hud_wipe = -1;
        draw_sprite(dst, g_hud_new, 0, 0x140);
        draw_sprite(dst, g_hud_old, 0, 0x140);
        srand(g_frame_counter);
    }
    // Personagem ausente (separados): o outro aparece em cinza.
    if (g_cur_char == 1 && g_joined == 0) draw_sprite(dst, g_icon_head_grey, g_hud_layout[0], DAT_004320e8);
    if (g_cur_char == 0 && g_joined == 0) draw_sprite(dst, g_icon_heels_grey, DAT_004320ec, DAT_004320f0);
    if (!g_has_bag) draw_sprite(dst, g_icon_bag_grey, DAT_00432104, DAT_00432108);
    if (g_carrying && g_has_bag) {
        Block *b = g_carried_obj->block;
        fblend_trans(b->shadow, dst, DAT_0043210c, (b->image->h - b->shadow->h) + DAT_00432110, 100);
        draw_sprite(dst, g_carried_obj->block->image, DAT_0043210c, DAT_00432110);
    }
    if (!g_has_hooter) draw_sprite(dst, g_icon_hooter_grey, DAT_00432124, DAT_00432128);
    if (!g_has_hooter || g_donuts == 0) draw_sprite(dst, g_icon_doughnut_grey, DAT_00432114, DAT_00432118);
    if (g_donuts > 0) {
        std::sprintf(text, "%i", g_donuts);
        draw_text_w10(dst, g_font_w10, text, DAT_0043211c - str_len(text) * 5, DAT_00432120);
    }
    for (int i = 0; i < 2; i++)
        hud_draw_bar(dst, DAT_0043213c[i], DAT_00432144[i], (g_shield_timer[i] * 4) / g_ticks_per_second);
    if (g_head_speed_timer > 0) {
        std::sprintf(text, "%i", g_head_speed_timer / 7);
        draw_text_w10(dst, g_font_w10, text, DAT_00432154 - str_len(text) * 5, DAT_00432158);
    }
    if (g_heels_big_jumps > 0) {
        std::sprintf(text, "%i", g_heels_big_jumps);
        draw_text_w10(dst, g_font_w10, text, DAT_00432164 - str_len(text) * 5, DAT_00432168);
    }
    hud_draw_lives(dst);
}

// 0x00406570 render_game_frame
void render_game_frame(BITMAP *dst) {
    clear_to_color(dst, 0);
    Room_draw(g_char_rooms[g_active_room_slot], dst, g_shadows_on, g_shadow_buffer, g_shadows_complex);
    hud_draw(dst);
}

// ================================================================== saves

// 0x004065c0 load_game: Save%i.dat (slot g_load_slot + 1); sem arquivo comeca um jogo novo.
// Formato em B_structs.md §3.1 (o mesmo que save_game grava).
void load_game() {
    char filename[256];
    std::sprintf(filename, s_Save_i_dat_00433070, g_load_slot + 1);
    packfile_password(g_password);
    PACKFILE *f = pack_fopen(filename, "rbp");  // 0x43304c
    packfile_password(nullptr);
    if (!f) {
        new_game();
        return;
    }
    char name[256];
    pack_fread(name, 0x10, f);  // nome do slot (descartado; scan_save_slots e quem o le)
    pack_read_i32(g_percent_explored, f);
    pack_read_i32(g_active_room_slot, f);
    for (int r = 0; r < 1600; r++) {
        pack_read_i32(g_room_taken_count[r], f);
        int32_t *ids = &g_room_taken_ids[0][0] + r * 10;  // original: ponteiro avancando 0x28 por sala
        for (int i = 0; i < g_room_taken_count[r]; i++) pack_read_i32(ids[i], f);
    }
    int32_t room_id[2] = {0, 0};  // original: locais nao iniciados
    pack_read_i32(room_id[0], f);
    pack_read_i32(room_id[1], f);
    pack_read_i32(g_cur_char, f);
    pack_read_i32(g_char_room_id[0], f);
    pack_read_i32(g_char_room_id[1], f);
    pack_read_i32(g_lives[0], f);
    pack_read_i32(g_lives[1], f);
    pack_read_i32(g_head_speed_timer, f);
    pack_read_i32(g_heels_big_jumps, f);
    pack_read_i32(g_shield_timer[0], f);
    pack_read_i32(g_shield_timer[1], f);
    pack_fread(&g_has_hooter, 1, f);
    pack_read_i32(g_donuts, f);
    pack_fread(&g_has_bag, 1, f);
    pack_read_i32(g_fish_count, f);
    pack_read_i32(g_crowns, f);
    for (int i = 0; i < 2; i++) {
        pack_read_i32(g_entry_x[i], f);
        pack_read_i32(g_entry_y[i], f);
        pack_read_i32(g_entry_z[i], f);
        pack_read_i32(g_entry_anim[i], f);
    }
    joined_block_read(f);  // original: pack_fread(&g_joined_at_entry, 4, f)
    pack_read_i32(g_light_timer[0], f);
    pack_read_i32(g_light_timer[1], f);
    pack_fread(&g_four_crowns, 1, f);
    for (int i = 0; i < 5; i++) pack_fread(&g_planets_freed[i], 1, f);
    for (int i = 0; i < 0x640; i++) pack_fread(&g_room_visited[i], 1, f);
    pack_read_i32(g_rooms_visited_count, f);
    pack_fclose(f);

    g_move_delay = 0;
    g_move_dir = -1;
    g_prev_dir = -1;
    g_held_dir = -1;
    g_move_bonus_dx = 0;
    g_move_bonus_dy = 0;
    g_move_bonus_dz = 0;
    g_jumping = 0;
    g_jump_after_pickup = 0;
    g_jump_power = 1;
    g_hit_room_edge = 0;
    g_teleporting = 0;
    g_hud_wipe = -1;
    g_carry_latch = 0;
    g_carrying = 0;
    g_carry_cooldown = 0;
    g_fire_latch = 0;
    DAT_0091e470 = 0;  // original: escrita de 1 byte (a flag so vale 0/1)
    g_transition_active = 0;
    g_dying = 0;
    g_death_frame = 0;
    g_jumpcarry_held = 0;
    DAT_00a5bc32 = 1;
    g_frame_counter = 0;
    g_life_transfer_anim = 0;
    g_in_final_room = 0;
    reset_idle_animation();

    // rooms[i] = sala do personagem i (Heels fica na sala de Head quando estao juntos na mesma).
    Room *rooms[2];
    g_char_rooms[0] = load_room(room_id[0]);
    rooms[0] = g_char_rooms[0];
    rooms[1] = g_char_rooms[0];
    if (room_id[1] == -1) {
        g_char_rooms[1] = nullptr;
    } else {
        g_char_rooms[1] = load_room(room_id[1]);
        rooms[1] = g_char_rooms[1];
    }
    for (int i = 1; i >= 0; i--) {  // original: Heels primeiro, depois Head
        Room *room = rooms[i];
        g_char_obj[i] = room->objects[i];
        Sprite_set_anim_frame(g_char_obj[i], g_entry_anim[i], 1);
        Room_set_active_player(room, g_char_obj[i]);
        const int z = g_entry_z[i];
        int y = g_entry_y[i];
        const int x = g_entry_x[i];
        while (!Room_try_place_object(room, g_char_obj[i], x, y, z)) y = y + 1;
        Room_update_camera(room, g_view_ext_x, g_view_ext_z);
    }
    if (!g_joined_at_entry) {
        g_joined = 0;
    } else {
        g_joined = 1;
        g_cur_char = 1;
        g_char_obj[0]->gravity = true;
        g_char_obj[1]->gravity = true;
        g_prev_dir = -1;
        Room_set_active_player(g_char_rooms[g_active_room_slot], g_char_obj[1]);
        g_char_obj[0]->x = kOutOfRoom;
        g_char_obj[0]->y = kOutOfRoom;
        g_char_obj[0]->z = kOutOfRoom;
        Sprite_invalidate(g_char_obj[0]);
        Room_try_place_object(g_char_rooms[0], g_char_obj[1], g_entry_x[0], g_entry_y[0], g_entry_z[0]);
        Sprite_set_block(g_char_obj[1], g_blocks[2]);
        Sprite_set_anim_frame(g_char_obj[1], g_entry_anim[0], 1);
    }
    room_spawn_weather(g_char_rooms[0]);
    if (g_char_room_id[0] != g_char_room_id[1]) room_spawn_weather(g_char_rooms[1]);
    room_init_scroll(g_char_rooms[g_active_room_slot]);
    if (g_joined != 0) {
        g_rain_volume = 0;
        g_rain_target = 0;
        g_swap_mode = 2;
        return;
    }
    g_rain_volume = 0;
    g_rain_target = 0;
    g_swap_mode = g_cur_char;
}

// 0x00409e20 save_game: Save%i.dat do slot g_menu_sel + 1 (chamado pelo menu 6 durante a pausa).
void save_game() {
    char filename[256];
    std::sprintf(filename, s_Save_i_dat_00433070, g_menu_sel + 1);
    packfile_password(g_password);
    PACKFILE *f = pack_fopen(filename, "wbp");  // 0x433140
    packfile_password(nullptr);
    if (!f) return;
    // Nome = rotulo do item do menu 6 (g_save_slot_text + g_menu_sel*0x18) cortado em 15.
    char name[256] = {};  // original: o que vem depois do NUL e lixo de pilha
    std::strcpy(name, g_menu_items[6][g_menu_sel]);
    name[15] = 0;
    pack_fwrite(name, 0x10, f);
    pack_write_i32(g_percent_explored, f);
    pack_write_i32(g_active_room_slot, f);
    for (int r = 0; r < 1600; r++) {
        pack_write_i32(g_room_taken_count[r], f);
        const int32_t *ids = &g_room_taken_ids[0][0] + r * 10;
        for (int i = 0; i < g_room_taken_count[r]; i++) pack_write_i32(ids[i], f);
    }
    pack_write_i32(g_char_rooms[0]->id, f);
    const int32_t heels_room = g_char_rooms[1] == nullptr ? -1 : g_char_rooms[1]->id;
    pack_write_i32(heels_room, f);
    pack_write_i32(g_cur_char, f);
    pack_write_i32(g_char_room_id[0], f);
    pack_write_i32(g_char_room_id[1], f);
    pack_write_i32(g_lives[0], f);
    pack_write_i32(g_lives[1], f);
    pack_write_i32(g_head_speed_timer, f);
    pack_write_i32(g_heels_big_jumps, f);
    pack_write_i32(g_shield_timer[0], f);
    pack_write_i32(g_shield_timer[1], f);
    pack_fwrite(&g_has_hooter, 1, f);
    pack_write_i32(g_donuts, f);
    pack_fwrite(&g_has_bag, 1, f);
    pack_write_i32(g_fish_count, f);
    pack_write_i32(g_crowns, f);
    for (int i = 0; i < 2; i++) {
        pack_write_i32(g_entry_x[i], f);
        pack_write_i32(g_entry_y[i], f);
        pack_write_i32(g_entry_z[i], f);
        pack_write_i32(g_entry_anim[i], f);
    }
    joined_block_write(f);  // original: pack_fwrite(&g_joined_at_entry, 4, f)
    pack_write_i32(g_light_timer[0], f);
    pack_write_i32(g_light_timer[1], f);
    pack_fwrite(&g_four_crowns, 1, f);
    for (int i = 0; i < 5; i++) pack_fwrite(&g_planets_freed[i], 1, f);
    for (int i = 0; i < 0x640; i++) pack_fwrite(&g_room_visited[i], 1, f);
    pack_write_i32(g_rooms_visited_count, f);
    pack_fclose(f);
}

// ================================================================== recorde

// 0x00406b00 clipboard_init (nao libera o texto anterior, como o original)
void clipboard_init(Clipboard *cb) {
    cb->text = nullptr;
    cb->status = 0;
}

// 0x00406b20 clipboard_set_text. Original: OpenClipboard(NULL), EmptyClipboard,
// SetClipboardData(CF_TEXT, text), CloseClipboard, cada falha trocando o texto por uma mensagem.
// No port a area de transferencia do sistema e platform_set_clipboard_text; a falha toma o
// caminho do SetClipboardData (abrir/fechar nao falham).
void clipboard_set_text(Clipboard *cb) {
    cb->status = -1;
    if (!platform_set_clipboard_text(cb->text)) {
        cb->text = static_cast<char *>(std::realloc(cb->text, 0x20));
        std::sprintf(cb->text, s_Failure_to_SetClipboardData____c_0043309c, 0);
    } else {
        cb->status = 1;
    }
}

// 0x00406bd0 highscore_encode: bytes [time>>8, time, tag>>8, tag] + cada valor em big-endian com
// nbytes[i] bytes; XOR encadeado (semente 0x69) + 1 byte de soma dos bytes originais; completa
// com zeros ate multiplo de 3 e codifica em base64 com o alfabeto de 0x4330e0 ("...+@").
void highscore_encode(char *out, int tag, int count, const int *values, const int *nbytes) {
    char alphabet[68];
    std::memcpy(alphabet, s_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef_004330e0, 0x41);  // 16 dwords + 1 byte
    uint8_t buf[32] = {};
    buf[0] = static_cast<uint8_t>(static_cast<uint32_t>(std::time(nullptr)) >> 8);
    buf[1] = static_cast<uint8_t>(static_cast<uint32_t>(std::time(nullptr)));
    buf[3] = static_cast<uint8_t>(tag);
    buf[2] = static_cast<uint8_t>(tag >> 8);
    int n = 4;
    for (int i = 0; i < count; i++) {
        int nb = nbytes[i];
        if (nb > 0) {
            const int v = values[i];
            int shift = nb * 8;
            do {
                shift = shift - 8;
                buf[n++] = static_cast<uint8_t>(v >> (shift & 0x1f));  // sar com cl (mascara 31)
                nb--;
            } while (nb != 0);
        }
    }
    uint8_t sum = 0;
    uint8_t chain = 0x69;
    for (int i = 0; i < n; i++) {
        const uint8_t orig = buf[i];
        chain = static_cast<uint8_t>(orig ^ chain);
        buf[i] = chain;
        sum = static_cast<uint8_t>(sum + orig);
    }
    buf[n] = sum;
    n = n + 1;
    int pad = 0;
    if (n % 3 != 0) pad = 3 - n % 3;
    if (pad > 0) {
        std::memset(buf + n, 0, static_cast<size_t>(pad));
        n = n + pad;
    }
    int k = 0;
    for (int g = 0; g < n / 3; g++) {
        const int v = (static_cast<int>(buf[g * 3]) * 0x100 + buf[g * 3 + 1]) * 0x100 + buf[g * 3 + 2];
        for (int shift = 0x12; shift > -6; shift -= 6) out[k++] = alphabet[(v >> shift) & 0x3f];
    }
    out[k - pad] = 0;
}

// 0x00406d40 gameover_screen_start: gera o codigo de recorde, grava HighScoreCode.txt e o copia
// para a area de transferencia (com trapaca: "++++++++++++" e nada gravado) -> estado 4.
void gameover_screen_start() {
    int values[3];
    values[0] = g_percent_explored;
    if (g_percent_explored > 100) values[0] = 100;
    values[1] = g_rooms_visited_count;
    values[2] = 0;
    for (int i = 0; i < 5; i++)
        if (g_planets_freed[i]) values[2]++;
    const int nbytes[3] = {1, 2, 1};
    highscore_encode(g_hiscore_code, 0x3f, 3, values, nbytes);
    if (!g_cheat) {
        // original: fopen(..., "wt") (0x43313c) sem testar o retorno
        std::FILE *file = std::fopen(s_HighScoreCode_txt_00433128, "w");
        if (file) {
            std::fwrite(g_hiscore_code, 1, std::strlen(g_hiscore_code), file);
            std::fclose(file);
        }
        g_clipboard_ptr = &g_clipboard;
        clipboard_init(&g_clipboard);
        g_clipboard.text = static_cast<char *>(std::realloc(g_clipboard.text, 0xe));
        std::sprintf(g_clipboard.text, "%s", g_hiscore_code);  // 0x433124
        clipboard_set_text(g_clipboard_ptr);
    } else {
        std::memset(g_hiscore_code, '+', 12);  // 3 dwords 0x2b2b2b2b
    }
    remove_int(timer_tick);
    g_ticks_per_second = 0x3c;
    install_int_ex(timer_tick, 0x4dae);  // 1193181 / 60
    MusicStream *stream = g_music_stream;
    g_music_stream = nullptr;
    ogg_stop(stream);
    g_music_stream = ogg_play(g_ogg_theme, 2, 0, int_div_float(g_music_volume, 15.0f));
    g_menu_panel_fade = 0;
    g_substate = 0;
    render_game_frame(g_buffer2);
    g_frame_counter = 0;
    clear_keybuf();
    g_hiscore_state = 0;
    g_enter_latch = 1;
    g_game_state = 4;
}

// 0x0040b7c0 highscore_submit: envia nome + codigo; ok -> estado 2 (posicao), senao 3 (erro).
void highscore_submit() {
    if (highscore_net_open() == 0) {
        if (highscore_query_position(g_hiscore_name, g_hiscore_code, &g_hiscore_position) == 0) {
            g_hiscore_state = 2;
            if (g_hiscore_position != -1) {
                highscore_net_close();
                return;
            }
        }
    }
    g_hiscore_state = 3;
    highscore_net_close();
}

// 0x0040b810 highscore_name_input: edicao de g_hiscore_name (ate 28 caracteres, cursor '#'
// piscando a cada 20 ticks; g_char_under_cursor guarda o caractere coberto pelo cursor).
void highscore_name_input() {
    g_cursor_blink_timer = g_cursor_blink_timer + 1;
    if (g_cursor_blink_timer == 0x14) {
        g_cursor_visible = !g_cursor_visible;
        g_cursor_blink_timer = 0;
        if (g_cursor_visible) g_hiscore_name[g_text_cursor] = '#';
        else g_hiscore_name[g_text_cursor] = g_char_under_cursor;
    }
    // O S que abriu a edicao nao vira letra: espera solta-lo.
    if (!key[KEY_S]) g_enter_latch = 0;
    else if (g_enter_latch) return;
    if (!keypressed()) return;
    const int sc = readkey() >> 8;
    const bool shift = key[KEY_LSHIFT] || key[KEY_RSHIFT];

    // Caractere digitado: letras (Shift = maiuscula), 0..9 (Shift+1 = '!'), ':' e espaco.
    bool typed = false;
    char ch = 0;
    if (sc >= KEY_A && sc <= KEY_Z) {
        ch = static_cast<char>(shift ? sc + 0x40 : sc + 0x60);
        typed = ch != static_cast<char>(0xff);  // original: teste com 0xff (nunca verdadeiro)
    } else if (sc >= KEY_1 && sc <= KEY_9) {
        if (!shift) {
            ch = static_cast<char>(sc + 0x15);
            typed = ch != static_cast<char>(0xff);
        } else if (sc == KEY_1) {
            ch = '!';
            typed = true;
        }
    } else if (sc == KEY_0) {
        ch = '0';
        typed = true;
    } else if (sc == KEY_COLON) {
        ch = ':';
        typed = true;
    } else if (sc == KEY_SPACE) {
        ch = ' ';
        typed = true;
    }
    if (typed && std::strlen(g_hiscore_name) < 0x1c) {
        const int cur = g_text_cursor;
        g_hiscore_name[cur] = g_char_under_cursor;
        for (int i = 0x1c; i > cur; i--) g_hiscore_name[i] = g_hiscore_name[i - 1];
        g_hiscore_name[cur] = ch;
        int pos = cur;
        if (cur < 0x1d) {
            pos = cur + 1;
            g_text_cursor = pos;
        }
        g_char_under_cursor = g_hiscore_name[pos];
        g_hiscore_name[pos] = '#';
        g_cursor_blink_timer = 0;
        g_cursor_visible = 1;
    }

    // Cursor de volta em `pos` (rotina comum de 0x40ba77).
    auto show_cursor_at = [](int pos) {
        g_char_under_cursor = g_hiscore_name[pos];
        g_hiscore_name[pos] = '#';
        g_cursor_blink_timer = 0;
        g_cursor_visible = 1;
        clear_keybuf();
    };
    int cur = g_text_cursor;
    const char under = g_char_under_cursor;
    if (sc == KEY_BACKSPACE || sc == KEY_DEL) {
        if (sc == KEY_BACKSPACE) {
            if (cur <= 0) {
                clear_keybuf();
                return;
            }
            g_hiscore_name[cur] = under;
            cur = cur - 1;
            g_text_cursor = cur;
        }
        int i = cur;
        for (; i < 0x1c; i++) g_hiscore_name[i] = g_hiscore_name[i + 1];
        g_hiscore_name[i] = 0;
        show_cursor_at(cur);
        return;
    }
    if (sc == KEY_LEFT) {
        if (cur > 0) {
            g_hiscore_name[cur] = under;
            g_char_under_cursor = g_hiscore_name[cur - 1];
            g_text_cursor = cur - 1;
            g_cursor_blink_timer = 0;
            g_hiscore_name[cur - 1] = '#';
            g_cursor_visible = 1;
        }
        clear_keybuf();
        return;
    }
    if (sc == KEY_RIGHT) {
        if (cur >= 0x1d || under == 0 || g_hiscore_name[cur] == 0) {
            clear_keybuf();
            return;
        }
        g_hiscore_name[cur] = under;
        cur = cur + 1;
        g_text_cursor = cur;
        show_cursor_at(cur);
        return;
    }
    if (sc == KEY_ESC) {
        // original: restaura a partir do backup do nome de slot do menu (g_slot_name_backup)
        g_enter_latch = 1;
        g_cursor_blink_timer = 0;
        g_cursor_visible = 0;
        std::strcpy(g_hiscore_name, g_slot_name_backup);
        clear_keybuf();
        g_hiscore_state = 0;
        return;
    }
    if (sc == KEY_ENTER) {
        g_hiscore_name[cur] = under;
        g_enter_latch = 1;
        g_cursor_blink_timer = 0;
        g_cursor_visible = 0;
        highscore_submit();
    }
    clear_keybuf();
}

// 0x0040f4b0 highscore_net_open. Original: InternetOpenA("InetURL/1.0") e InternetConnectA(
// "retrospec.sgn.net"). O servidor nao existe mais: o port nao abre conexao e devolve a falha do
// InternetOpenA (1).
int highscore_net_open() {
    g_inet_session = nullptr;  // port: InternetOpenA(s_InetURL_1_0_00433490, ...) sempre "falha"
    if (g_inet_session == nullptr) return 1;
    g_inet_connect = nullptr;  // InternetConnectA(g_inet_session, s_retrospec_sgn_net_0043347c, ...)
    if (g_inet_connect == nullptr) return 2;
    return 0;
}

// 0x0040f500 highscore_net_close. Original: InternetCloseHandle(g_inet_connect) e
// InternetCloseHandle(g_inet_session) quando nao nulos (sem zera-los). No port os handles sao
// sempre nulos: nada a fechar.
void highscore_net_close() {}

// 0x0040f530 url_escape_plus: copia trocando '+' por "%2B".
void url_escape_plus(const char *src, char *dst) {
    const int len = str_len(src);
    if (len < 1) {
        *dst = 0;
        return;
    }
    int j = 0;
    int i = 0;
    do {
        if (src[i] == '+') {
            dst[j] = '%';
            dst[j + 1] = '2';
            dst[j + 2] = 'B';
            j = j + 3;
        } else {
            dst[j] = src[i];
            j = j + 1;
        }
        i = i + 1;
    } while (i < len);
    dst[j] = 0;
}

// 0x0040f590 str_is_digits
bool str_is_digits(const char *s) {
    const int len = str_len(s);
    for (int i = 0; i < len; i++)
        if (s[i] > '9' || s[i] < '0') return false;
    return true;
}

// 0x0040f5d0 highscore_query_position. Original: GET "/highscore.php?yourname=%s&code=%s&
// positiononly=" por HttpOpenRequestA/HttpSendRequestA e le a resposta (InternetReadFile, 0x400
// bytes por vez); cada bloco so de digitos vira *position (sscanf "%i"). 0 ok, 3 falha ao abrir
// o pedido, 4 falha ao enviar. No port nao ha servidor: o pedido nunca abre (3).
int highscore_query_position(const char *name, const char *code, int *position) {
    char escaped[256];
    char url[1000];
    url_escape_plus(code, escaped);
    *position = -1;
    std::sprintf(url, s__highscore_php_yourname__s_code__004334a0, name, escaped);
    const void *request = nullptr;  // port: HttpOpenRequestA(g_inet_connect, "GET", url, ...)
    if (request == nullptr) return 3;
    return 4;
}

// ================================================================== estados 2, 3 e 4

// 0x00406f10 planet_screen_start: estado 2 (mapa "THE BLACKTOOTH EMPIRE"). from_menu: novo jogo
// (g_load_slot == -1, zera os planetas) ou carregar; senao vem do jogo (coroa): musica do tema a
// 60 Hz e o quadro do jogo guardado em g_buffer2 para a transicao de volta.
void planet_screen_start(bool from_menu) {
    if (!from_menu) {
        remove_int(timer_tick);
        g_ticks_per_second = 0x3c;
        install_int_ex(timer_tick, 0x4dae);
        MusicStream *stream = g_music_stream;
        g_music_stream = nullptr;
        ogg_stop(stream);
        g_music_stream = ogg_play(g_ogg_theme, 2, 0, int_div_float(g_music_volume, 15.0f));
        g_ticker_timer = -1;
        g_menu_panel_fade = 0;
        g_saved_frame_counter = g_frame_counter;
        render_game_frame(g_buffer2);
    } else {
        if (g_load_slot == -1) {
            for (int i = 0; i < 5; i++) g_planets_freed[i] = 0;  // original: dword + byte (DAT_0091e460)
        } else {
            load_game();
        }
        if (g_ticker_timer < 0xb4) g_ticker_timer = 0xb4;
        g_menu_panel_fade = 0xf0;
    }
    g_frame_counter = 0;
    g_substate_timer = 0;
    g_substate = 0;
    g_game_state = 2;
    g_planet_from_menu = from_menu;
}

// 0x004072f0 pause_menu_open: ESC/PAUSE no jogo -> estado 3 (menu 7) sobre o quadro escurecido.
void pause_menu_open() {
    g_saved_frame_counter = g_frame_counter;
    draw_sprite_lit(g_buffer2, g_backbuffer, 0, 0, 0x80);
    hud_draw(g_buffer2);
    g_menu_count[7] = (g_fish_count > 0) + 2;  // SAVE GAME so com peixe (DAT_00431d08)
    menu_return_to_main();
    menu_open(7, 0);
    g_game_state = 3;
}

// 0x0040bb20 update_state_result (estado 4). g_hiscore_state: 0 codigo mostrado, 1 digitando o
// nome, 2 enviado, 3 erro. Tecla (depois de 100 ticks) -> fade -> menu principal.
void update_state_result() {
    if (g_frame_counter == 99 && g_substate == 0) clear_keybuf();
    const char first = g_hiscore_name[0];
    g_menu_fade = g_menu_fade - 1;
    if (g_substate == 0) {
        if (g_hiscore_state == 1) {
            highscore_name_input();
            return;
        }
        if (g_hiscore_state == 0) {
            if (key[KEY_S] && !g_cheat) {
                std::memset(g_hiscore_name, 0, 30);
                g_char_under_cursor = g_hiscore_name[0];
                g_hiscore_name[0] = '#';
                g_cursor_blink_timer = 0;
                g_cursor_visible = 1;
                g_text_cursor = 0;
                g_enter_latch = 1;
                clear_keybuf();
                g_hiscore_state = 1;
                return;
            }
        } else if (g_hiscore_state == 3 && key[KEY_S]) {
            g_hiscore_name[0] = '#';
            g_char_under_cursor = first;
            g_cursor_blink_timer = 0;
            g_cursor_visible = 1;
            g_text_cursor = 0;
            g_enter_latch = 1;
            clear_keybuf();
            g_hiscore_state = 1;
            return;
        }
        if ((keypressed() || g_input_fire != 0 || g_input_swap != 0 || g_input_jump != 0 ||
             g_input_carry != 0) &&
            g_frame_counter > 100) {
            if (!g_enter_latch) {
                g_substate = 1;
                g_menu_fade = 0x14;
                return;
            }
            if (!key[KEY_ENTER]) {
                g_enter_latch = 0;
                return;
            }
        }
    } else if (g_substate == 1 && g_menu_fade < 0) {
        clear_keybuf();
        menu_return_to_main();
    }
}

// 0x0040bcc0 update_state_empire_map (estado 2): espera o letreiro e o painel do menu sumirem;
// sub 0 fade-in 12 ticks, 1 espera tecla, 2 fade-out 12 ticks (baixando a musica), 3 transicao de
// 0x34 ticks -> estado 1 com Game.ogg.
void update_state_empire_map() {
    if (g_ticker_timer != -1) {
        g_ticker_timer = g_ticker_timer + 1;
        if (g_ticker_timer == 200) g_ticker_timer = -1;
    }
    if (g_menu_panel_fade > 0) g_menu_panel_fade = g_menu_panel_fade - 0x14;
    if (g_ticker_timer != -1 || g_menu_panel_fade != 0) return;
    if (g_substate == 0 && g_substate_timer == 0) {
        tint_bitmap(g_font_medium, g_font_tinted, 0x78, 200, 0xcd, 0, -1, 0, 0, 0, 0, 0, 1);
        tint_bitmap(g_font_w18, g_font_title, 0, 200, 0xd7, 0, -1, 0, 0, 0, 0, 0, 1);
        clear_keybuf();
    }
    g_substate_timer = g_substate_timer + 1;
    switch (g_substate) {
        case 0:
            if (g_substate_timer == 0xc) {
                g_substate_timer = 0;
                g_substate = 1;
            }
            return;
        case 1:
            if ((keypressed() || g_input_fire != 0 || g_input_swap != 0 || g_input_jump != 0 ||
                 g_input_carry != 0) &&
                g_frame_counter > 100) {
                g_substate_timer = 0;
                g_substate = 2;
            }
            if (g_frame_counter == 99) clear_keybuf();
            return;
        case 2: {
            if (g_substate_timer != 0xc) {
                // original: _ftol(g_music_volume * (1/15.0) (0x430260) * (12 - timer)) / 12.0f
                const int vol = static_cast<int>(static_cast<double>(g_music_volume) * 0.06666666666666667 *
                                                 static_cast<double>(0xc - g_substate_timer));
                ogg_set_volume(g_music_stream, int_div_float(vol, 12.0f));
                return;
            }
            clear_keybuf();
            g_substate_timer = 0;
            g_substate = 3;
            if (g_planet_from_menu) {
                if (g_load_slot == -1) new_game();
                render_game_frame(g_buffer2);
                play_sample_at_volume(0xf, g_music_volume);
            }
            g_game_state = 2;
            ogg_set_volume(g_music_stream, int_div_float(0, 12.0f));
            return;
        }
        case 3:
            if (g_substate_timer == 0x34) {
                g_frame_counter = g_planet_from_menu ? 0 : g_saved_frame_counter;
                remove_int(timer_tick);
                g_ticks_per_second = g_game_tps;
                install_int_ex(timer_tick, 0x1234dd / g_game_tps);
                MusicStream *stream = g_music_stream;
                g_game_state = 1;
                g_music_stream = nullptr;
                ogg_stop(stream);
                g_music_stream = ogg_play(g_ogg_game, 2, 0, int_div_float(g_music_volume, 15.0f));
            }
            return;
        default:
            return;
    }
}

// 0x0040d330 draw_end_screen (estado 4): logo, "GAME OVER" ou "PEOPLE SALUTE YOU!" (com o logo
// HEAD OVER HEELS carimbado na agua depois de 100 ticks), estatisticas, classificacao e o codigo
// de recorde, compostos em g_bmp_panel.
void draw_end_screen(BITMAP *dst) {
    char text[256];
    fblend_trans(g_bmp_logo_shadow, dst, 0xbc, 0x35, 0x3c);
    draw_sprite(dst, g_bmp_logo, 0xb9, 0x32);
    clear_to_color(g_bmp_panel, g_mask_color);
    if (g_lives[0] == 0 || g_lives[1] == 0) {
        std::sprintf(text, "%s", s_GAME_OVER_0043333c);
    } else {
        std::sprintf(text, "%s", s_PEOPLE_SALUTE_YOU__00433348);
        if (g_frame_counter > 100) {
            int v = (g_frame_counter * 5 - 500) * 2;
            if (v > 0xff) v = 0xff;
            water_stamp_bitmap(g_img_ending_logo, g_water_cur, g_screen_w / 2 - g_img_ending_logo->w / 2,
                               g_screen_h / 2 - g_img_ending_logo->h / 2, static_cast<int16_t>(v));
        }
    }
    draw_text_w18(g_bmp_panel, g_font_w18_tinted[6], text, 0xbe - str_len(text) * 9, 0);  // DAT_0091e014
    std::sprintf(text, s__i___ROOMS_EXPLORED__00433324, g_percent_explored);
    tint_bitmap(g_font_medium, g_font_tinted, 0x78, 0x96, 0xd7, 0, -1, 0, 0, 0, 0, 0, 1);
    draw_text_w18(g_bmp_panel, g_font_tinted, text, 0xbe - str_len(text) * 9, 0x32);
    std::sprintf(text, s_LIBERATED__i_PLANETS__0043330c, g_crowns);
    tint_bitmap(g_font_medium, g_font_tinted, 0xdc, 100, 0xd7, 0, -1, 0, 0, 0, 0, 0, 1);
    draw_text_w18(g_bmp_panel, g_font_tinted, text, 0xbe - str_len(text) * 9, 0x5f);
    std::sprintf(text, "%s", s_RATING_00433304);
    tint_bitmap(g_font_medium, g_font_tinted, 0xa0, 0x96, 0xd7, 0, -1, 0, 0, 0, 0, 0, 1);
    draw_text_w18(g_bmp_panel, g_font_tinted, text, 0xbe - str_len(text) * 9, 0x82);
    int rating = g_percent_explored / 10;
    if (g_crowns == 5 && (g_lives[0] != 0 || g_lives[1] != 0)) rating = 9;
    else if (rating == 9 && g_crowns != 5) rating = 8;
    // original: com 100% explorado e menos de 5 coroas o indice e 10 e le alem de g_rating_names a
    // tabela seguinte do exe (g_key_names, 0x4321a4: [0] = "").
    std::strcpy(text, rating < 10 ? g_rating_names[rating] : g_key_names[rating - 10]);
    draw_text_w18(g_bmp_panel, g_font_w18_tinted[0], text, 0xbe - str_len(text) * 9, 0xaa);
    const int center = g_bmp_panel->w / 2;
    switch (g_hiscore_state) {
        case 0:
            std::sprintf(text, s_Retrospec_High_Score_Code___s_004332e4, g_hiscore_code);
            draw_text_w8(g_bmp_panel, g_font_small, text, center - str_len(text) * 4, 0xd7);
            std::sprintf(text, "%s", s_Press__S__To_Send_The_Code_To_Th_004332b4);
            draw_text_w8(g_bmp_panel, g_font_small, text, center - str_len(text) * 4, 0xea);
            break;
        case 1:
            std::sprintf(text, s_Enter_Your_Name___s_004332a0, g_hiscore_name);
            draw_text_w8(g_bmp_panel, g_font_small, text, 0, 0xea);
            break;
        case 2:
            std::sprintf(text, "%s", s_Congratulations__0043328c);
            draw_text_w8(g_bmp_panel, g_font_small, text, center - str_len(text) * 4, 0xd7);
            std::sprintf(text, s_High_Score_Position___i_00433274, g_hiscore_position);
            draw_text_w8(g_bmp_panel, g_font_small, text, center - str_len(text) * 4, 0xea);
            break;
        case 3:
            std::sprintf(text, s_Retrospec_High_Score_Code___s_004332e4, g_hiscore_code);
            draw_text_w8(g_bmp_panel, g_font_small, text, center - str_len(text) * 4, 0xd7);
            std::sprintf(text, "%s", s_Error__Press__S__to_resend__00433254);
            draw_text_w8(g_bmp_panel, g_font_small, text, center - str_len(text) * 4, 0xea);
            break;
        default:
            break;
    }
    make_silhouette(g_bmp_panel, g_bmp_panel_shadow);
    int shadow_alpha = 0x3c;
    int panel_alpha = 0xff;
    if (g_substate == 1) {
        shadow_alpha = g_menu_fade * 3;
        panel_alpha = g_menu_fade * 0xc;
    }
    fblend_trans(g_bmp_panel_shadow, dst, 0x85, 0xad, shadow_alpha);
    if (g_substate != 1) {
        draw_sprite(dst, g_bmp_panel, 0x82, 0xaa);  // original: draw_256_sprite se 8 bpp
        return;
    }
    fblend_trans(g_bmp_panel, dst, 0x82, 0xaa, panel_alpha);
}

// 0x0040d8f0 draw_empire_screen (estado 2): titulo e os 5 planetas (coroa colorida se libertado,
// cinza senao), com fade conforme g_substate; sub 3 = transicao em colunas para o quadro do jogo.
void draw_empire_screen(BITMAP *dst) {
    int alpha = 0xff;
    if (g_substate == 3) {
        transition_column_wipe(dst, g_substate_timer, true, g_buffer2);
        return;
    }
    if (g_ticker_timer != -1) draw_credits_ticker(dst);
    if (g_menu_panel_fade > 0) {
        fblend_trans(g_bmp_logo_shadow, dst, 0xbc, 0x35, g_menu_panel_fade / 4);
        fblend_trans(g_bmp_logo, dst, 0xb9, 0x32, g_menu_panel_fade);
        fblend_trans(g_bmp_copyright_shadow, dst, 0x6b, 0x1a7, g_menu_panel_fade / 4);
        fblend_trans(g_bmp_copyright, dst, 0x68, 0x1a4, g_menu_panel_fade);
    }
    if (g_ticker_timer != -1) return;
    if (g_menu_panel_fade != 0) return;
    if (g_substate == 0) alpha = g_substate_timer * 0x14;
    else if (g_substate == 1) alpha = 0xff;
    else if (g_substate == 2) alpha = (0xc - g_substate_timer) * 0x14;
    clear_to_color(g_bmp_text_line, g_mask_color);
    draw_text_w18(g_bmp_text_line, g_font_title, s_THE_BLACKTOOTH_EMPIRE_0043335c, 0, 0);
    make_silhouette(g_bmp_text_line, g_bmp_text_line_shadow);
    const int shadow_alpha = alpha / 4;
    fblend_trans(g_bmp_text_line_shadow, dst, 0x85, 0x35, shadow_alpha);
    if (alpha == 0xff) draw_sprite(dst, g_bmp_text_line, 0x82, 0x32);
    else fblend_trans(g_bmp_text_line, dst, 0x82, 0x32, alpha);
    for (int i = 0; i < 5; i++) {
        BITMAP *font = g_font_tinted;
        // original: o nome e desenhado duas vezes (a 1a com g_font_tinted e logo apagada)
        clear_to_color(g_bmp_text_line, g_mask_color);
        draw_text_w18(g_bmp_text_line, font, g_planet_names[i], 0, 0);
        bool gray = false;
        if (!g_planets_freed[i]) {
            font = g_font_medium;
            gray = true;
        }
        clear_to_color(g_bmp_text_line, g_mask_color);
        draw_text_w18(g_bmp_text_line, font, g_planet_names[i], 0, 0);
        make_silhouette(g_bmp_text_line, g_bmp_text_line_shadow);
        fblend_trans(g_icon_crown_shadow, dst, g_planet_x[i] + 3, g_planet_y[i] + 3, shadow_alpha);
        const int len = str_len(g_planet_names[i]);
        fblend_trans(g_bmp_text_line_shadow, dst, (3 - len) * 9 + g_planet_x[i], g_planet_y[i] + 0x39,
                     shadow_alpha);
        BITMAP *crown = gray ? g_icon_crown_gray : g_icon_crown;
        if (alpha == 0xff) {
            draw_sprite(dst, crown, g_planet_x[i], g_planet_y[i]);
            draw_sprite(dst, g_bmp_text_line, g_planet_x[i] - len * 9 + 0x18, g_planet_y[i] + 0x36);
        } else {
            fblend_trans(crown, dst, g_planet_x[i], g_planet_y[i], alpha);
            fblend_trans(g_bmp_text_line, dst, g_planet_x[i] - len * 9 + 0x18, g_planet_y[i] + 0x36, alpha);
        }
    }
}

// 0x0040dcd0 draw_pause_screen (estado 3): quadro escurecido gravado por pause_menu_open + menu.
void draw_pause_screen(BITMAP *dst) {
    blit(g_buffer2, dst, 0, 0, 0, 0, g_screen_w, g_screen_h);
    draw_menu_screen(dst);
}

}  // namespace hoh

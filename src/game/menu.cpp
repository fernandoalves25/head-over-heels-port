// Menus do titulo/pausa, opcoes, controles e Config.dat (HoH.exe 0x407020..0x40e8d0).
//
// Paginas (g_menu_id): 0 principal, 1 NEW/OLD GAME, 2 controles, 3 som, 4 opcoes, 5 confirmar
// saida, 6 slots de save, 7 pausa. Ciclo: menu_open -> g_menu_fade sobe ate 20 -> navegacao ->
// item confirmado (g_menu_choice = 0) -> menu_compose_items (desenho) liga g_menu_closing com
// g_menu_fade = 20 -> menu_update decrementa e, quando < 0, executa a acao do item.
#include "game/menu.h"

#include "compat/platform.h"
#include "game/globals.h"
#include "game/gfx_util.h"
#include "game/intro.h"
#include "game/lang.h"
#include "game/main_loop.h"
#include "game/music.h"
#include "game/sound.h"
#include "game/water.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hoh {

namespace {

// fread(&v, 1, 4, f) do original (x86, little-endian): os bytes que faltarem mantem o valor antigo.
void read_i32(int32_t &v, std::FILE *f) {
    uint32_t u = static_cast<uint32_t>(v);
    uint8_t b[4] = {uint8_t(u), uint8_t(u >> 8), uint8_t(u >> 16), uint8_t(u >> 24)};
    std::fread(b, 1, 4, f);
    v = static_cast<int32_t>(uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 |
                             uint32_t(b[3]) << 24);
}

void write_i32(int32_t v, std::FILE *f) {
    uint32_t u = static_cast<uint32_t>(v);
    uint8_t b[4] = {uint8_t(u), uint8_t(u >> 8), uint8_t(u >> 16), uint8_t(u >> 24)};
    std::fwrite(b, 1, 4, f);
}

// pack_fread(&v, 4, f) do original (int32 little-endian).
void pack_read_i32(int32_t &v, PACKFILE *f) {
    uint32_t u = static_cast<uint32_t>(v);
    uint8_t b[4] = {uint8_t(u), uint8_t(u >> 8), uint8_t(u >> 16), uint8_t(u >> 24)};
    pack_fread(b, 4, f);
    v = static_cast<int32_t>(uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 |
                             uint32_t(b[3]) << 24);
}

// Redefinicao de controle, fases 0 e 2: alguma tecla (0..0x72), botao ou direcao dos eixos 0/1
// do stick 0 de qualquer joystick ainda pressionado? (o original repete este laco inline)
bool redefine_input_held() {
    bool held = false;
    for (int k = 0; k < 0x73; k++)
        if (key[k]) held = true;
    if (g_joystick_enabled && num_joysticks > 0) {
        for (int j = 0; j < num_joysticks; j++) {
            for (int b = 0; b < joy[j].num_buttons; b++)
                if (joy[j].button[b].b) held = true;
            for (int a = 0; a < 2; a++) {
                if (joy[j].stick[0].axis[a].d1) held = true;
                if (joy[j].stick[0].axis[a].d2) held = true;
            }
        }
    }
    return held;
}

// original: a checagem de conflito da redefinicao percorre 12 entradas de g_control_code e
// g_control_type; as duas ultimas caem nas globais seguintes do exe (g_jump_length[0..1] logo
// depois de g_control_code, g_control_joy[0..1] logo depois de g_control_type).
int control_code_slot(int i) { return i < 10 ? g_control_code[i] : g_jump_length[i - 10]; }
int control_type_slot(int i) { return i < 10 ? g_control_type[i] : g_control_joy[i - 10]; }

// Volta ao jogo (RESUME GAME / save concluido), endereco 0x40a9fa dentro de menu_update.
void resume_game() {
    g_game_state = 1;
    remove_int(timer_tick);
    g_ticks_per_second = g_game_tps;
    install_int_ex(timer_tick, 0x1234dd / g_game_tps);
}

}  // namespace

// 0x00407020 menu_open
void menu_open(int menu, int item) {
    g_game_state = 0;
    g_menu_id = menu;
    g_menu_closing = 0;
    g_menu_fade = 0;
    g_menu_text_edit = 0;
    for (int i = 0; i < 13; i++) g_menu_item_indent[i] = 0x12;
    g_menu_highlight = item;
    g_menu_sel = item;
    g_menu_choice = -1;
    g_menu_opening = 1;
    g_menu_item_indent[item] = 0x19;
}

// 0x00407080 menu_return_to_main
void menu_return_to_main() {
    g_ticker_timer = 0;
    g_ticker_index = 0;
    g_key_up_latch = 0;
    g_key_down_latch = 0;
    g_enter_latch = 0;
    g_select_latch = 0;
    g_menu_timer = 0;
    g_water_shape_timer = 0;
    g_water_shape_idx = -1;
    remove_int(timer_tick);
    g_ticks_per_second = 0x3c;
    install_int_ex(timer_tick, 0x4dae);  // 1193181 / 60
    menu_open(0, 0);
}

// 0x00409ca0 menu_update_cursor. g_menu_item_indent[i] e a altura animada do item (0x12 em
// repouso, 0x19 selecionado); g_menu_highlight segue g_menu_sel um passo por tick.
void menu_update_cursor() {
    int h = g_menu_highlight;
    int s = g_menu_sel;
    if (h < s) {
        g_menu_item_indent[h]--;
        g_menu_item_indent[h + 1]++;
        if (g_menu_item_indent[h] == 0x12) {
            h++;
            g_menu_highlight = h;
        }
    }
    if (h > s) {
        g_menu_item_indent[h]--;
        g_menu_item_indent[h - 1]++;
        if (g_menu_item_indent[h] == 0x12) {
            h--;
            g_menu_highlight = h;
        }
    }
    if (g_menu_text_edit || g_redefining_control != 0) return;

    const int last = g_menu_count[g_menu_id] - 1;
    if (g_input[0] == 0 && key[g_menu_key_up] == 0) {
        g_key_up_latch = 0;
    } else if (!g_key_up_latch) {
        g_key_up_latch = 1;
        if (s > 0 && (h == last || (h < last && g_menu_item_indent[h + 1] == 0x12))) {
            g_menu_sel = s - 1;
            play_sfx(0x11);
            h = g_menu_highlight;
            s = g_menu_sel;
        }
    }
    if (g_input_down == 0 && key[g_menu_key_down] == 0) {
        g_key_down_latch = 0;
        return;
    }
    if (!g_key_down_latch) {
        g_key_down_latch = 1;
        if (s < last && (h == 0 || (h > 0 && g_menu_item_indent[h - 1] == 0x12))) {
            g_menu_sel = s + 1;
            play_sfx(0x11);
        }
    }
}

// 0x0040a6a0 menu_update (estados 0 e 3 de game_update)
void menu_update() {
    // original: o argumento e g_menu_id, empurrado por game_update; nao muda ate o retorno
    // (todo menu_open e seguido de return).
    const int menu = g_menu_id;

    if (key[KEY_C] && key[KEY_H] && key[KEY_E] && key[KEY_A] && key[KEY_T]) {
        play_sfx(0x12);
        g_cheat = 1;
    }
    if (g_game_state != 3) {
        g_ticker_timer = g_ticker_timer + 1;
        if (g_ticker_timer == 200) {
            g_ticker_index = g_ticker_index + 1;
            g_ticker_timer = 0;
            if (g_ticker_index == 0x18) g_ticker_index = 0;
        }
        water_update_title_shapes(g_water_cur);
    }
    if (g_menu_fadein > 0) g_menu_fadein = g_menu_fadein - 1;
    if (g_quit_countdown != -1) {  // QUIT TO SYSTEM: wipe para preto e sai
        if (g_quit_countdown != 0) {
            g_quit_countdown = g_quit_countdown - 1;
            g_menu_fadein = 0x34 - g_quit_countdown;
            return;
        }
        g_quit = 1;
        return;
    }

    // ---------------------------------------------------------------- fechando: acao do item
    if (g_menu_closing) {
        g_menu_choice = -1;
        g_menu_fade = g_menu_fade - 1;
        if (g_menu_fade >= 0) return;
        switch (menu) {
            case 0:
                switch (g_menu_sel) {
                    case 0: menu_open(1, 0); return;
                    case 1: menu_open(2, 0); return;
                    case 2: menu_open(3, 0); return;
                    case 3: menu_open(4, 0); return;
                    case 4: menu_open(5, 0); return;
                    default: return;
                }
            case 1:
                if (g_menu_sel == 0) {  // NEW GAME
                    g_load_slot = -1;
                    planet_screen_start(true);
                    return;
                }
                if (g_menu_sel == 1) {  // OLD GAME: slots so de leitura
                    for (int i = 0; i < 6; i++) g_menu_item_adjustable[6][i] = 0;  // 0x431c92/0x431c96
                    g_menu_y[6] = 0xaa;
                    menu_open(6, 0);
                    return;
                }
                if (g_menu_sel == 2) {  // BACK
                    menu_open(0, 0);
                    return;
                }
                menu_open(1, 0);
                return;
            case 2:
                if (g_menu_sel == 10) {  // BACK
                    controls_build_menu_text(true);
                    menu_open(0, 1);
                    return;
                }
                menu_open(2, 0);
                return;
            case 3:
                if (g_menu_sel == 3) menu_open(0, 2);
                else menu_open(3, 0);
                return;
            case 4:  // port: BACK e o ultimo item (6; o original tinha 6 itens e BACK no 5)
                if (g_menu_sel == g_menu_count[4] - 1) menu_open(0, 3);
                else menu_open(4, 0);
                return;
            case 5:
                if (g_menu_sel == 0) {  // YES - QUIT
                    if (g_game_state != 3) {
                        g_quit_countdown = 0x34;
                        clear_to_color(g_buffer2, 0);
                        return;
                    }
                    free_character_rooms();
                    menu_return_to_main();
                    MusicStream *stream = g_music_stream;
                    g_menu_fadein = 0x34;
                    g_music_stream = nullptr;
                    ogg_stop(stream);
                    g_music_stream = ogg_play(g_ogg_theme, 2, 0, int_div_float(g_music_volume, 15.0f));
                    return;
                }
                if (g_menu_sel == 1) {  // BACK
                    if (g_game_state != 3) {
                        menu_open(0, 0);
                        blit(g_backbuffer, g_buffer2, 0, 0, 0, 0, g_screen_w, g_screen_h);
                        return;
                    }
                    menu_open(7, 0);
                    g_game_state = 3;
                }
                return;
            case 6:
                if (g_menu_sel != 6) {
                    if (g_game_state != 3) {  // carregar o slot
                        g_load_slot = g_menu_sel;
                        planet_screen_start(true);
                        return;
                    }
                    g_frame_counter = g_saved_frame_counter;  // jogo gravado: volta ao jogo
                    resume_game();
                    return;
                }
                if (g_game_state != 3) {  // BACK
                    menu_open(1, 1);
                    return;
                }
                menu_open(7, 2);
                g_game_state = 3;
                return;
            case 7:
                if (g_menu_sel == 0) {  // RESUME GAME
                    g_frame_counter = g_saved_frame_counter;
                    resume_game();
                    return;
                }
                if (g_menu_sel == 1) {  // QUIT GAME
                    menu_open(5, 0);
                    g_game_state = 3;
                    return;
                }
                if (g_menu_sel == 2) {  // SAVE GAME: slots editaveis
                    for (int i = 0; i < 6; i++) g_menu_item_adjustable[6][i] = 1;  // 0x431c92/0x431c96
                    g_menu_y[6] = 100;
                    menu_open(6, 0);
                    g_game_state = 3;
                    return;
                }
                return;
            default:
                return;
        }
    }

    if (g_menu_opening) {
        g_menu_fade = g_menu_fade + 1;
        if (g_menu_fade == 0x14) g_menu_opening = 0;
    }
    menu_update_cursor();

    // ---------------------------------------------------------------- edicao do nome do slot
    if (g_menu_text_edit) {
        char *item = g_menu_items[menu][g_menu_sel];
        g_cursor_blink_timer = g_cursor_blink_timer + 1;
        if (g_cursor_blink_timer == 0x14) {
            g_cursor_blink_timer = 0;
            g_cursor_visible = !g_cursor_visible;
            if (g_cursor_visible) item[g_text_cursor] = '_';
            else item[g_text_cursor] = g_char_under_cursor;
        }
        if (!key[KEY_ENTER]) g_enter_latch = 0;
        if (!keypressed()) return;
        play_sfx(0x11);
        const int scancode = readkey() >> 8;
        int cursor = g_text_cursor;

        int c = -1;  // caractere a inserir
        if (scancode >= 1 && scancode <= 0x1a) {
            c = scancode + 0x40;  // KEY_A..KEY_Z -> 'A'..'Z'
        } else if (scancode > 0x1b && scancode < 0x25) {
            c = scancode + 0x15;  // KEY_1..KEY_9 -> '1'..'9'
        } else {
            switch (scancode) {
                case KEY_0: c = '0'; break;
                case KEY_MINUS: c = '-'; break;
                case KEY_OPENBRACE: c = '['; break;
                case KEY_CLOSEBRACE: c = ']'; break;
                case KEY_COLON: c = ':'; break;
                case KEY_COMMA: c = ','; break;
                case KEY_STOP: c = '.'; break;
                case KEY_SPACE: c = ' '; break;
                default: break;
            }
        }
        // (o original ainda compara o caractere com 0xff antes de inserir; nunca acontece)
        if (c != -1 && cursor < 0xf) {  // insere na posicao do cursor (nome de 15 caracteres)
            item[cursor] = g_char_under_cursor;
            for (int i = 0xe; i > cursor; i--) item[i] = item[i - 1];
            g_cursor_blink_timer = 0;
            item[cursor] = static_cast<char>(c);
            g_char_under_cursor = item[cursor + 1];
            cursor = cursor + 1;
            g_text_cursor = cursor;
            item[cursor] = '_';
            g_cursor_visible = 1;
        }

        bool move_cursor = false;  // 0x40adba: guarda o caractere sob o cursor e desenha '_'
        if (scancode == KEY_BACKSPACE || scancode == KEY_DEL) {
            bool erase = true;
            if (scancode == KEY_BACKSPACE) {
                if (cursor < 1) {
                    erase = false;
                } else {
                    cursor = cursor - 1;
                    g_text_cursor = cursor;
                    item[cursor + 1] = g_char_under_cursor;
                }
            }
            if (erase) {  // apaga na posicao do cursor puxando o resto (ate o indice 14)
                int i = cursor;
                for (; i < 0xe; i++) item[i] = item[i + 1];
                item[i] = ' ';
                move_cursor = true;
            }
        } else if (scancode == KEY_LEFT) {
            if (cursor >= 1) {
                item[cursor] = g_char_under_cursor;
                cursor = cursor - 1;
                g_text_cursor = cursor;
                move_cursor = true;
            }
        } else if (scancode == KEY_RIGHT) {
            if (cursor < 0xf) {
                item[cursor] = g_char_under_cursor;
                cursor = cursor + 1;
                g_text_cursor = cursor;
                move_cursor = true;
            }
        } else if (scancode == KEY_ESC) {  // desiste: restaura o rotulo e volta a pausa
            std::strcpy(item, g_slot_name_backup);
            menu_open(7, 0);
            g_game_state = 3;
            g_menu_text_edit = 0;
            g_enter_latch = 1;
            g_cursor_blink_timer = 0;
            g_cursor_visible = 0;
            clear_keybuf();
            return;
        } else if (scancode == KEY_ENTER && !g_enter_latch) {  // confirma e grava
            std::sprintf(item + 0xf, s__2i___0043321c, g_percent_explored);
            g_enter_latch = 1;
            g_menu_items[menu][g_menu_sel][g_text_cursor] = g_char_under_cursor;
            g_fish_count = g_fish_count - 1;
            g_menu_choice = 0;
            g_menu_text_edit = 0;
            g_cursor_blink_timer = 0;
            g_cursor_visible = 0;
            save_game();
            clear_keybuf();
            return;
        }
        if (move_cursor) {
            g_char_under_cursor = item[cursor];
            item[cursor] = '_';
            g_cursor_blink_timer = 0;
            g_cursor_visible = 1;
        }
        // original: reescreve " NN%" em item+15 a cada tecla (apaga um '_' na posicao 15)
        std::sprintf(item + 0xf, s__2i___0043321c, g_percent_explored);
        clear_keybuf();
        return;
    }

    if (menu == 4) options_text_frame_rate();  // port: o F9 tambem muda o modo com o menu aberto

    if (g_redefining_control == 0) {
        // ------------------------------------------------------------ navegacao normal
        const bool left = g_input_left != 0;
        const bool right = g_input_right != 0;
        if (key[KEY_ENTER] || g_input_carry != 0 || g_input_jump != 0 || g_input_fire != 0 ||
            (g_menu_item_adjustable[menu][g_menu_sel] && (left || right))) {
            if (!g_menu_item_adjustable[menu][g_menu_sel]) {
                if (g_menu_choice == -1) {  // item normal: confirma
                    play_sfx(0x12);
                    g_menu_choice = 0;
                }
            } else if (!g_select_latch) {  // item ajustavel: LEFT diminui, o resto aumenta
                play_sfx(0x11);
                g_select_latch = 1;
                switch (menu) {
                    case 2:  // controle: comeca a redefinicao
                        g_redefining_control = 1;
                        std::sprintf(g_menu_items[2][g_menu_sel], "%s_",  // 0x433218
                                     g_control_labels[g_menu_sel]);
                        g_redefine_phase = 0;
                        break;
                    case 3:
                        if (g_menu_sel == 0) {
                            if (left) {
                                g_sound_mode = g_sound_mode - 1;
                                if (g_sound_mode < 0) g_sound_mode = 2;
                            } else {
                                g_sound_mode = g_sound_mode + 1;
                                if (g_sound_mode > 2) g_sound_mode = 0;
                            }
                            options_text_sound_mode();
                        } else if (g_menu_sel == 1) {
                            if (left) {
                                g_music_volume = g_music_volume - 1;
                                if (g_music_volume < 0) g_music_volume = 0xf;
                            } else {
                                g_music_volume = g_music_volume + 1;
                                if (g_music_volume > 0xf) g_music_volume = 0;
                            }
                            options_text_music_volume();
                            ogg_set_volume(g_music_stream, int_div_float(g_music_volume, 15.0f));
                        } else if (g_menu_sel == 2) {
                            if (left) {
                                g_sfx_volume = g_sfx_volume - 1;
                                if (g_sfx_volume < 0) g_sfx_volume = 0xf;
                            } else {
                                g_sfx_volume = g_sfx_volume + 1;
                                if (g_sfx_volume > 0xf) g_sfx_volume = 0;
                            }
                            options_text_sound_volume();
                        }
                        break;
                    case 4:
                        switch (g_menu_sel) {
                            case 0:  // SHADOWS
                                if (left) {
                                    g_shadows_option = g_shadows_option - 1;
                                    if (g_shadows_option < 0) g_shadows_option = 2;
                                } else {
                                    g_shadows_option = g_shadows_option + 1;
                                    if (g_shadows_option > 2) g_shadows_option = 0;
                                }
                                options_apply_shadows();
                                break;
                            case 1:  // GAME SPEED
                                if (left) {
                                    g_game_speed_option = g_game_speed_option - 1;
                                    if (g_game_speed_option < 0) g_game_speed_option = 2;
                                } else {
                                    g_game_speed_option = g_game_speed_option + 1;
                                    if (g_game_speed_option > 2) g_game_speed_option = 0;
                                }
                                options_apply_game_speed();
                                break;
                            case 2:  // GFX MODE: alterna janela / tela cheia (nao com -b)
                                if (!g_convert_colors) {
                                    g_windowed = !g_windowed;
                                    options_text_gfx_mode();
                                    int card;
                                    if (g_windowed) {
                                        // original: win_set_wnd_create_proc(0x402200), procedimento
                                        // de janela Win32; sem equivalente na camada de plataforma.
                                        card = GFX_DIRECTX_WIN;
                                    } else {
                                        card = GFX_DIRECTX;
                                    }
                                    if (set_gfx_mode(card, g_screen_w, g_screen_h, 0, 0) < 0) {
                                        allegro_exit();
                                        allegro_message("%s", s_Error_setting_graphics_mode__Exi_004331f0);
                                    }
                                    // original: em janela chama center_window() (0x402180, MoveWindow
                                    // do Win32); no port a camada de plataforma posiciona a janela.
                                    triple_buffer_init();
                                    if ((gfx_capabilities & GFX_CAN_TRIPLE_BUFFER) == 0) enable_triple_buffer();
                                    if (!g_triple_buffer) g_render_target = screen;
                                }
                                break;
                            case 3:  // TRIPLE BUFFERING
                                if (g_triple_available) {
                                    if (g_triple_buffer) {
                                        g_triple_buffer = 0;
                                        g_render_target = screen;
                                        request_video_bitmap(screen);
                                        show_video_bitmap(screen);
                                        options_text_triple_buffer();
                                        break;
                                    }
                                    g_triple_buffer = 1;
                                    g_render_target = g_video_page0;
                                    g_video_page_index = 0;
                                }
                                options_text_triple_buffer();
                                break;
                            case 4:  // C.SENSITIVITY
                                g_csens_high = !g_csens_high;
                                options_text_sensitivity();
                                break;
                            case 5:  // port: FRAME RATE (o mesmo ciclo do F9)
                                platform_cycle_fps_mode(left ? -1 : 1);
                                options_text_frame_rate();
                                break;
                            case 6:  // port: LANGUAGE (troca na hora; grava no port.cfg)
                                lang_set(static_cast<Lang>((lang_current() + (left ? LANG_COUNT - 1 : 1)) % LANG_COUNT));
                                options_text_language();
                                break;
                            default:
                                break;
                        }
                        break;
                    case 6: {  // slot no menu de pausa: comeca a editar o nome
                        char *item = g_menu_items[menu][g_menu_sel];
                        std::strcpy(g_slot_name_backup, item);
                        if (std::strcmp(g_slot_name_backup, s___EMPTY_____004331dc) == 0)
                            std::memset(item, ' ', 16);
                        g_char_under_cursor = item[0];
                        g_menu_text_edit = 1;
                        item[0] = '_';
                        g_cursor_blink_timer = 0;
                        g_cursor_visible = 1;
                        g_text_cursor = 0;
                        g_enter_latch = 1;
                        clear_keybuf();
                        std::sprintf(item + 0x10, s__2i___004331d4, g_percent_explored);
                        break;
                    }
                    default:  // menus 0, 1, 5 e 7 nao tem itens ajustaveis
                        break;
                }
            }
        } else {
            g_enter_latch = 0;
            g_select_latch = 0;
        }
    } else if (g_redefine_phase == 0) {
        // ------------------------------------------------------------ redefinicao: fase 0
        if (!redefine_input_held()) g_redefine_phase = 1;  // espera soltar tudo
    } else if (g_redefine_phase == 1) {
        // ------------------------------------------------------------ fase 1: le o novo controle
        int code = -1, type = -1, joy_index = -1;
        for (int k = 0; k < 0x73; k++) {
            if (key[k]) {  // tipo 0: tecla (a de maior codigo vence)
                type = 0;
                code = k;
            }
        }
        if (g_joystick_enabled && code == -1) {
            for (int j = 0; j < num_joysticks; j++) {
                for (int b = 0; b < joy[j].num_buttons; b++) {
                    if (joy[j].button[b].b) {  // tipo 1: botao
                        type = 1;
                        joy_index = j;
                        code = b;
                    }
                }
                for (int a = 0; a < 4; a += 2) {  // tipo 2: eixo (a/2) do stick 0, codigo = eixo*2 + sentido
                    if (joy[j].stick[0].axis[a / 2].d1) {
                        type = 2;
                        joy_index = j;
                        code = a;
                    }
                    if (joy[j].stick[0].axis[a / 2].d2) {
                        code = a + 1;
                        type = 2;
                        joy_index = j;
                    }
                }
            }
        }
        if (code != -1) {
            bool unique = true;  // recusa se outro controle ja usa o mesmo codigo e tipo
            for (int i = 0; i < 12; i++)
                if (i != g_menu_sel && control_code_slot(i) == code && control_type_slot(i) == type)
                    unique = false;
            if (unique) {
                play_sfx(0x12);
                g_control_code[g_menu_sel] = code;
                g_control_type[g_menu_sel] = type;
                g_control_joy[g_menu_sel] = joy_index;
                controls_build_menu_text(true);
                g_redefine_phase = 2;
            }
        }
        // original: compara so o codigo (vale tambem para botao/eixo 59)
        if (code == KEY_ESC) {
            play_sfx(0x12);
            controls_build_menu_text(true);
            g_redefine_phase = 2;
        }
    } else {
        // ------------------------------------------------------------ fase 2: espera soltar
        if (!redefine_input_held()) g_redefining_control = 0;
    }

    // ESC/PAUSE nos menus do titulo seleciona o ultimo item (BACK)
    if (!key[KEY_ESC] && g_input_pause == 0) return;
    if (menu == 5) return;
    if (g_game_state == 3) return;
    if (g_redefining_control != 0) return;
    if (g_menu_choice == 0) return;
    play_sfx(0x12);
    g_menu_choice = 0;
    g_menu_highlight = g_menu_count[menu] - 1;
    g_menu_sel = g_menu_count[menu] - 1;
}

// 0x0040cdb0 menu_compose_items: desenha os itens da pagina em g_bmp_panel. Tambem e aqui que o
// item confirmado (g_menu_choice == 0) liga g_menu_closing e reinicia g_menu_fade.
void menu_compose_items() {
    clear_to_color(g_bmp_panel, g_mask_color);
    int y = 0;
    if (g_game_state == 3 && g_menu_id == 6) y = 0x22;  // espaco para "NN SAVES LEFT"
    for (int i = 0; i < g_menu_count[g_menu_id]; i++) {
        int off = 0;
        clear_to_color(g_bmp_text_line, g_mask_color);
        // fonte pela altura do item: g_font_w18_tinted[altura - 18] (0x91dfb4 + altura*4)
        draw_text_w18(g_bmp_text_line, g_font_w18_tinted[g_menu_item_indent[i] - 0x12],
                      tr_menu(g_menu_id, g_menu_items[g_menu_id][i]), 0, 0);  // port: tr_menu (idiomas)
        if (g_menu_choice == 0 && i == g_menu_sel) {
            off = 3;
            g_menu_closing = 1;
            g_menu_fade = 0x14;
        }
        if (i == g_menu_sel && g_menu_fade > 0xf && g_menu_closing) off = 3;
        if (g_menu_closing && i == g_menu_sel && g_game_state != 3)  // marca o item na agua
            water_stamp_bitmap(g_bmp_text_line, g_water_cur, g_menu_x[g_menu_id], g_menu_y[g_menu_id] + y,
                               -127);
        if (g_menu_item_indent[i] == 0x19)
            draw_sprite(g_bmp_panel, g_bmp_text_line, off, off + y);
        else
            stretch_sprite(g_bmp_panel, g_bmp_text_line, off, off + y, g_bmp_text_line->w,
                           g_menu_item_indent[i]);
        int gap;
        if (g_menu_id == 2) gap = (i != g_menu_count[2] - 2) ? 4 : 9;  // +9 antes de BACK
        else gap = 0xe;
        y = y + g_menu_item_indent[i] + gap;
    }
}

// 0x0040cf90 draw_menu_screen
void draw_menu_screen(BITMAP *dst) {
    char text[256];
    if (g_game_state != 3) {  // logo, copyright e letreiro so nos menus do titulo
        fblend_trans(g_bmp_logo_shadow, dst, 0xbc, 0x35, 0x3c);
        draw_sprite(dst, g_bmp_logo, 0xb9, 0x32);
        fblend_trans(g_bmp_copyright_shadow, dst, 0x6b, 0x1a7, 0x3c);
        draw_sprite(dst, g_bmp_copyright, 0x68, 0x1a4);
        draw_credits_ticker(dst);
    }
    menu_compose_items();
    if (g_game_state == 3 && g_menu_id == 6) {
        // port: tr (idiomas)
        if (g_fish_count > 1) std::snprintf(text, sizeof text, tr(s__2i_SAVES_LEFT_00433244), g_fish_count);
        else if (g_fish_count == 1) std::snprintf(text, sizeof text, "%s", tr(s_1_SAVE_LEFT_00433234));
        else std::snprintf(text, sizeof text, "%s", tr(s_NO_SAVES_LEFT_00433224));
        tint_bitmap(g_font_medium, g_font_tinted, 100, 200, 0xff, 0, 0, 0, 0, 0, 0, 0, 1);
        draw_text_w18(g_bmp_panel, g_font_tinted, text, 0x32, 0);
    }
    make_silhouette(g_bmp_panel, g_bmp_panel_shadow);
    int shadow_alpha = 0x3c;
    int panel_alpha = 0xff;
    if (g_menu_closing || g_menu_opening) {
        shadow_alpha = g_menu_fade * 3;
        panel_alpha = g_menu_fade * 0xc;
    }
    fblend_trans(g_bmp_panel_shadow, dst, g_menu_x[g_menu_id] + 3, g_menu_y[g_menu_id] + 3, shadow_alpha);
    if (!g_menu_closing && !g_menu_opening) {
        draw_sprite(dst, g_bmp_panel, g_menu_x[g_menu_id], g_menu_y[g_menu_id]);
        return;
    }
    fblend_trans(g_bmp_panel, dst, g_menu_x[g_menu_id], g_menu_y[g_menu_id], panel_alpha);
}

// 0x0040a0c0 options_text_sound_mode
void options_text_sound_mode() {
    std::sprintf(g_menu_items[3][0], "%s", g_sound_mode_names[g_sound_mode]);  // 0x43162c, "%s" 0x433144
}

// 0x0040a0f0 options_text_music_volume
void options_text_music_volume() {
    std::sprintf(g_menu_items[3][1], s_MUSIC_VOLUME___2i_00433148, g_music_volume);
}

// 0x0040a110 options_text_sound_volume
void options_text_sound_volume() {
    std::sprintf(g_menu_items[3][2], s_SOUND_VOLUME___2i_0043315c, g_sfx_volume);
}

// 0x0040a130 options_apply_shadows
void options_apply_shadows() {
    std::sprintf(g_menu_items[4][0], "%s", g_shadow_mode_names[g_shadows_option]);
    if (g_shadows_option == 0) {
        g_shadows_on = 0;
        g_shadows_complex = 0;
    } else if (g_shadows_option == 1) {
        g_shadows_on = 1;
        g_shadows_complex = 0;
    } else if (g_shadows_option == 2) {
        g_shadows_on = 1;
        g_shadows_complex = 1;
    }
}

// 0x0040a190 options_apply_game_speed
void options_apply_game_speed() {
    std::sprintf(g_menu_items[4][1], "%s", g_game_speed_names[g_game_speed_option]);
    if (g_game_speed_option == 0) g_game_tps = 0x19;
    else if (g_game_speed_option == 1) g_game_tps = 0x1e;
    else if (g_game_speed_option == 2) g_game_tps = 0x23;
}

// 0x0040a1f0 options_text_triple_buffer
void options_text_triple_buffer() {
    int index;
    if (!g_triple_available) index = 2;  // N-A
    else index = g_triple_buffer != 0;
    std::sprintf(g_menu_items[4][3], "%s", g_triple_buffer_names[index]);
}

// 0x0040a230 options_text_sensitivity
void options_text_sensitivity() {
    std::sprintf(g_menu_items[4][4], "%s", g_csens_names[g_csens_high == 0]);  // [0] HIGH, [1] LOW
}

// 0x0040a350 options_text_gfx_mode
void options_text_gfx_mode() {
    if (g_windowed) {
        std::sprintf(g_str_gfx_mode, "%s", s_GFX_MODE__WINDOWED_004331a8);
        return;
    }
    std::sprintf(g_str_gfx_mode, "%s", s_GFX_MODE__FULL_SCREEN_00433190);
}

// port: GAME OPTIONS ganha FRAME RATE (quadros por segundo; no R36S nao ha F9) no lugar do BACK e
// LANGUAGE (idioma dos textos); o BACK desce para o item 7. O painel sobe 30 px para o menu
// continuar centrado.
void options_port_init() {
    std::snprintf(g_menu_items[4][7], sizeof g_menu_items[4][7], "BACK");
    g_menu_item_adjustable[4][5] = 1;
    g_menu_item_adjustable[4][6] = 1;
    g_menu_item_adjustable[4][7] = 0;
    g_menu_count[4] = 8;
    g_menu_y[4] = 140;
    options_text_frame_rate();
    options_text_language();
}

// "LANGUAGE:     ENGLISH" (21 colunas); nos outros idiomas tr_menu poe o nome do idioma atual.
void options_text_language() {
    std::snprintf(g_menu_items[4][6], sizeof g_menu_items[4][6], "LANGUAGE:%12s", "ENGLISH");
}

// "FRAME RATE:      AUTO" / "FRAME RATE:   120 FPS" (21 colunas, como os outros itens)
void options_text_frame_rate() {
    char value[16];
    if (platform_fps_mode() < 0) std::snprintf(value, sizeof value, "AUTO");
    else std::snprintf(value, sizeof value, "%d FPS", platform_fps_target());
    std::snprintf(g_menu_items[4][5], sizeof g_menu_items[4][5], "FRAME RATE:%10s", value);
}

// 0x00407360 input_read_controls: g_control_type 0 = tecla key[code], 1 = botao code do joystick
// g_control_joy, 2 = eixo (code>>1)&1 do stick 0, sentido negativo (code par) ou positivo.
void input_read_controls() {
    for (int i = 0; i < 10; i++) {
        const int type = g_control_type[i];
        if (type == 0) {
            g_input[i] = static_cast<signed char>(key[g_control_code[i]]);
        } else if (type == 1) {
            g_input[i] = joy[g_control_joy[i]].button[g_control_code[i]].b;
        } else if (type == 2) {
            const JOYSTICK_AXIS_INFO &axis = joy[g_control_joy[i]].stick[0].axis[(g_control_code[i] >> 1) & 1];
            if ((g_control_code[i] & 1) == 0) g_input[i] = axis.d1;
            else g_input[i] = axis.d2;
        }
    }
    // Ctrl+F liga/desliga o contador de FPS
    if ((!key[KEY_LCONTROL] && !key[KEY_RCONTROL]) || !key[KEY_F]) {
        g_fps_toggle_latch = 0;
    } else if (!g_fps_toggle_latch) {
        g_show_fps = !g_show_fps;
        g_fps_toggle_latch = 1;
    }
}

// 0x0040a260 controls_build_menu_text: textos do menu 2 ("UP          UP    ", "FIRE        JP0-03"...).
// Com full, tambem decide se as setas navegam os menus: g_menu_key_up/down = KEY_UP/KEY_DOWN, ou 0
// (key[0], nunca pressionada) se a seta ja e o controle de algum item.
void controls_build_menu_text(bool full) {
    for (int i = 0; i < 10; i++) {
        char *dest = g_menu_items[2][i];
        const int type = g_control_type[i];
        if (type == 0)
            std::sprintf(dest, "%s%s", g_control_labels[i], g_key_names[g_control_code[i]]);  // 0x433170
        else if (type == 1)
            std::sprintf(dest, s__sJP_i__02i_00433178, g_control_labels[i], g_control_joy[i], g_control_code[i]);
        else if (type == 2)
            std::sprintf(dest, s__sJS_i__02i_00433184, g_control_labels[i], g_control_joy[i], g_control_code[i]);
    }
    if (full) {
        g_menu_key_up = KEY_UP;
        g_menu_key_down = KEY_DOWN;
        for (int i = 0; i < 10; i++) {
            if (g_control_code[i] == KEY_UP && g_control_type[i] == 0) g_menu_key_up = 0;
            if (g_control_code[i] == KEY_DOWN && g_control_type[i] == 0) g_menu_key_down = 0;
        }
    }
}

// 0x0040bf80 input_any_pressed: alguma tecla ou botao de joystick (eixos nao contam).
bool input_any_pressed() {
    bool pressed = false;
    for (int k = 0; k < 0x73; k++)
        if (key[k]) pressed = true;
    if (g_joystick_enabled && !pressed && num_joysticks > 0) {
        for (int j = 0; j < num_joysticks; j++)
            for (int b = 0; b < joy[j].num_buttons; b++)
                if (joy[j].button[b].b) pressed = true;
    }
    return pressed;
}

// 0x0040a390 config_load. Config.dat (143 bytes) e lido com stdio ("rb"), sem packfile:
// 5 int, 10 x {code, type, joy}, 3 bytes (g_windowed, g_triple_buffer, g_csens_high).
void config_load() {
    std::FILE *f = std::fopen(s_Config_dat_004331c0, "rb");  // "rb" = DAT_004331cc
    if (f) {
        read_i32(g_sound_mode, f);
        read_i32(g_music_volume, f);
        read_i32(g_sfx_volume, f);
        read_i32(g_shadows_option, f);
        read_i32(g_game_speed_option, f);
        for (int i = 0; i < 10; i++) {
            read_i32(g_control_code[i], f);
            read_i32(g_control_type[i], f);
            read_i32(g_control_joy[i], f);
        }
        std::fread(&g_windowed, 1, 1, f);
        std::fread(&g_triple_buffer, 1, 1, f);
        std::fread(&g_csens_high, 1, 1, f);
        std::fclose(f);
    }
    options_text_sound_mode();
    options_text_music_volume();
    options_text_sound_volume();
    options_apply_shadows();
    options_apply_game_speed();
    controls_build_menu_text(true);
    options_text_gfx_mode();
    options_text_sensitivity();
}

// 0x0040a490 config_save
void config_save() {
    std::FILE *f = std::fopen(s_Config_dat_004331c0, "wb");  // "wb" = DAT_004331d0
    if (!f) return;
    write_i32(g_sound_mode, f);
    write_i32(g_music_volume, f);
    write_i32(g_sfx_volume, f);
    write_i32(g_shadows_option, f);
    write_i32(g_game_speed_option, f);
    for (int i = 0; i < 10; i++) {
        write_i32(g_control_code[i], f);
        write_i32(g_control_type[i], f);
        write_i32(g_control_joy[i], f);
    }
    std::fwrite(&g_windowed, 1, 1, f);
    std::fwrite(&g_triple_buffer, 1, 1, f);
    std::fwrite(&g_csens_high, 1, 1, f);
    std::fclose(f);
}

// 0x0040e8d0 scan_save_slots: rotulos dos 6 slots do menu 6 a partir do cabecalho de Save1..6.dat
// (packfile com senha, 16 bytes de nome + int de % explorado). Slot sem arquivo fica como esta.
void scan_save_slots() {
    char label[256] = "test";  // original: buffer iniciado com DAT_004333a4 ("test") e zeros
    int32_t explored = 0;      // original: variavel de pilha nao iniciada
    char filename[256];
    for (int slot = 1; slot <= 6; slot++) {
        std::sprintf(filename, s_Save_i_dat_00433070, slot);
        packfile_password(g_password);
        PACKFILE *f = pack_fopen(filename, "rbp");  // 0x43304c
        packfile_password(nullptr);
        if (f) {
            pack_fread(label, 0x10, f);
            pack_read_i32(explored, f);
            std::sprintf(g_menu_items[6][slot - 1], s__15s__2i___00433398, label, explored);
            pack_fclose(f);
        }
    }
}

}  // namespace hoh

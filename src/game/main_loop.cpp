#include "game/main_loop.h"

#include "compat/platform.h"
#include "game/data_bridge.h"
#include "game/gfx_util.h"
#include "game/globals.h"
#include "game/intro.h"
#include "game/menu.h"
#include "game/music.h"
#include "game/pending.h"
#include "game/sound.h"
#include "game/water.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace hoh {

// 0x00402150 timer_tick: no original tambem alimentava o stream Ogg (FUN_00427ff0); no port o
// mixer puxa a musica sozinho.
void timer_tick() { g_tick_counter = g_tick_counter + 1; }

// 0x00402170 int_div_float
float int_div_float(int a, float b) { return static_cast<float>(a) / b; }

// 0x0040a570 triple_buffer_init
void triple_buffer_init() {
    if ((gfx_capabilities & GFX_CAN_TRIPLE_BUFFER) == 0) g_triple_available = enable_triple_buffer() == 0;
    else g_triple_available = 1;
    g_render_target = screen;
    if (!g_triple_available) {
        g_video_page2 = g_video_page1 = g_video_page0 = nullptr;
        g_triple_buffer = 0;
        options_text_triple_buffer();
        return;
    }
    g_video_page_index = 0;
    int w = gfx_driver ? gfx_driver->w : 0, h = gfx_driver ? gfx_driver->h : 0;
    g_video_page0 = create_video_bitmap(w, h);
    g_video_page1 = create_video_bitmap(w, h);
    g_video_page2 = create_video_bitmap(w, h);
    if (g_video_page0 && g_video_page1 && g_video_page2) {
        g_render_target = g_video_page0;
        if (!g_triple_available) g_triple_buffer = 0;
        options_text_triple_buffer();
        return;
    }
    g_triple_available = 0;
    g_triple_buffer = 0;
    options_text_triple_buffer();
}

// 0x0040e6b0 free_game_resources. Os blocos/moveis/nomeados/mundos do original eram objetos com
// destrutor virtual; no port pertencem ao GameData (g_data), destruido aqui.
void free_game_resources() {
    for (int i = 0; i < g_bitmap_registry_count; i++) destroy_bitmap(g_bitmap_registry[i]);
    g_bitmap_registry_count = 0;
    delete g_data;
    g_data = nullptr;
    for (BITMAP *&b : g_images) b = nullptr;
    if (g_shadow_buffer) destroy_bitmap(g_shadow_buffer);
    if (g_backbuffer) destroy_bitmap(g_backbuffer);
    destroy_bitmap(g_buffer2);
    if (g_video_page0) destroy_bitmap(g_video_page0);
    if (g_video_page1) destroy_bitmap(g_video_page1);
    if (g_video_page2) destroy_bitmap(g_video_page2);
    g_shadow_buffer = g_backbuffer = g_buffer2 = g_video_page0 = g_video_page1 = g_video_page2 = nullptr;
}

// 0x0040e150 draw_fps_counter: fps = quadros / ticks * ticks_por_segundo, arredondando para cima
// so se a fracao for >= 0.6 (constante 0x4302d8).
void draw_fps_counter(BITMAP *dst) {
    g_fps_frames = g_fps_frames + 1;
    int ticks = g_logic_counter - g_fps_last_tick;
    if (ticks >= g_ticks_per_second - 1) {
        g_fps_last_tick = g_logic_counter;
        double fps = static_cast<double>(g_fps_frames) / ticks * g_ticks_per_second;
        g_fps_value = static_cast<int>(fps);  // _ftol trunca
        if (fps - g_fps_value >= 0.6) g_fps_value = g_fps_value + 1;
        g_fps_frames = 0;
    }
    textprintf(dst, font, 0x248, 0x1d8, makecol(0xff, 0xff, 0xff), s_FPS___2i_00433380, g_fps_value);
}

// 0x0040c170 sound_update_ambient: chuva/trovao e sons de ambiente por mundo.
// O ramo do estado de jogo (1) le campos da sala ativa e sera completado com o port da sala;
// fora do jogo o comportamento (inclusive as chamadas a rand) e o do original.
int sound_update_ambient() {
    int r;
    bool in_game = g_game_state == 1 && room_has_rain_active();
    if (in_game) {
        g_rain_target = room_rain_level_active() + 1;
    } else {
        g_rain_target = 0;
    }
    if (g_rain_target == 0 && g_rain_volume == 1) {
        stop_voice_slot(g_voice_rain);
        g_rain_volume = 0;
    } else if (g_rain_target > 0 || g_rain_volume > 0) {
        if (g_rain_volume < g_rain_target) g_rain_volume = g_rain_volume + 1;
        else if (g_rain_target < g_rain_volume) g_rain_volume = g_rain_volume - 1;
        play_sfx_on_voice_vol(0x20, g_voice_rain, g_rain_volume / 7);
        voice_set_volume(g_voice_rain->voice, scale_sfx_volume(g_rain_volume / 7) * 0x11);
    }

    int world = -1;
    if (g_game_state == 1) world = world_index3_from_named(room_named_index_active());
    if (g_game_state != 1 || world != g_ambient_world) {
        g_ambient_world = world;
        stop_voice_slot(g_voice_ambient);
    }

    g_ambient_timer = g_ambient_timer + 1;
    r = g_ambient_timer;
    if (g_ambient_next <= g_ambient_timer) {
        g_ambient_timer = 0;
        g_ambient_next = rand() % 300 + 300;
        r = g_game_state;
        if (g_game_state == 1) {
            int w3 = world_index3_from_named(room_named_index_active()) / 3;
            if (room_has_rain_active()) {
                play_sfx_on_voice_vol(0x28, g_voice_ambient, (room_rain_level_active() + 1) / 7);
                g_thunder_flash = 1;
                r = rand();
                g_ambient_next = r % 300 + 300;
                return r / 300;
            }
            if (g_world_ambient_count[w3] > 0) {
                int pick = rand() % g_world_ambient_count[w3];
                play_sfx_on_voice_vol(g_world_ambient_ids[w3][pick], g_voice_ambient, 1);
                int r2 = rand();
                r = r2 / 800;
                g_ambient_next = r2 % 800 + 400;
            }
        }
    }
    return r;
}

// 0x0040c3c0 game_update: despachante do tick de logica (o Ghidra tinha fundido nele os
// handlers dos estados, que sao funcoes proprias na tabela de saltos 0x40c44c).
void game_update() {
    if (g_port_notice_ticks > 0) g_port_notice_ticks = g_port_notice_ticks - 1;  // port
    sound_update_ambient();
    if (g_joystick_enabled) poll_joystick();
    input_read_controls();
    g_frame_counter = g_frame_counter + 1;
    if (g_hud_wipe != -1) g_hud_wipe = g_hud_wipe + 1;
    if (g_transition_active) {
        g_transition_frame = g_transition_frame + 1;
        return;
    }
    switch (g_game_state) {
        case 0: menu_update(); return;
        case 1: update_state_game(); return;
        case 2: update_state_empire_map(); return;
        case 3: menu_update(); return;
        case 4: update_state_result(); return;
        case 5: update_state_intro(); return;
        default: return;
    }
}

// 0x0040e210 game_render
void game_render() {
    if (!g_triple_buffer) g_render_target = screen;
    BITMAP *out = nullptr;  // bitmap copiado para o destino no fim (iVar3)
    switch (g_game_state) {
        case 0:
            draw_water_background(g_backbuffer);
            draw_menu_screen(g_backbuffer);
            if (g_menu_fadein > 0) transition_column_wipe(g_backbuffer, g_menu_fadein, true, g_buffer2);
            blit(g_backbuffer, g_render_target, 0, 0, 0, 0, g_screen_w, g_screen_h);
            break;
        case 1:
            if (!g_transition_active) {
                render_game_frame(g_backbuffer);
                if (g_show_fps) {
                    int id = active_room_id();
                    textprintf(g_backbuffer, font, 0, 0, makecol(0xff, 0xff, 0xff), s___02i__02i__0043338c, id % 0x28, id / 0x28);
                }
                blit(g_backbuffer, g_render_target, 0, 0, 0, 0, g_screen_w, g_screen_h);
                break;
            }
            if (g_transition_type != 0) {
                if (g_transition_type == 1) {
                    if (g_transition_frame > 3) g_transition_active = 0;
                    clear_to_color(g_buffer2, 0);
                    hud_draw(g_buffer2);
                    blit(g_buffer2, g_render_target, 0, 0, 0, 0, g_screen_w, g_screen_h);
                }
                break;
            }
            if (g_transition_frame > 0x1f) {
                if (g_transition_frame > 0x25) g_transition_active = 0;
                clear_to_color(g_buffer2, 0);
                hud_draw(g_buffer2);
                blit(g_buffer2, g_render_target, 0, 0, 0, 0, g_screen_w, g_screen_h);
                break;
            }
            draw_sprite_lit(g_buffer2, g_backbuffer, 0, 0, g_transition_frame * -8 + 0x100);
            hud_draw(g_buffer2);
            out = g_buffer2;
            break;
        case 2:
        case 4:
            draw_water_background(g_backbuffer);
            if (g_game_state == 2) draw_empire_screen(g_backbuffer);
            else draw_end_screen(g_backbuffer);
            if ((g_game_state == 4 || !g_planet_from_menu) && g_frame_counter < 0x34)
                transition_column_wipe(g_backbuffer, 0x34 - g_frame_counter, true, g_buffer2);
            blit(g_backbuffer, g_render_target, 0, 0, 0, 0, g_screen_w, g_screen_h);
            break;
        case 3:
            draw_pause_screen(g_backbuffer);
            out = g_backbuffer;
            break;
        case 5:
            draw_intro_screen(g_backbuffer);
            out = g_backbuffer;
            break;
        default:
            break;
    }
    if (out) blit(out, g_render_target, 0, 0, 0, 0, g_screen_w, g_screen_h);

    if (g_thunder_flash > 0) {
        g_thunder_flash = g_thunder_flash + 1;
        if (g_thunder_flash == 2 || g_thunder_flash == 4) {
            clear_to_color(g_render_target, makecol(0xff, 0xff, 0xff));
            if (g_thunder_flash == 4) g_thunder_flash = 0;
        }
    }
    if (g_show_fps) draw_fps_counter(g_render_target);
    if (g_port_notice_ticks > 0 && g_port_notice) {  // port: aviso de funcionalidade pendente
        int w = text_length(font, g_port_notice);
        int x = (g_screen_w - w) / 2;
        rectfill(g_render_target, x - 6, 440, x + w + 6, 456, makecol(0, 0, 0));
        textout(g_render_target, font, g_port_notice, x, 444, makecol(0xff, 0xff, 0x80));
    }
    if (g_triple_buffer) {
        while (poll_scroll() != 0) {
        }
        request_video_bitmap(g_render_target);
        if (g_video_page_index == 0) {
            g_video_page_index = 1;
            g_render_target = g_video_page1;
        } else if (g_video_page_index == 1) {
            g_video_page_index = 2;
            g_render_target = g_video_page2;
        } else if (g_video_page_index == 2) {
            g_video_page_index = 0;
            g_render_target = g_video_page0;
        }
    } else {
        platform_present();  // port: no Allegro o blit no `screen` ja aparecia (memoria de video)
    }
}

// 0x0040f050 game_main. O port trata --data/--fullscreen antes (src/main.cpp); o resto da linha
// de comando segue a do original: -r0..-r4 (modo grafico), -b (cores), ou nome do .dat.
int game_main(int argc, char **argv) {
    controls_build_menu_text(true);  // FUN_0040a260(1)
    g_windowed = 1;
    config_load();
    int card = (g_windowed ? 0x160b : 0) + GFX_DIRECTX;  // DXWN em janela, DXAC em tela cheia
    std::strcpy(g_data_filename, s_HoHOriginal_dat_0043346c);
    g_convert_colors = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-') {
            if (a[1] == 'r') {
                switch (a[2]) {
                    case '0': card = GFX_DIRECTX_WIN; g_windowed = 1; break;
                    case '1': card = GFX_AUTODETECT_FULLSCREEN; g_windowed = 0; break;
                    case '2': card = GFX_AUTODETECT_WINDOWED; g_windowed = 1; break;
                    case '3': card = GFX_DIRECTX; g_windowed = 0; break;
                    case '4':
                        card = GFX_GDI;
                        g_windowed = 1;
                        g_convert_colors = 1;
                        std::sprintf(g_str_gfx_mode, "%s", s_GFX_MODE__SPECIAL_GDI_00433454);
                        break;
                    default: break;
                }
            }
            if (a[1] == 'b') g_convert_colors = !g_convert_colors;
        } else {
            std::strncpy(g_data_filename, a, sizeof g_data_filename - 1);
        }
    }
    // win_set_wnd_create_proc: so Windows/Allegro, sem efeito no port.
    srand(static_cast<unsigned>(std::time(nullptr)));
    g_title_variant = rand() % 4;

    install_allegro(0, nullptr, nullptr);
    set_color_depth(0x10);
    if (install_keyboard() != 0) {
        allegro_exit();
        allegro_message("%s", s_Error_installing_keyboard_handle_00433428);
        return -1;
    }
    if (set_gfx_mode(card, g_screen_w, g_screen_h, 0, 0) < 0) {
        allegro_exit();
        allegro_message("%s", s_Error_setting_graphics_mode__Exi_004331f0);
        return -1;
    }
    // center_window (0x402180): o SDL ja centraliza a janela.
    triple_buffer_init();
    if (install_joystick(JOY_TYPE_AUTODETECT) != 0) g_joystick_enabled = 0;
    if (num_joysticks == 0) g_joystick_enabled = 0;
    g_backbuffer = create_bitmap(g_screen_w, g_screen_h);
    g_buffer2 = create_bitmap(g_screen_w, g_screen_h);
    clear_to_color(g_buffer2, 0);
    clear_to_color(screen, makecol(0, 0, 0));
    g_shadow_buffer = create_bitmap(g_render_target->w, g_render_target->h);
    if (!load_game_data()) {
        allegro_message("Nao foi possivel carregar os dados do jogo original:\n%s\n\n"
                        "Copie HoHOriginal.dat e a pasta Sound para a pasta do jogo\n"
                        "ou use --data <pasta>.", allegro_error);
        allegro_exit();
        return -1;
    }
    init_ui_graphics();
    water_init();
    intro_start();
    reserve_voices(0x10, -1);
    if (install_sound(DIGI_AUTODETECT, MIDI_NONE, nullptr) != 0)
        std::fprintf(stderr, s_Failed_to_initialise_sound_drive_00433400, allegro_error);
    set_volume_per_voice(0);
    g_ogg_theme = ogg_load_file(s_Sound_Theme_ogg_004333f0, 1);
    g_ogg_game = ogg_load_file(s_Sound_Game_ogg_004333e0, 1);
    DAT_0043a290 = 4;  // numero de buffers do alogg; sem uso no port
    g_music_stream = ogg_play(g_ogg_theme, 2, 0, int_div_float(g_music_volume, 15.0f));
    install_int_ex(timer_tick, 0x1234dd / g_ticks_per_second);

    int redraw = g_redraw;  // o original le g_redraw so uma vez, antes do laco
    for (;;) {
        platform_pump();  // port: o Allegro tratava as mensagens do SO numa thread propria
        if (platform_quit_requested()) g_quit = 1;
        if (g_quit) {
            remove_int(timer_tick);
            ogg_stop(g_music_stream);
            ogg_destroy(g_ogg_theme);
            ogg_destroy(g_ogg_game);
            free_game_resources();
            config_save();
            allegro_exit();
            return 0;
        }
        if (g_logic_counter < g_tick_counter) {
            do {
                game_update();
                g_redraw = -1;
                g_logic_counter = g_logic_counter + 1;
            } while (g_logic_counter < g_tick_counter);
            game_render();
            redraw = 0;
            g_redraw = 0;
        } else if (redraw != 0) {
            game_render();
            redraw = 0;
            g_redraw = 0;
        } else {
            SDL_Delay(1);  // port: o original girava a CPU a 100% esperando o timer
        }
    }
}

}  // namespace hoh

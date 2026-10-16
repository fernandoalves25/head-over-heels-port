// GERADO por HeadOverHeels/re/port/gen_globals.py a partir do HoH.exe. Nao editar a mao.
// Globais do jogo original com os valores iniciais do .data (derivado do executavel original).
#pragma once

#include "compat/allegro.h"
#include "game/crt.h"  // rand()/srand() do MSVC
#include <cstdint>

namespace hoh {

// Classes do motor (game/engine_types.h)
struct Behaviour;
struct Block;
struct Controller;
struct Door;
struct Flare;
struct FloorSet;
struct Glow;
struct Named;
struct Object;
struct Room;
struct RoomBase;
struct Sprite;
struct World;

extern void *g_vtbl_Door[1];
extern void *g_vtbl_Flare[1];
extern void *g_vtbl_Block[1];
extern void *g_vtbl_Sprite[1];
extern void *g_vtbl_Object[1];
extern void *g_vtbl_Controller[1];
extern void *g_vtbl_Behaviour[1];
extern void *g_vtbl_RoomBase[1];
extern void *g_vtbl_Room[1];
extern void *g_vtbl_Named[1];
extern int32_t g_ticks_per_second;
extern int32_t g_bigjumps_per_bunny;
extern int32_t g_speed_bunny_ticks;
extern int32_t g_light_radius_full;
extern int32_t g_screen_w;
extern int32_t g_screen_h;
extern char *g_attr_name_table[35];
extern int32_t g_control_code[10];
extern int32_t g_jump_length[2];
extern int32_t g_jump_profile[2][50];
extern char g_menu_items[8][13][24];
extern char *const s_UP_UP_004314f4;
extern char *const s_SOUND_MODE__NASTY_0043162c;
extern char *const s_MUSIC_VOLUME__15_00431644;
extern char *const s_SOUND_VOLUME__15_0043165c;
extern char *const s_SHADOWS__COMPLEX_00431764;
extern char *const s_GAME_SPEED__ORIGINAL_0043177c;
extern char *const g_str_gfx_mode;
extern char *const s_TRIPLE_BUFFERING__NO_004317ac;
extern char *const s_C_SENSITIVITY__HIGH_004317c4;
extern char *const g_save_slot_text;
extern uint8_t g_menu_item_adjustable[8][13];
extern uint8_t &DAT_00431c92;
extern uint8_t &DAT_00431c96;
extern int32_t g_menu_x[8];
extern int32_t g_menu_y[8];
extern int32_t &DAT_00431ce4;
extern int32_t g_menu_count[8];
extern int32_t &DAT_00431cf4;
extern int32_t &DAT_00431d08;
extern int32_t DAT_00431d0c;
extern char *g_sound_mode_names[3];
extern char *g_triple_buffer_names[3];
extern char *g_shadow_mode_names[3];
extern char *g_game_speed_names[3];
extern char *g_control_labels[10];
extern char *g_csens_names[2];
extern char *g_ticker_texts[24];
extern int32_t g_planet_x[5];
extern int32_t g_planet_y[5];
extern char *g_planet_names[5];
extern int32_t g_sample_category[48][2];
extern int32_t &DAT_00431e10;
extern char *g_world_names[7];
extern int32_t g_world_ambient_count[7];
extern int32_t g_world_ambient_ids[7][10];
extern int32_t g_hud_layout[1];
extern int32_t DAT_004320e8;
extern int32_t DAT_004320ec;
extern int32_t DAT_004320f0;
extern int32_t g_hud_lives_pos[2][2];
extern int32_t &DAT_004320f8;
extern int32_t &DAT_004320fc;
extern int32_t &DAT_00432100;
extern int32_t DAT_00432104;
extern int32_t DAT_00432108;
extern int32_t DAT_0043210c;
extern int32_t DAT_00432110;
extern int32_t DAT_00432114;
extern int32_t DAT_00432118;
extern int32_t DAT_0043211c;
extern int32_t DAT_00432120;
extern int32_t DAT_00432124;
extern int32_t DAT_00432128;
extern int32_t DAT_0043213c[2];
extern int32_t DAT_00432144[2];
extern int32_t DAT_00432154;
extern int32_t DAT_00432158;
extern int32_t DAT_00432164;
extern int32_t DAT_00432168;
extern char *g_rating_names[10];
extern char *g_key_names[115];
extern int32_t g_redraw;
extern uint8_t g_joystick_enabled;
extern uint8_t g_triple_buffer;
extern uint8_t g_shadows_on;
extern uint8_t g_shadows_complex;
extern int32_t g_shadows_option;
extern uint8_t g_csens_high;
extern int32_t g_move_dir;
extern int32_t g_prev_dir;
extern int32_t DAT_00432390;
extern int32_t g_jump_power;
extern uint8_t g_teleport_out;
extern int32_t g_hud_wipe;
extern int32_t g_lives[2];
extern int32_t &g_lives_heels;
extern int32_t g_idle_choice;
extern int32_t g_luma_weight_r;
extern int32_t g_luma_weight_g;
extern int32_t g_luma_weight_b;
extern int32_t g_menu_choice;
extern uint8_t g_menu_opening;
extern int32_t g_menu_fadein;
extern int32_t g_quit_countdown;
extern uint8_t g_cursor_visible;
extern int32_t g_water_shape_idx;
extern int32_t g_sound_mode;
extern int32_t g_music_volume;
extern int32_t g_sfx_volume;
extern int32_t g_game_tps;
extern int32_t g_game_speed_option;
extern int32_t g_held_dir;
extern int32_t g_hitwall_last_frame;
extern int32_t g_ambient_world;
extern int32_t g_intro_logo_tiles[60][2];
extern int32_t &DAT_00432404;
extern int32_t DAT_0043304c;
extern char s_Sound_Sound_dat_00433050[16];
extern char g_password[16];
extern char &g_fmt_int;
extern char s_Save_i_dat_00433070[12];
extern char s_Failure_to_CloseClipboard____c_0043307c[32];
extern char s_Failure_to_SetClipboardData____c_0043309c[36];
extern char s_Failure_to_OpenClipboard____c_004330c0[32];
extern char s_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef_004330e0[68];
extern int32_t DAT_00433124;
extern char s_HighScoreCode_txt_00433128[20];
extern int32_t DAT_0043313c;
extern int32_t DAT_00433140;
extern int32_t DAT_00433144;
extern char s_MUSIC_VOLUME___2i_00433148[20];
extern char s_SOUND_VOLUME___2i_0043315c[20];
extern int32_t DAT_00433170;
extern char s__sJP_i__02i_00433178[12];
extern char s__sJS_i__02i_00433184[12];
extern char s_GFX_MODE__FULL_SCREEN_00433190[24];
extern char s_GFX_MODE__WINDOWED_004331a8[24];
extern char s_Config_dat_004331c0[12];
extern int32_t DAT_004331cc;
extern int32_t DAT_004331d0;
extern char s__2i___004331d4[8];
extern char s___EMPTY_____004331dc[20];
extern char s_Error_setting_graphics_mode__Exi_004331f0[40];
extern int32_t DAT_00433218;
extern char s__2i___0043321c[8];
extern char s_NO_SAVES_LEFT_00433224[16];
extern char s_1_SAVE_LEFT_00433234[16];
extern char s__2i_SAVES_LEFT_00433244[16];
extern char s_Error__Press__S__to_resend__00433254[32];
extern char s_High_Score_Position___i_00433274[24];
extern char s_Congratulations__0043328c[20];
extern char s_Enter_Your_Name___s_004332a0[20];
extern char s_Press__S__To_Send_The_Code_To_Th_004332b4[48];
extern char s_Retrospec_High_Score_Code___s_004332e4[32];
extern char s_RATING_00433304[8];
extern char s_LIBERATED__i_PLANETS__0043330c[24];
extern char s__i___ROOMS_EXPLORED__00433324[24];
extern char s_GAME_OVER_0043333c[12];
extern char s_PEOPLE_SALUTE_YOU__00433348[20];
extern char s_THE_BLACKTOOTH_EMPIRE_0043335c[24];
extern char s_PRESENTS____00433374[12];
extern char s_FPS___2i_00433380[12];
extern char s___02i__02i__0043338c[12];
extern char s__15s__2i___00433398[12];
extern int32_t DAT_004333a4;
extern int32_t DAT_004333a8;
extern char s_COPYRIGHT_2003_RETROSPEC_004333ac[28];
extern char s_OVER_004333c8[8];
extern char s_HEELS_004333d0[8];
extern char s_HEAD_004333d8[8];
extern char s_Sound_Game_ogg_004333e0[16];
extern char s_Sound_Theme_ogg_004333f0[16];
extern char s_Failed_to_initialise_sound_drive_00433400[40];
extern char s_Error_installing_keyboard_handle_00433428[44];
extern char s_GFX_MODE__SPECIAL_GDI_00433454[24];
extern char s_HoHOriginal_dat_0043346c[16];
extern char s_retrospec_sgn_net_0043347c[20];
extern char s_InetURL_1_0_00433490[12];
extern int32_t DAT_0043349c;
extern char s__highscore_php_yourname__s_code__004334a0[512];
extern int32_t DAT_0043a290;
extern BITMAP *g_img_ending_logo;
extern BITMAP *g_icon_bag;
extern BITMAP *g_icon_bag_grey;
extern int32_t g_carry_origin_x;
extern int32_t g_carried_orig_y;
extern BITMAP *g_icon_head;
extern BITMAP *g_icon_head_grey;
extern int32_t g_carried_orig_z;
extern int32_t g_view_ext_z;
extern int32_t g_view_ext_x;
extern int32_t g_world_count;
extern int32_t g_attr_count;
extern int32_t g_room_taken_count[1600];
extern int32_t g_mobile_count;
extern int32_t g_prev_room_ext_x;
extern int32_t g_cur_char;
extern int32_t g_prev_room_ext_z;
extern BITMAP *g_img_title;
extern BITMAP *g_font_w10;
extern BITMAP *g_icon_doughnuts;
extern BITMAP *g_icon_doughnut_grey;
extern int32_t g_input[10];
extern int32_t &g_input_down;
extern int32_t &g_input_left;
extern int32_t &g_input_right;
extern int32_t &g_input_jump;
extern int32_t &g_input_carry;
extern int32_t &g_input_jumpcarry;
extern int32_t &g_input_fire;
extern int32_t &g_input_swap;
extern int32_t &g_input_pause;
extern char g_hiscore_code[20];
extern char &DAT_0043bd90;
extern char &DAT_0043bd94;
extern int32_t g_life_transfer_amount;
extern int32_t *g_room_table;
extern int32_t g_teleport_saved_anim;
extern uint8_t g_triple_available;
extern int32_t g_room_taken_ids[1600][10];
extern int32_t g_room_count;
extern BITMAP *g_font_title;
extern BITMAP *DAT_0044b7bc;
extern int32_t g_entry_anim[2];
extern int32_t &g_entry_anim_heels;
extern int32_t g_carry_cooldown_ticks;
extern Controller *g_teleport_ctrl;
extern uint8_t *g_room_blob;
extern BITMAP *g_images[40];
extern BITMAP * &DAT_00527398;
extern BITMAP * &DAT_0052739c;
extern BITMAP * &DAT_005273a0;
extern BITMAP * &DAT_005273a4;
extern BITMAP * &DAT_005273a8;
extern BITMAP * &DAT_005273ac;
extern BITMAP * &g_images_hud_panels;
extern BITMAP * &DAT_005273e4;
extern BITMAP * &DAT_005273e8;
extern int32_t DAT_0052745c;
extern Object *g_char_obj[2];
extern Object * &g_player_heels;
extern uint8_t DAT_00529080;
extern uint8_t DAT_00529081;
extern uint8_t DAT_00529082;
extern char g_attr_names[8925];
extern DATAFILE *g_sound_datafile;
extern int32_t g_move_result;
extern int32_t g_attr_map[255];
extern Object *g_support_obj1;
extern Object *g_support_obj2;
extern int32_t g_teleport_timer;
extern int32_t g_image_count;
extern int32_t g_menu_item_indent[13];
extern int32_t &DAT_005392a0;
extern BITMAP *g_bar_image;
extern int32_t g_char_room_id[2];
extern int32_t &g_player_room_heels;
extern uint8_t g_saved_anim_playing;
extern uint8_t g_planet_from_menu;
extern int32_t g_mask_color;
extern int32_t g_idle_timer;
extern int32_t g_menu_panel_fade;
extern BITMAP *DAT_005392ec;
extern BITMAP *g_font_small;
extern char g_data_filename[256];
extern BITMAP *g_bitmap_registry[256];
extern int32_t DAT_0091dfb4;
extern char g_char_under_cursor;
extern int32_t g_redefine_phase;
extern BITMAP *g_font_w18_tinted[8];
extern BITMAP * &DAT_0091e014;
extern FloorSet *g_mobiles[255];
extern uint8_t g_windowed;
extern Object *g_carried_obj;
extern int32_t g_light_fade_ticks;
extern char g_slot_name_backup[24];
extern int32_t g_move_dx;
extern int32_t g_move_dy;
extern int32_t g_start_room;
extern int32_t g_block_count;
extern int32_t g_move_dz;
extern int16_t *g_water_cur;
extern int16_t *g_water_prev;
extern uint8_t g_planets_freed[5];
extern uint8_t &DAT_0091e460;
extern BITMAP *g_font_w18;
extern int32_t g_shield_bunny_ticks;
extern int32_t g_saved_frame_counter;
extern int32_t DAT_0091e470;
extern int32_t g_jump_frame;
extern int16_t g_water_buf_a[480][640];
extern int32_t g_saved_frame;
extern int16_t g_water_buf_b[480][640];
extern BITMAP *DAT_00a4a47c;
extern BITMAP *g_font_medium;
extern int32_t g_text_cursor;
extern BITMAP *DAT_00a4a488;
extern BITMAP *g_hud_old;
extern BITMAP *g_hud_new;
extern uint8_t g_room_visited[1600];
extern BITMAP *g_bmp_logo_shadow;
extern BITMAP *g_bmp_panel_shadow;
extern BITMAP *g_bmp_text_line_shadow;
extern BITMAP *g_bmp_copyright_shadow;
extern BITMAP *g_bmp_ticker_shadow[24];
extern BITMAP *g_icon_crown_shadow;
extern int16_t *g_water_rendered;
extern char g_named_names[65025];
extern BITMAP *g_buffer2;
extern BITMAP *g_shadow_buffer;
extern int32_t g_life_transfer_from;
extern int32_t g_title_variant;
extern BITMAP *g_backbuffer;
extern BITMAP *g_render_target;
extern int32_t g_saved_anim;
extern int32_t DAT_00a5a9a0;
extern int32_t g_named_count;
extern BITMAP *g_icon_hooter;
extern BITMAP *g_icon_hooter_grey;
extern int32_t g_entry_x[2];
extern int32_t &g_entry_x_heels;
extern int32_t g_entry_y[2];
extern int32_t &g_entry_y_heels;
extern int32_t g_entry_z[2];
extern int32_t &g_entry_z_heels;
extern Behaviour *g_block_behaviours[255];
extern BITMAP *g_icon_heels;
extern BITMAP *g_icon_heels_grey;
extern Named *g_named[255];
extern Block *g_blocks[255];
extern Block * &g_blocks_1_;
extern Block * &g_blocks_2_;
extern Block * &DAT_00a5b1d4;
extern Block * &DAT_00a5b1d8;
extern Block * &DAT_00a5b1dc;
extern Block * &g_blocks_6_;
extern Block * &g_blocks_7_;
extern Block * &g_blocks_8_;
extern Block * &DAT_00a5b1ec;
extern Block * &DAT_00a5b1f0;
extern Block * &g_blocks_11_;
extern World *g_worlds[48];
extern Room *g_char_rooms[2];
extern Room * &g_room_heels;
extern int32_t g_menu_fade;
extern int32_t g_hiscore_position;
extern BITMAP *g_font_tinted;
extern int32_t g_water_shape_y;
extern int32_t g_water_shape_x;
extern int32_t g_idle_state;
extern int32_t g_idle_delay;
extern Block *g_blocks_alt[255];
extern int32_t g_load_slot;
extern BITMAP *g_bmp_logo;
extern BITMAP *g_bmp_panel;
extern BITMAP *g_bmp_text_line;
extern BITMAP *g_bmp_copyright;
extern BITMAP *g_water_shapes[5];
extern BITMAP *g_bmp_ticker[24];
extern BITMAP *g_icon_crown;
extern BITMAP *g_icon_crown_gray;
extern BITMAP *g_popup_bitmap;
extern int32_t g_menu_key_up;
extern int32_t g_menu_key_down;
extern int32_t g_control_type[10];
extern int32_t g_control_joy[10];
extern int32_t g_logic_counter;
extern int32_t g_tick_counter;
extern uint8_t g_convert_colors;
extern int32_t g_fps_frames;
extern uint8_t g_show_fps;
extern uint8_t g_fps_toggle_latch;
extern BITMAP *g_video_page0;
extern BITMAP *g_video_page1;
extern BITMAP *g_video_page2;
extern int32_t g_video_page_index;
extern int32_t g_bitmap_registry_count;
extern int32_t g_active_room_slot;
extern int32_t g_joined;
extern int32_t g_swap_mode;
extern int32_t g_move_delay;
extern int32_t g_move_bonus_dx;
extern int32_t g_move_bonus_dy;
extern int32_t g_move_bonus_dz;
extern uint8_t g_jumping;
extern uint8_t g_jump_after_pickup;
extern uint8_t g_hit_room_edge;
extern uint8_t g_teleporting;
extern int32_t g_teleport_dest_room;
extern uint8_t g_has_bag;
extern uint8_t g_carry_latch;
extern uint8_t g_carrying;
extern int32_t g_carry_cooldown;
extern uint8_t g_carried_light_on;
extern int32_t g_head_speed_timer;
extern int32_t g_heels_big_jumps;
extern int32_t g_shield_timer[2];
extern int32_t &g_shield_timer_heels;
extern int32_t g_light_timer[2];
extern int32_t &g_light_timer_heels;
extern uint8_t g_has_hooter;
extern int32_t g_donuts;
extern uint8_t g_fire_latch;
extern int32_t g_fish_count;
extern int32_t g_crowns;
extern uint8_t g_dying;
extern int32_t g_death_frame;
extern uint8_t g_joined_at_entry;
extern uint8_t g_cheat;
extern uint8_t g_transition_active;
extern int32_t g_transition_frame;
extern int32_t g_transition_type;
extern uint8_t g_falling;
extern uint8_t g_jumpcarry_held;
extern uint8_t DAT_00a5bc32;
extern uint8_t g_four_crowns;
extern int32_t g_game_state;
extern int32_t g_frame_counter;
extern int32_t g_rooms_visited_count;
extern int32_t g_menu_id;
extern int32_t g_menu_highlight;
extern int32_t g_menu_sel;
extern uint8_t g_key_up_latch;
extern uint8_t g_key_down_latch;
extern uint8_t g_enter_latch;
extern uint8_t g_select_latch;
extern int32_t g_ticker_index;
extern int32_t g_ticker_timer;
extern uint8_t g_menu_closing;
extern uint8_t g_menu_text_edit;
extern int32_t g_cursor_blink_timer;
extern int32_t g_redefining_control;
extern int32_t g_menu_timer;
extern int32_t g_water_shape_timer;
extern uint8_t g_quit;
extern int32_t g_percent_explored;
extern int32_t g_substate_timer;
extern int32_t g_substate;
extern uint8_t g_life_transfer_anim;
extern uint8_t g_in_final_room;
extern uint8_t g_moved_last_frame;
extern int32_t g_rain_volume;
extern int32_t g_rain_target;
extern int32_t g_ambient_timer;
extern int32_t g_ambient_next;
extern int32_t g_thunder_flash;
extern int32_t g_intro_step;
extern int32_t g_intro_presents_alpha;
extern int32_t g_hiscore_state;
extern char g_hiscore_name[30];
extern char &DAT_00a5bcad;
extern int32_t g_room_cursor;
extern int32_t g_water_last_tick;
extern int32_t g_fps_value;
extern int32_t g_fps_last_tick;
extern void *g_inet_connect;
extern void *g_inet_session;
extern char g_empty_name[4];

// DAT_00431294 @0x431294 cai dentro de g_menu_items (+16); acessar via g_menu_items
// MODULO: g_sound_index_map @0x431f88 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_walk_voice @0x4323e8 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_voice_jump @0x4323ec tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_voice_rain @0x4323f0 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_voice_ambient @0x4323f4 tipo 'MODULE' -> definir no modulo que o usa
// SKIP: DAT_00448400 @0x448400 nao e global (campo de classe)
// SKIP: DAT_00448440 @0x448440 nao e global (campo de classe)
// SKIP: DAT_00448444 @0x448444 nao e global (campo de classe)
// SKIP: DAT_00448858 @0x448858 nao e global (campo de classe)
// SKIP: DAT_0044885c @0x44885c nao e global (campo de classe)
// SKIP: DAT_0044889c @0x44889c nao e global (campo de classe)
// SKIP: DAT_004488dc @0x4488dc nao e global (campo de classe)
// SKIP: DAT_004488e0 @0x4488e0 nao e global (campo de classe)
// SKIP: DAT_0044891c @0x44891c nao e global (campo de classe)
// SKIP: DAT_0044895c @0x44895c nao e global (campo de classe)
// SKIP: DAT_00448960 @0x448960 nao e global (campo de classe)
// SKIP: DAT_00448d60 @0x448d60 nao e global (campo de classe)
// SKIP: DAT_00448d64 @0x448d64 nao e global (campo de classe)
// SKIP: DAT_00448d8c @0x448d8c nao e global (campo de classe)
// SKIP: DAT_00448da0 @0x448da0 nao e global (campo de classe)
// SKIP: DAT_00448da1 @0x448da1 nao e global (campo de classe)
// SKIP: DAT_00448da2 @0x448da2 nao e global (campo de classe)
// SKIP: DAT_00448da3 @0x448da3 nao e global (campo de classe)
// SKIP: DAT_00448da4 @0x448da4 nao e global (campo de classe)
// SKIP: DAT_00448da8 @0x448da8 nao e global (campo de classe)
// SKIP: DAT_00448db0 @0x448db0 nao e global (campo de classe)
// SKIP: DAT_00448db1 @0x448db1 nao e global (campo de classe)
// SKIP: DAT_00448db4 @0x448db4 nao e global (campo de classe)
// SKIP: DAT_00448dbc @0x448dbc nao e global (campo de classe)
// SKIP: DAT_00448dc0 @0x448dc0 nao e global (campo de classe)
// SKIP: DAT_00448dc4 @0x448dc4 nao e global (campo de classe)
// SKIP: DAT_00448dc8 @0x448dc8 nao e global (campo de classe)
// SKIP: DAT_00448dd0 @0x448dd0 nao e global (campo de classe)
// SKIP: DAT_00449ef0 @0x449ef0 nao e global (campo de classe)
// SKIP: DAT_00449ef4 @0x449ef4 nao e global (campo de classe)
// MODULO: g_ogg_theme @0x44b7b8 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_voice_slots @0x44b7d0 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_stars @0x527458 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_music_stream @0x91e01c tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_clipboard_ptr @0xa5a978 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_clipboard @0xa5a980 tipo 'MODULE' -> definir no modulo que o usa
// MODULO: g_ogg_game @0xa5b684 tipo 'MODULE' -> definir no modulo que o usa

}  // namespace hoh

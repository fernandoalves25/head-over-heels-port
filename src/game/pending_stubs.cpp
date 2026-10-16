// Implementacoes PROVISORIAS das funcoes de pending.h. Cada uma avisa uma vez no stderr.
// Remover cada funcao daqui quando a versao portada existir.
#include "game/globals.h"
#include "game/intro.h"
#include "game/menu.h"
#include "game/pending.h"

#include <cstdio>

namespace hoh {

#define PENDING(name)                                                                  \
    do {                                                                               \
        static bool warned = false;                                                    \
        if (!warned) {                                                                 \
            warned = true;                                                             \
            std::fprintf(stderr, "[hoh] ainda nao portado: %s\n", name);               \
        }                                                                              \
    } while (0)

// Sem o jogo portado, qualquer estado de jogo volta para o menu principal.
static void back_to_title() {
    g_game_state = 0;
}

void update_state_game() { PENDING("update_state_game 0x4074b0"); back_to_title(); }
void update_state_empire_map() { PENDING("update_state_empire_map 0x40bcc0"); back_to_title(); }
void update_state_result() { PENDING("update_state_result 0x40bb20"); back_to_title(); }

void render_game_frame(BITMAP *dst) { PENDING("render_game_frame 0x406570"); clear_to_color(dst, 0); }
void hud_draw(BITMAP *) { PENDING("hud_draw 0x406150"); }
void draw_empire_screen(BITMAP *) { PENDING("draw_empire_screen 0x40d8f0"); }
void draw_pause_screen(BITMAP *dst) { PENDING("draw_pause_screen 0x40dcd0"); clear_to_color(dst, 0); }
void draw_end_screen(BITMAP *) { PENDING("draw_end_screen 0x40d330"); }

int active_room_id() { return 0; }
bool room_has_rain_active() { return false; }
int room_rain_level_active() { return 0; }
int room_named_index_active() { return 0; }

// --- pendentes declaradas em menu.h ---
// Aviso na tela enquanto o jogo nao esta portado (codigo so do port; desenhado por game_render).
int g_port_notice_ticks = 0;
const char *g_port_notice = nullptr;

// NEW GAME / OLD GAME: no original vai para o mapa dos planetas (estado 2) e dai para o jogo.
// Sem isso portado, o menu ficava vazio e parado ("travado"); agora volta ao menu PLAY.
void planet_screen_start(bool) {
    PENDING("planet_screen_start 0x406f10 (NEW/OLD GAME)");
    g_port_notice = "O JOGO EM SI AINDA ESTA SENDO PORTADO - POR ENQUANTO SO O MENU";
    g_port_notice_ticks = 4 * 60;
    menu_open(1, 0);
}
void save_game() { PENDING("save_game 0x409e20"); }
void free_character_rooms() { PENDING("free_character_rooms 0x405d70"); }

// --- pendentes declaradas em intro.h: sem motor de sala, a cena da abertura fica vazia ---
static int g_stub_origin[2] = {0, 0};
Room *load_room(int id) { PENDING("load_room 0x403c50"); (void)id; return nullptr; }
void *Room_scalar_dtor(Room *, int) { return nullptr; }
void Room_update_controllers(Room *) {}
void Room_update_physics(Room *) {}
void Room_animate(Room *) {}
void Room_set_active_player(Room *, Object *) {}
void Room_draw_objects(Room *, BITMAP *, bool, BITMAP *, bool) {}
void Room_draw_glows(Room *, BITMAP *) {}
void Sprite_set_anim_frame(Object *, int, int) {}
void Sprite_draw(Object *, BITMAP *, int, int) {}
void reset_idle_animation() {}
int &room_origin_x(Room *) { return g_stub_origin[0]; }
int &room_origin_y(Room *) { return g_stub_origin[1]; }
int room_id_of(Room *) { return 0; }
Object *room_object(Room *, int) { return nullptr; }
int object_pos_x(Object *) { return 0; }
int object_pos_y(Object *) { return 0; }
int object_pos_z(Object *) { return 0; }
int object_anim(Object *) { return 0; }

}  // namespace hoh

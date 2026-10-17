// Port: modo responsivo (ver responsive.h).
#include "game/responsive.h"

#include "compat/hd_render.h"
#include "compat/platform.h"
#include "game/engine_types.h"
#include "game/globals.h"
#include "game/room_load.h"
#include "game/room_render.h"
#include "game/screens.h"

#include <algorithm>
#include <cmath>

namespace hoh {

CanvasGeom g_canvas_geom;

namespace {

BITMAP *g_canvas = nullptr;     // o que vai para a janela
BITMAP *g_wide_game = nullptr;  // ultimo quadro do jogo na tela larga
BITMAP *g_wide_shadow = nullptr;  // shadow_buf do Room_draw na tela larga (SHADOWS COMPLEX)
BITMAP *g_hud_layer = nullptr;  // HUD desenhado em 640x480 sobre magenta, depois posto no centro
bool g_wide_valid = false;      // g_wide_game tem um quadro do jogo
bool g_wide_fresh = false;      // ... desenhado neste quadro
bool g_flash = false;

void destroy(BITMAP *&b) {
    if (!b) return;
    platform_forget_bitmap(b);
    destroy_bitmap(b);
    b = nullptr;
}

// Recalcula a rolagem das salas do jogo (room_init_scroll soma a margem da tela larga as extensoes
// de vista; as flags so ligam, entao voltam a falso antes). So no jogo/pausa: fora dele os ponteiros
// de g_char_rooms podem ser de salas ja destruidas (free_character_rooms nao os zera).
void rescroll() {
    if (g_game_state != 1 && g_game_state != 3) return;
    for (int s = 0; s < 2; s++) {
        Room *r = g_char_rooms[s];
        if (!r) continue;
        r->scroll_x = false;
        r->scroll_z = false;
        if (s != g_active_room_slot) room_init_scroll(r);
    }
    if (Room *r = g_char_rooms[g_active_room_slot]) room_init_scroll(r);  // a ativa por ultimo (g_view_ext_*)
}

// Paredes do HUD alem do miolo: o HUD e um "V" (as paredes sobem do centro ate as bordas do 4:3) e,
// nos ultimos ~16 px de cada painel, a estrutura dobra a esquina: aparece a face lateral, que desce
// em direcao a quem olha. Fora do miolo essa face continua descendo (faixas de 16 px da ponta do
// painel, cada uma 8 px mais baixa, como a inclinacao isometrica de 1 para 2) ate a borda da tela.
void hud_side_fill(BITMAP *dst) {
    const int ox = g_canvas_geom.ox, oy = g_canvas_geom.oy;
    if (ox <= 0 || (g_game_state != 1 && g_game_state != 3)) return;
    Room *room = g_char_rooms[g_active_room_slot];
    if (!room) return;
    const int k = world_index3_from_named(room->wall_named);
    BITMAP *left = g_images[19 + k], *right = g_images[21 + k];
    if (!left || !right) return;
    constexpr int kSlice = 16, kDrop = kSlice / 2;
    const int base_y = oy + 0x140;
    for (int i = 1; ox - kSlice * (i - 1) > 0; i++) {
        const int y = base_y + kDrop * i;
        masked_blit(left, dst, 0, 0, ox - kSlice * i, y, kSlice, left->h);
        masked_blit(right, dst, right->w - kSlice, 0, ox + 640 + kSlice * (i - 1), y, kSlice, right->h);
    }
}

// Fundo das telas sem conteudo largo: o miolo reduzido a uma grade de 32x24 medias, escurecido e
// ampliado com interpolacao para cobrir a tela toda (so fora do miolo, que vem por cima).
void ambient_fill(BITMAP *dst, BITMAP *core, int oy) {
    constexpr int kGw = 32, kGh = 24, kCell = 20;
    float grid[kGh][kGw][3];
    for (int gy = 0; gy < kGh; gy++)
        for (int gx = 0; gx < kGw; gx++) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int y = gy * kCell; y < (gy + 1) * kCell && y < core->h; y += 2)
                for (int x = gx * kCell; x < (gx + 1) * kCell && x < core->w; x += 2) {
                    const int c = core->line[y][x];
                    if (c == MASK_COLOR_16) continue;
                    sr += getr(c); sg += getg(c); sb += getb(c); n++;
                }
            const float f = n ? 0.55f / n : 0.f;
            grid[gy][gx][0] = sr * f;
            grid[gy][gx][1] = sg * f;
            grid[gy][gx][2] = sb * f;
        }
    const int ox = g_canvas_geom.ox;
    const double s = std::min(640.0 / dst->w, 480.0 / dst->h);  // o miolo "cobre" a tela
    for (int y = 0; y < dst->h; y++) {
        const bool row_in_core = y >= oy && y < oy + 480;
        const double fy = std::clamp(((y - dst->h / 2.0) * s + 240.0) / kCell - 0.5, 0.0, kGh - 1.0);
        const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, kGh - 1);
        const float ty = static_cast<float>(fy - y0);
        for (int x = 0; x < dst->w; x++) {
            if (row_in_core && x >= ox && x < ox + 640) {
                x = ox + 639;
                continue;
            }
            const double fx = std::clamp(((x - dst->w / 2.0) * s + 320.0) / kCell - 0.5, 0.0, kGw - 1.0);
            const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, kGw - 1);
            const float tx = static_cast<float>(fx - x0);
            int rgb[3];
            for (int c = 0; c < 3; c++) {
                const float top = grid[y0][x0][c] + (grid[y0][x1][c] - grid[y0][x0][c]) * tx;
                const float bot = grid[y1][x0][c] + (grid[y1][x1][c] - grid[y1][x0][c]) * tx;
                rgb[c] = std::clamp(static_cast<int>(top + (bot - top) * ty + 0.5f), 0, 255);
            }
            dst->line[y][x] = static_cast<uint16_t>(makecol(rgb[0], rgb[1], rgb[2]));
        }
    }
    // HD: as faixas viram copias 1x com filtro (o fundo ja e desfocado)
    if (ox > 0) {
        hd::wrote(dst, 0, oy, ox, 480, hd::LINEAR);
        hd::wrote(dst, ox + 640, oy, dst->w - ox - 640, 480, hd::LINEAR);
    }
    if (oy > 0) {
        hd::wrote(dst, 0, 0, dst->w, oy, hd::LINEAR);
        hd::wrote(dst, 0, oy + 480, dst->w, dst->h - oy - 480, hd::LINEAR);
    }
}

}  // namespace

bool canvas_active() { return g_canvas != nullptr; }

void canvas_update() {
    int w = 640, h = 480;
    platform_canvas_size(&w, &h);
    if (w == g_canvas_geom.w && h == g_canvas_geom.h) return;
    free_canvas();
    g_canvas_geom = {w, h, (w - 640) / 2, h - 480, (h - 480) / 2};
    if (w != 640 || h != 480) {
        g_canvas = create_bitmap(w, h);
        g_wide_game = create_bitmap(w, h);
        g_wide_shadow = create_bitmap(w, h);
        g_hud_layer = create_bitmap(640, 480);
        hd::track(g_canvas);
        hd::track(g_wide_game);
        hd::track(g_hud_layer);
    }
    rescroll();
}

void free_canvas() {
    destroy(g_canvas);
    destroy(g_wide_game);
    destroy(g_wide_shadow);
    destroy(g_hud_layer);
    g_wide_valid = g_wide_fresh = false;
    g_canvas_geom = CanvasGeom();
}

void render_game_frame_wide() {
    const int ox = g_canvas_geom.ox, oy = g_canvas_geom.oy;
    Room *room = g_char_rooms[g_active_room_slot];
    hd::frame_begin(g_wide_game);
    clear_to_color(g_wide_game, 0);
    // a sala no centro: a origem (camera) e deslocada so durante o desenho
    room->origin_x += ox;
    room->origin_y += oy;
    Room_draw(room, g_wide_game, g_shadows_on, g_wide_shadow, g_shadows_complex);
    room->origin_x -= ox;
    room->origin_y -= oy;
    clear_to_color(g_hud_layer, MASK_COLOR_16);
    hud_draw(g_hud_layer);
    hud_side_fill(g_wide_game);
    masked_blit(g_hud_layer, g_wide_game, 0, 0, ox, oy, 640, 480);
    // o miolo continua em g_backbuffer, como se render_game_frame tivesse desenhado nele
    blit(g_wide_game, g_backbuffer, ox, oy, 0, 0, 640, 480);
    g_wide_valid = true;
    g_wide_fresh = true;
}

void canvas_flash() { g_flash = true; }

void present_frame(BITMAP *target) {
    if (!g_canvas) {
        request_video_bitmap(target);
        return;
    }
    BITMAP *c = g_canvas;
    const bool game = g_game_state == 1 || g_game_state == 3;
    const int ox = g_canvas_geom.ox, oy = game ? g_canvas_geom.oy : g_canvas_geom.oy_menu;
    if (g_flash) {
        clear_to_color(c, makecol(0xff, 0xff, 0xff));
    } else if (g_game_state == 1 && !g_transition_active && g_wide_fresh) {
        blit(g_wide_game, c, 0, 0, 0, 0, c->w, c->h);
    } else if ((g_game_state == 1 || g_game_state == 3) && g_wide_valid) {
        // transicao de sala / pausa: a tela larga escurece como o miolo (game_render, pause_menu_open)
        int light = 0x80;
        if (g_game_state == 1)
            light = (g_transition_type != 0 || g_transition_frame > 0x1f) ? 0 : g_transition_frame * -8 + 0x100;
        clear_to_color(c, 0);
        if (light > 0) draw_sprite_lit(c, g_wide_game, 0, 0, light);
        hud_side_fill(c);
    } else {
        clear_to_color(c, 0);
        ambient_fill(c, target, oy);
    }
    blit(target, c, 0, 0, ox, oy, 640, 480);  // o miolo, com o que foi desenhado por cima dele
    g_wide_fresh = false;
    g_flash = false;
    request_video_bitmap(c);
}

}  // namespace hoh

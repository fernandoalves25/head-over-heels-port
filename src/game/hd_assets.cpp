// Port (renderizador HD): nomes das imagens e etiquetas de pisos e paredes (ver hd_assets.h).
#include "game/hd_assets.h"

#include "compat/hd_render.h"
#include "compat/platform.h"
#include "data/hohdata.h"
#include "game/data_bridge.h"
#include "game/engine_data.h"
#include "game/engine_types.h"
#include "game/globals.h"
#include "game/room_load.h"
#include "game/screens.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace hoh {

namespace {

std::string fmt(const char *f, int a, int b = 0, int c = 0) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

// Imagem de coordenadas: cada pixel opaco guarda 1 + y*w + x (nunca magenta nem 0xFFFF para as
// imagens do jogo, que tem no maximo poucos milhares de pixels); os transparentes ficam magenta.
BITMAP *coord_image(const BITMAP *src) {
    BITMAP *c = create_bitmap(src->w, src->h);
    for (int y = 0; y < src->h; y++)
        for (int x = 0; x < src->w; x++)
            c->line[y][x] = src->line[y][x] == MASK_COLOR_16 ? MASK_COLOR_16
                                                              : static_cast<uint16_t>(1 + y * src->w + x);
    return c;
}

// real = recorte de verdade; coord = o mesmo recorte feito sobre coord_image(src).
void tag_from_coords(BITMAP *real, const BITMAP *coord, const BITMAP *src) {
    const int asset = hd::asset_of(src);
    const int n = src->w * src->h;
    if (!asset || !real || !coord || real->w != coord->w || real->h != coord->h || n >= MASK_COLOR_16) return;
    std::vector<int32_t> prov(static_cast<size_t>(real->w) * real->h);
    for (int y = 0; y < real->h; y++) {
        for (int x = 0; x < real->w; x++) {
            const int c = coord->line[y][x];
            int32_t p;
            if (c == MASK_COLOR_16) p = -1;
            else if (c >= 1 && c <= n) p = (((c - 1) / src->w) << 16) | ((c - 1) % src->w);
            else p = -2;  // ex.: getpixel fora da imagem (-1) nas faces dos ladrilhos
            prov[static_cast<size_t>(y) * real->w + x] = p;
        }
    }
    hd::tag_derived(real, asset, std::move(prov));
}

}  // namespace

void hd_register_assets() {
    if (!g_data) return;
    const GameData &d = *g_data;
    for (size_t i = 0; i < d.blocks.size(); i++)
        for (size_t di = 0; di < d.blocks[i].dirs.size(); di++)
            for (size_t fi = 0; fi < d.blocks[i].dirs[di].frames.size(); fi++) {
                const FramePair &fp = d.blocks[i].dirs[di].frames[fi];
                const std::string base = fmt("block%03d_d%d_f%02d", int(i), int(di), int(fi));
                hd::register_asset(fp.a, base + "_a");
                hd::register_asset(fp.b, base + "_b");
            }
    for (size_t i = 0; i < d.mobiles.size(); i++)
        for (size_t fi = 0; fi < d.mobiles[i].frames.size(); fi++)
            hd::register_asset(d.mobiles[i].frames[fi], fmt("mobile%03d_f%02d", int(i), int(fi)));
    for (size_t i = 0; i < d.named.size(); i++)
        for (size_t fi = 0; fi < d.named[i].frames.size(); fi++)
            hd::register_asset(d.named[i].frames[fi],
                               fmt("named_%03d_", int(i)) + d.named[i].name + fmt("_f%02d", int(fi)));
    for (size_t i = 0; i < d.worlds.size(); i++) {
        hd::register_asset(d.worlds[i].main_image, fmt("world%d_main", int(i)));
        for (int p = 0; p < 6; p++)
            for (int k = 0; k < 2; k++) {
                hd::register_asset(d.worlds[i].parts[p][k].a, fmt("world%d_p%d_%d_a", int(i), p, k));
                hd::register_asset(d.worlds[i].parts[p][k].b, fmt("world%d_p%d_%d_b", int(i), p, k));
            }
    }
    for (size_t i = 0; i < d.images.size(); i++) {
        const BITMAP *b = d.images[i];
        if (b) hd::register_asset(d.images[i], fmt("image%02d_%dx%d", int(i), b->w, b->h));
    }
}

void hd_tag_sets() {
    // pisos: Mobile_cut_tiles sobre uma copia do FloorSet com os quadros-fonte trocados
    for (int i = 0; i < g_mobile_count; i++) {
        FloorSet *m = g_mobiles[i];
        if (!m) continue;
        auto copy = std::make_unique<FloorSet>(*m);
        for (int f = 0; f < m->nframes && f < kSetMaxFrames; f++) {
            if (!m->src_frames[f] || !m->tile_bmp[0][0][f]) continue;
            copy->src_frames[f] = coord_image(m->src_frames[f]);
            Mobile_cut_tiles(copy.get(), f, m->flip);
            for (int r = 0; r < m->n; r++)
                for (int c = 0; c < m->n; c++) {
                    tag_from_coords(m->tile_bmp[r][c][f], copy->tile_bmp[r][c][f], m->src_frames[f]);
                    destroy_bitmap(copy->tile_bmp[r][c][f]);
                }
            destroy_bitmap(copy->src_frames[f]);
        }
    }
    // paredes: Named_slice_frame idem; as celulas B sao o espelho das A
    for (int i = 0; i < g_named_count; i++) {
        Named *s = g_named[i];
        if (!s) continue;
        auto copy = std::make_unique<Named>(*s);
        for (int f = 0; f < s->nframes && f < kSetMaxFrames; f++) {
            if (!s->src_frames[f] || !s->cells_a[0][0][f]) continue;
            copy->src_frames[f] = coord_image(s->src_frames[f]);
            Named_slice_frame(copy.get(), f);
            for (int c = 0; c < s->cols; c++)
                for (int r = 0; r < s->rows; r++) {
                    tag_from_coords(s->cells_a[c][r][f], copy->cells_a[c][r][f], s->src_frames[f]);
                    hd::tag_flip_copy(s->cells_b[c][r][f], s->cells_a[c][r][f]);
                    destroy_bitmap(copy->cells_a[c][r][f]);
                    destroy_bitmap(copy->cells_b[c][r][f]);
                }
            destroy_bitmap(copy->src_frames[f]);
        }
    }
}

void hd_room_tour(const char *out_dir) {
    const std::string dir = out_dir;
    FILE *rep = std::fopen((dir + "/salas.txt").c_str(), "w");
    if (!rep) return;
    int total = 0, bad = 0;
    for (int id = 0; id < kRoomGrid; id++) {
        if (g_room_table[id] < 0) continue;
        Room *r = load_room(id);
        if (!r) continue;
        Room *saved = g_char_rooms[g_active_room_slot];
        g_char_rooms[g_active_room_slot] = r;
        render_game_frame(g_backbuffer);
        size_t ops = 0, snaps = 0;
        const int diff = hd::verify_diff(g_backbuffer, &ops, &snaps);
        std::fprintf(rep, "sala %4d (%02d-%02d) parede %3d escura %d: ops %zu copias %zu diferentes %d\n", id, id % 40,
                     id / 40, r->wall_named, r->dark ? 1 : 0, ops, snaps, diff);
        total++;
        if (diff) bad++;
        if (diff) {  // a sala 1x e a lista refeita em 1x, para comparar
            BITMAP *rep1x = create_bitmap(g_backbuffer->w, g_backbuffer->h);
            hd::verify_diff(g_backbuffer, nullptr, nullptr, rep1x);
            platform_save_bitmap(g_backbuffer, (dir + fmt("/sala_%04d_1x.bmp", id)).c_str());
            platform_save_bitmap(rep1x, (dir + fmt("/sala_%04d_lista.bmp", id)).c_str());
            destroy_bitmap(rep1x);
        }
        if (total % 12 == 1 || diff) {  // algumas capturas (e todas as salas com diferenca)
            blit(g_backbuffer, screen, 0, 0, 0, 0, g_screen_w, g_screen_h);
            platform_capture_next((dir + fmt("/sala_%04d.bmp", id)).c_str());
            request_video_bitmap(screen);
        }
        hd::frame_done();
        g_char_rooms[g_active_room_slot] = saved;
        Room_destroy(r);
    }
    std::fprintf(rep, "total %d salas, %d com diferencas\n", total, bad);
    std::fclose(rep);
}

}  // namespace hoh

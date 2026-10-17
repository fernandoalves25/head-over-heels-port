// Renderizador HD, parte SDL: pacotes, texturas (QOI) e redesenho da lista na GPU (ver hd_render.h).
#include "compat/hd_internal.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace hd {

using namespace detail;

namespace {

SDL_Renderer *g_r = nullptr;
std::string g_dir = "hd";
std::vector<std::string> g_packs;
int g_current = -1;
int g_pack_k = 0;  // texels por pixel 1x do pacote atual (0 = ainda nao medido)

struct Tex {
    SDL_Texture *t = nullptr;
    int k = 1;  // texels por pixel 1x
};
struct Rgba {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};
std::vector<Tex> g_asset_tex;                // por asset (pacote atual)
std::vector<uint8_t> g_asset_tried;
std::unordered_map<int, Rgba> g_asset_rgba;  // pixels HD dos assets usados por derivadas
std::unordered_map<int, Tex> g_derived_tex;  // por etiqueta
struct CacheTex {
    SDL_Texture *t = nullptr;
    uint32_t last = 0;
};
std::unordered_map<uint64_t, CacheTex> g_snap_tex;  // copias 1x, pelo hash do conteudo
uint32_t g_frame_no = 0;
SDL_Texture *g_union = nullptr;
int g_union_w = 0, g_union_h = 0;

inline uint8_t c5(int v) { return static_cast<uint8_t>((v * 255 + 15) / 31); }
inline uint8_t c6(int v) { return static_cast<uint8_t>((v * 255 + 31) / 63); }
inline void rgb565_to_rgba(uint16_t p, bool opaque, uint8_t *o) {
    if (p == MASK_COLOR_16 && !opaque) {
        o[0] = o[1] = o[2] = o[3] = 0;
        return;
    }
    o[0] = c5(p >> 11);
    o[1] = c6((p >> 5) & 63);
    o[2] = c5(p & 31);
    o[3] = 255;
}

SDL_Texture *make_tex(const uint8_t *rgba, int w, int h, bool linear) {
    if (!g_r || w <= 0 || h <= 0) return nullptr;
    // o filtro de cada textura vem da dica no momento da criacao (funciona em qualquer SDL2)
    const char *old = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    const std::string keep = old ? old : "";
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "linear" : "nearest");
    SDL_Texture *t = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, keep.c_str());
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

SDL_Texture *tex_from_bitmap(const BITMAP *b, bool linear) {
    std::vector<uint8_t> px(static_cast<size_t>(b->w) * b->h * 4);
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) rgb565_to_rgba(b->line[y][x], false, &px[(static_cast<size_t>(y) * b->w + x) * 4]);
    return make_tex(px.data(), b->w, b->h, linear);
}

// ------------------------------------------------------------------ QOI (qoiformat.org)
uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

bool qoi_decode(const std::vector<uint8_t> &d, Rgba &out) {
    if (d.size() < 22 || std::memcmp(d.data(), "qoif", 4) != 0) return false;
    const uint32_t w = be32(&d[4]), h = be32(&d[8]);
    if (w == 0 || h == 0 || w > 16384 || h > 16384) return false;
    out.w = static_cast<int>(w);
    out.h = static_cast<int>(h);
    out.px.assign(static_cast<size_t>(w) * h * 4, 0);
    uint8_t px[4] = {0, 0, 0, 255};
    uint8_t index[64][4] = {};
    const size_t end = d.size() - 8;  // 8 bytes de fim
    size_t p = 14;
    int run = 0;
    for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) {
        if (run > 0) {
            run--;
        } else if (p < end) {
            const int b1 = d[p++];
            if (b1 == 0xfe) {
                if (p + 3 > end) break;
                px[0] = d[p++]; px[1] = d[p++]; px[2] = d[p++];
            } else if (b1 == 0xff) {
                if (p + 4 > end) break;
                px[0] = d[p++]; px[1] = d[p++]; px[2] = d[p++]; px[3] = d[p++];
            } else if ((b1 & 0xc0) == 0x00) {
                std::memcpy(px, index[b1], 4);
            } else if ((b1 & 0xc0) == 0x40) {
                px[0] = static_cast<uint8_t>(px[0] + ((b1 >> 4) & 3) - 2);
                px[1] = static_cast<uint8_t>(px[1] + ((b1 >> 2) & 3) - 2);
                px[2] = static_cast<uint8_t>(px[2] + (b1 & 3) - 2);
            } else if ((b1 & 0xc0) == 0x80) {
                if (p + 1 > end) break;
                const int b2 = d[p++];
                const int vg = (b1 & 0x3f) - 32;
                px[0] = static_cast<uint8_t>(px[0] + vg - 8 + ((b2 >> 4) & 0x0f));
                px[1] = static_cast<uint8_t>(px[1] + vg);
                px[2] = static_cast<uint8_t>(px[2] + vg - 8 + (b2 & 0x0f));
            } else {
                run = b1 & 0x3f;
            }
            std::memcpy(index[(px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) % 64], px, 4);
        }
        std::memcpy(&out.px[i * 4], px, 4);
    }
    return true;
}

bool load_qoi(const std::string &path, Rgba &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return qoi_decode(d, out);
}

// Pixels HD de um asset; false se o pacote nao tem a imagem ou ela nao e um multiplo inteiro do 1x.
bool asset_rgba(int asset, const Rgba **out, int *k) {
    auto it = g_asset_rgba.find(asset);
    if (it == g_asset_rgba.end()) {
        Rgba img;
        if (g_current < 0 || !load_qoi(g_dir + "/" + g_packs[g_current] + "/" + g_assets[asset].name + ".qoi", img))
            img.w = 0;
        it = g_asset_rgba.emplace(asset, std::move(img)).first;
    }
    const Rgba &img = it->second;
    const BITMAP *b = g_assets[asset].bmp;
    if (img.w == 0 || img.w % b->w != 0 || img.h % b->h != 0 || img.w / b->w != img.h / b->h) return false;
    *out = &img;
    *k = img.w / b->w;
    return true;
}

Tex asset_tex(int asset) {
    if (asset >= static_cast<int>(g_asset_tex.size())) {
        g_asset_tex.resize(g_assets.size());
        g_asset_tried.resize(g_assets.size(), 0);
    }
    Tex &t = g_asset_tex[asset];
    if (!g_asset_tried[asset]) {
        g_asset_tried[asset] = 1;
        const Rgba *img = nullptr;
        int k = 1;
        if (asset_rgba(asset, &img, &k)) {
            t.t = make_tex(img->px.data(), img->w, img->h, false);  // desenhado 1:1 (render_scale)
            t.k = k;
        }
        if (!t.t) {  // sem versao HD: a imagem 1x, sem filtro
            t.t = tex_from_bitmap(g_assets[asset].bmp, false);
            t.k = 1;
        }
    }
    return t;
}

// Textura propria de uma etiqueta: ladrilho/celula (cada pixel 1x vira o bloco k x k do asset HD de
// onde ele veio) ou o asset inteiro transformado (cinza).
Tex derived_tex(int tag) {
    auto it = g_derived_tex.find(tag);
    if (it != g_derived_tex.end()) return it->second;
    const Tag &t = g_tags[tag];
    const BITMAP *b = t.self;
    Tex out;
    const Rgba *src = nullptr;
    int k = 1;
    if (asset_rgba(t.asset, &src, &k)) {
        const int W = b->w * k, H = b->h * k;
        std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4, 0);
        if (t.prov.empty()) px = src->px;  // o asset inteiro (mesmo tamanho)
        for (int y = 0; y < b->h && !t.prov.empty(); y++) {
            for (int x = 0; x < b->w; x++) {
                const int32_t p = t.prov[static_cast<size_t>(y) * b->w + x];
                if (p == -1) continue;
                uint8_t c[4];  // cor 1x deste pixel (a textura fica na orientacao do asset)
                rgb565_to_rgba(b->line[y][t.flip ? b->w - 1 - x : x], false, c);
                int dist_sum = 0;
                for (int j = 0; j < k; j++) {
                    uint8_t *o = &px[((static_cast<size_t>(y) * k + j) * W + static_cast<size_t>(x) * k) * 4];
                    if (p < 0) {  // pixel que nao veio do asset
                        for (int i = 0; i < k; i++) std::memcpy(o + i * 4, c, 4);
                    } else {
                        const int sx = p & 0xffff, sy = p >> 16;
                        std::memcpy(o, &src->px[((static_cast<size_t>(sy) * k + j) * src->w + static_cast<size_t>(sx) * k) * 4],
                                    static_cast<size_t>(k) * 4);
                        for (int i = 0; i < k; i++)
                            dist_sum += std::abs(o[i * 4] - c[0]) + std::abs(o[i * 4 + 1] - c[1]) + std::abs(o[i * 4 + 2] - c[2]);
                    }
                }
                // Bloco que nao representa o pixel 1x (a borda suave da imagem HD cruza a escada da
                // mascara do ladrilho): os texels muito diferentes ficam com a cor 1x. Sem isso aparecem
                // pontinhos do ladrilho vizinho da imagem-fonte nas emendas do piso. O miolo, que
                // concorda com o 1x, fica intacto.
                if (p >= 0 && dist_sum > 60 * k * k)
                    for (int j = 0; j < k; j++) {
                        uint8_t *o = &px[((static_cast<size_t>(y) * k + j) * W + static_cast<size_t>(x) * k) * 4];
                        for (int i = 0; i < k; i++) {
                            uint8_t *q = o + i * 4;
                            if (std::abs(q[0] - c[0]) + std::abs(q[1] - c[1]) + std::abs(q[2] - c[2]) > 120 || q[3] < 128)
                                std::memcpy(q, c, 4);
                        }
                    }
            }
        }
        if (t.gray)
            for (size_t i = 0; i < px.size(); i += 4) {
                const uint8_t v = static_cast<uint8_t>((px[i] + px[i + 1] + px[i + 2]) / 4);
                px[i] = px[i + 1] = px[i + 2] = v;
            }
        out.t = make_tex(px.data(), W, H, false);
        out.k = k;
    }
    if (!out.t) {
        out.t = tex_from_bitmap(b, false);
        out.k = 1;
    }
    g_derived_tex[tag] = out;
    return out;
}

SDL_Texture *snap_tex(const Snap &s, bool linear, bool opaque) {
    const uint64_t key = s.hash ^ (linear ? 0x9e3779b97f4a7c15ull : 0) ^ (opaque ? 0x632be59bd9b4e019ull : 0);
    auto it = g_snap_tex.find(key);
    if (it != g_snap_tex.end()) {
        it->second.last = g_frame_no;
        return it->second.t;
    }
    std::vector<uint8_t> px(static_cast<size_t>(s.w) * s.h * 4);
    for (size_t i = 0; i < s.px.size(); i++) rgb565_to_rgba(s.px[i], opaque, &px[i * 4]);
    SDL_Texture *t = make_tex(px.data(), s.w, s.h, linear);
    g_snap_tex[key] = {t, g_frame_no};
    return t;
}

void evict_snaps() {
    if (g_snap_tex.size() < 1024) return;
    for (auto it = g_snap_tex.begin(); it != g_snap_tex.end();) {
        if (g_frame_no - it->second.last > 120) {
            if (it->second.t) SDL_DestroyTexture(it->second.t);
            it = g_snap_tex.erase(it);
        } else {
            ++it;
        }
    }
}

void drop_pack_textures() {
    for (Tex &t : g_asset_tex)
        if (t.t) SDL_DestroyTexture(t.t);
    g_asset_tex.clear();
    g_asset_tried.clear();
    for (auto &kv : g_derived_tex)
        if (kv.second.t) SDL_DestroyTexture(kv.second.t);
    g_derived_tex.clear();
    g_asset_rgba.clear();
}

void on_tag_released(int tag) {
    auto it = g_derived_tex.find(tag);
    if (it == g_derived_tex.end()) return;
    if (it->second.t) SDL_DestroyTexture(it->second.t);
    g_derived_tex.erase(it);
}

}  // namespace

void attach(SDL_Renderer *r) {
    g_r = r;
    g_tag_released = on_tag_released;
}

void detach() {
    drop_pack_textures();
    for (auto &kv : g_snap_tex)
        if (kv.second.t) SDL_DestroyTexture(kv.second.t);
    g_snap_tex.clear();
    if (g_union) SDL_DestroyTexture(g_union);
    g_union = nullptr;
    g_union_w = g_union_h = 0;
    g_r = nullptr;
}

void scan_packs(const std::string &dir) {
    g_dir = dir;
    g_packs.clear();
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_directory(ec)) continue;
        std::error_code ec2;
        for (const auto &f : std::filesystem::directory_iterator(e.path(), ec2)) {
            if (f.path().extension() == ".qoi") {
                g_packs.push_back(e.path().filename().string());
                break;
            }
        }
    }
    std::sort(g_packs.begin(), g_packs.end());
    if (g_current >= static_cast<int>(g_packs.size())) g_current = -1;
}

const std::vector<std::string> &packs() { return g_packs; }
int current_pack() { return g_current; }

void select_pack(int index) {
    if (index >= static_cast<int>(g_packs.size())) index = -1;
    if (index == g_current) return;
    drop_pack_textures();
    g_current = index;
    g_pack_k = 0;  // medida no primeiro render_scale(), quando os assets ja estao registrados
}

int render_scale() {
    if (g_current < 0) return 1;
    if (g_pack_k) return g_pack_k;
    // escala do pacote: a 1a imagem que existir (todas tem a mesma); sem nenhuma, 4
    g_pack_k = 4;
    for (size_t a = 1; a < g_assets.size(); a++) {
        std::ifstream f(g_dir + "/" + g_packs[g_current] + "/" + g_assets[a].name + ".qoi", std::ios::binary);
        uint8_t hdr[14];
        if (!f.read(reinterpret_cast<char *>(hdr), sizeof hdr) || std::memcmp(hdr, "qoif", 4) != 0) continue;
        const int w = static_cast<int>(be32(&hdr[4]));
        const BITMAP *b = g_assets[a].bmp;
        if (b->w > 0 && w % b->w == 0) g_pack_k = std::clamp(w / b->w, 1, 8);
        break;
    }
    return g_pack_k;
}

bool frame_ready(const BITMAP *bmp) { return g_r && g_current >= 0 && bmp && live(list_of(bmp)); }

void render(SDL_Renderer *r, SDL_Texture *target, const BITMAP *bmp, int out_w, int out_h) {
    const List *l = list_of(bmp);
    if (!l) return;
    g_frame_no++;
    const double kx = static_cast<double>(out_w) / bmp->w, ky = static_cast<double>(out_h) / bmp->h;
    // bordas arredondadas (nao larguras): pecas vizinhas se encostam sem fresta
    auto rect = [&](int x, int y, int w, int h) {
        SDL_Rect d;
        d.x = static_cast<int>(std::lround(x * kx));
        d.y = static_cast<int>(std::lround(y * ky));
        d.w = static_cast<int>(std::lround((x + w) * kx)) - d.x;
        d.h = static_cast<int>(std::lround((y + h) * ky)) - d.y;
        return d;
    };
    bool in_group = false;
    int group_alpha = kFull;
    for (const Op &op : l->ops) {
        switch (op.kind) {
            case OP_FILL: {
                const uint16_t c = op.color;
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(r, c5(c >> 11), c6((c >> 5) & 63), c5(c & 31), 255);
                const SDL_Rect d = rect(op.dx, op.dy, op.w, op.h);
                SDL_RenderFillRect(r, &d);
                break;
            }
            case OP_GROUP_BEGIN: {
                if (!g_union || g_union_w != out_w || g_union_h != out_h) {
                    if (g_union) SDL_DestroyTexture(g_union);
                    g_union = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, out_w, out_h);
                    g_union_w = g_union ? out_w : 0;
                    g_union_h = g_union ? out_h : 0;
                    if (g_union) SDL_SetTextureBlendMode(g_union, SDL_BLENDMODE_BLEND);
                }
                if (g_union && SDL_SetRenderTarget(r, g_union) == 0) {
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
                    SDL_RenderClear(r);
                    in_group = true;
                    group_alpha = op.alpha;
                }
                break;
            }
            case OP_GROUP_END: {
                if (!in_group) break;
                in_group = false;
                SDL_SetRenderTarget(r, target);
                SDL_SetTextureColorMod(g_union, 0, 0, 0);
                SDL_SetTextureAlphaMod(g_union, static_cast<Uint8>(std::min(255, group_alpha * 8)));
                SDL_RenderCopy(r, g_union, nullptr, nullptr);
                break;
            }
            case OP_SPRITE: {
                SDL_Texture *t = nullptr;
                int k = 1, sx = op.sx;
                bool flip = (op.flags & HFLIP) != 0;
                if (op.tag) {
                    const Tag &tg = g_tags[op.tag];
                    if (!tg.used) break;
                    const Tex tx = (tg.prov.empty() && !tg.gray) ? asset_tex(tg.asset) : derived_tex(op.tag);
                    t = tx.t;
                    k = tx.k;
                    if (tg.flip) {  // o bitmap desenhado e o espelho do que esta na textura
                        sx = tg.self->w - op.sx - op.w;
                        flip = !flip;
                    }
                } else if (op.snap >= 0) {
                    t = snap_tex(l->snaps[op.snap], (op.flags & LINEAR) != 0, (op.flags & OPAQUE) != 0);
                }
                if (!t) break;
                const SDL_Rect s = {sx * k, op.sy * k, op.w * k, op.h * k};
                const SDL_Rect d = rect(op.dx, op.dy, op.w, op.h);
                if (in_group) {  // silhueta preta na uniao
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureColorMod(t, 0, 0, 0);
                    SDL_SetTextureAlphaMod(t, 255);
                } else if (op.flags & ADD) {
                    const Uint8 f = static_cast<Uint8>(std::min(255, op.alpha * 8));
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
                    SDL_SetTextureColorMod(t, f, f, f);
                    SDL_SetTextureAlphaMod(t, 255);
                } else {
                    const Uint8 c = static_cast<Uint8>(std::min(255, op.tint * 8));
                    SDL_SetTextureBlendMode(t, (op.flags & OPAQUE) ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
                    SDL_SetTextureColorMod(t, c, c, c);
                    SDL_SetTextureAlphaMod(t, static_cast<Uint8>(std::min(255, op.alpha * 8)));
                }
                SDL_RenderCopyEx(r, t, &s, &d, 0.0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                break;
            }
        }
    }
    if (in_group) SDL_SetRenderTarget(r, target);
    evict_snaps();
}

}  // namespace hd

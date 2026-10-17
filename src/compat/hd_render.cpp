// Renderizador HD, parte SDL: pacotes, texturas (QOI) e redesenho da lista na GPU (ver hd_render.h).
//
// Texturas guardam o alfa pre-multiplicado (se o renderer aceita modos de mistura proprios): assim
// uma lista de origem desenhada numa textura propria (painel em fade, linha de menu esticada,
// quadro do jogo escurecido) pode ser misturada de novo sem escurecer as bordas suaves do pacote.
#include "compat/hd_internal.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
bool g_premul = false;
SDL_BlendMode g_bm_over = SDL_BLENDMODE_BLEND;  // origem pre-multiplicada "sobre" o destino
SDL_BlendMode g_bm_add = SDL_BLENDMODE_ADD;

struct Rgba {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};
struct Tex {
    SDL_Texture *t = nullptr;
    int k = 1;  // texels por pixel 1x
    uint32_t last = 0;  // assets: ultimo quadro usado
};
// Textura em cache (copias, listas, etiquetas derivadas), com o ultimo quadro em que foi usada.
struct Cached {
    SDL_Texture *t = nullptr;
    int k = 1;
    int w = 0, h = 0;
    bool linear = false, target = false;
    uint32_t last = 0;
    size_t bytes() const { return static_cast<size_t>(w) * h * 4; }
};

std::vector<Tex> g_asset_tex;                // por asset (pacote atual)
std::vector<uint8_t> g_asset_tried;
std::unordered_map<int, Rgba> g_asset_rgba;  // pixels HD dos assets usados por derivadas
std::unordered_map<uint64_t, std::vector<uint8_t>> g_stretch_rgba;  // assets esticados (stretch_sprite)
std::unordered_map<int, Cached> g_tag_tex;   // etiquetas derivadas
std::unordered_map<uint64_t, Cached> g_snap_tex;  // copias 1x, pelo hash do conteudo
std::unordered_map<uint64_t, Cached> g_comp_tex;  // listas de origem desenhadas, pelo hash
std::vector<Cached> g_pool;                       // texturas soltas para reaproveitar
uint32_t g_frame_no = 0;
int g_k = 4;  // escala das texturas de lista neste quadro
SDL_Texture *g_union = nullptr;
int g_union_w = 0, g_union_h = 0;

inline uint8_t c5(int v) { return static_cast<uint8_t>((v * 255 + 15) / 31); }
inline uint8_t c6(int v) { return static_cast<uint8_t>((v * 255 + 31) / 63); }
inline void rgb565_to_rgba(uint16_t p, uint8_t *o) {
    if (p == MASK_COLOR_16) {
        o[0] = o[1] = o[2] = o[3] = 0;
        return;
    }
    o[0] = c5(p >> 11);
    o[1] = c6((p >> 5) & 63);
    o[2] = c5(p & 31);
    o[3] = 255;
}

void destroy(SDL_Texture *t) {
    if (t) SDL_DestroyTexture(t);
}

SDL_Texture *new_texture(int w, int h, bool linear, bool target) {
    if (!g_r || w <= 0 || h <= 0) return nullptr;
    for (size_t i = 0; i < g_pool.size(); i++) {
        const Cached &c = g_pool[i];
        if (c.w == w && c.h == h && c.linear == linear && c.target == target) {
            SDL_Texture *t = c.t;
            g_pool.erase(g_pool.begin() + static_cast<std::ptrdiff_t>(i));
            return t;
        }
    }
    // o filtro de cada textura vem da dica no momento da criacao (funciona em qualquer SDL2)
    const char *old = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    const std::string keep = old ? old : "";
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "linear" : "nearest");
    // RGBA na memoria para as estaticas (como os bytes de make_tex); alvos no formato nativo
    SDL_Texture *t = target ? SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h)
                            : SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, keep.c_str());
    return t;
}

void recycle(Cached c) {
    if (!c.t) return;
    size_t total = 0;
    for (const Cached &p : g_pool) total += p.bytes();
    if (g_pool.size() >= 12 || total + c.bytes() > (64u << 20)) {
        destroy(c.t);
        return;
    }
    c.last = g_frame_no;  // no reaproveitamento: desde quando
    g_pool.push_back(c);
}

// rgba: RGBA8 com alfa reto; pre-multiplica se o renderer mistura assim.
SDL_Texture *make_tex(std::vector<uint8_t> &rgba, int w, int h, bool linear) {
    SDL_Texture *t = new_texture(w, h, linear, false);
    if (!t) return nullptr;
    if (g_premul)
        for (size_t i = 0; i < rgba.size(); i += 4)
            if (rgba[i + 3] != 255)
                for (int c = 0; c < 3; c++) rgba[i + c] = static_cast<uint8_t>((rgba[i + c] * rgba[i + 3] + 127) / 255);
    SDL_UpdateTexture(t, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(t, g_bm_over);
    return t;
}

SDL_Texture *tex_from_px(const uint16_t *px, int w, int h, bool linear) {
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; i++) rgb565_to_rgba(px[i], &rgba[i * 4]);
    return make_tex(rgba, w, h, linear);
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
    const Asset &a = g_assets[asset];
    if (img.w == 0 || a.w <= 0 || a.h <= 0 || img.w % a.w != 0 || img.h % a.h != 0 || img.w / a.w != img.h / a.h) return false;
    *out = &img;
    *k = img.w / a.w;
    return true;
}

Tex asset_tex(int asset) {
    if (asset >= static_cast<int>(g_asset_tex.size())) {
        g_asset_tex.resize(g_assets.size());
        g_asset_tried.resize(g_assets.size(), 0);
    }
    Tex &t = g_asset_tex[asset];
    t.last = g_frame_no;
    if (!g_asset_tried[asset]) {
        g_asset_tried[asset] = 1;
        const Rgba *img = nullptr;
        int k = 1;
        const bool cached = g_asset_rgba.count(asset) != 0;
        if (asset_rgba(asset, &img, &k)) {
            std::vector<uint8_t> px = img->px;
            t.t = make_tex(px, img->w, img->h, false);  // desenhado 1:1 (render_scale)
            t.k = k;
        }
        if (!cached) g_asset_rgba.erase(asset);  // os pixels so ficam para quem deriva deles (pisos)
        if (!t.t) {  // sem versao HD: a imagem 1x, sem filtro
            const Tag &tg = g_tags[g_assets[asset].tag];
            t.t = tex_from_px(tg.px->data(), tg.w, tg.h, false);
            t.k = 1;
        }
    }
    return t;
}

// ------------------------------------------------------------------ texturas derivadas
// Reduz (ou amplia) RGBA com media por area, em alfa pre-multiplicado (stretch_sprite da fonte).
void resample(const Rgba &src, int W, int H, std::vector<uint8_t> &out) {
    auto pass = [](const std::vector<float> &in, int iw, int ih, int ow, int oh, bool horiz) {
        std::vector<float> o(static_cast<size_t>(ow) * oh * 4, 0.0f);
        const int n_in = horiz ? iw : ih, n_out = horiz ? ow : oh, other = horiz ? ih : iw;
        const double s = static_cast<double>(n_in) / n_out;
        for (int i = 0; i < n_out; i++) {
            const double a = i * s, b = (i + 1) * s;
            for (int j = static_cast<int>(a); j < n_in && j < b; j++) {
                const double wgt = (std::min<double>(b, j + 1) - std::max<double>(a, j)) / s;
                if (wgt <= 0) continue;
                for (int q = 0; q < other; q++) {
                    const size_t from = horiz ? (static_cast<size_t>(q) * iw + j) * 4 : (static_cast<size_t>(j) * iw + q) * 4;
                    const size_t to = horiz ? (static_cast<size_t>(q) * ow + i) * 4 : (static_cast<size_t>(i) * ow + q) * 4;
                    for (int c = 0; c < 4; c++) o[to + c] += static_cast<float>(in[from + c] * wgt);
                }
            }
        }
        return o;
    };
    std::vector<float> f(src.px.size());
    for (size_t i = 0; i < src.px.size(); i += 4) {
        const float a = src.px[i + 3] / 255.0f;
        for (int c = 0; c < 3; c++) f[i + c] = src.px[i + c] * a;
        f[i + 3] = src.px[i + 3];
    }
    int w = src.w, h = src.h;
    if (W != w) { f = pass(f, w, h, W, h, true); w = W; }
    if (H != h) { f = pass(f, w, h, w, H, false); h = H; }
    out.assign(static_cast<size_t>(W) * H * 4, 0);
    for (size_t i = 0; i < out.size(); i += 4) {
        const float a = f[i + 3];
        out[i + 3] = static_cast<uint8_t>(std::clamp(a + 0.5f, 0.0f, 255.0f));
        if (a <= 0.0f) continue;
        for (int c = 0; c < 3; c++) out[i + c] = static_cast<uint8_t>(std::clamp(f[i + c] * 255.0f / a + 0.5f, 0.0f, 255.0f));
    }
}

// Pixels HD (RGBA8 reto) de uma etiqueta, opcionalmente com outro recorte por pixel (prov). A
// textura fica na orientacao do asset (o espelho e aplicado no desenho).
bool build_rgba(const Tag &t, const std::vector<int32_t> &prov, std::vector<uint8_t> &out, int *k_out) {
    const Rgba *src = nullptr;
    int k = 1;
    if (!asset_rgba(t.asset, &src, &k)) return false;
    const int W = t.w * k, H = t.h * k;
    auto xf_all = [&](uint8_t *p, size_t n) {
        for (const Xform &x : t.xf) xform_rgba(x, p, n);
    };
    if (!prov.empty()) {
        out.assign(static_cast<size_t>(W) * H * 4, 0);
        const uint16_t *px1 = t.px->data();
        for (int y = 0; y < t.h; y++) {
            for (int x = 0; x < t.w; x++) {
                const int32_t p = prov[static_cast<size_t>(y) * t.w + x];
                if (p == -1) continue;
                uint8_t c[4];  // cor 1x deste pixel (ja transformada; a textura fica na orientacao do asset)
                rgb565_to_rgba(px1[static_cast<size_t>(y) * t.w + (t.flip ? t.w - 1 - x : x)], c);
                if (c[3] == 0) continue;
                int dist_sum = 0;
                for (int j = 0; j < k; j++) {
                    uint8_t *o = &out[((static_cast<size_t>(y) * k + j) * W + static_cast<size_t>(x) * k) * 4];
                    if (p < 0) {  // pixel que nao veio do asset
                        for (int i = 0; i < k; i++) std::memcpy(o + i * 4, c, 4);
                    } else {
                        const int sx = p & 0xffff, sy = p >> 16;
                        std::memcpy(o, &src->px[((static_cast<size_t>(sy) * k + j) * src->w + static_cast<size_t>(sx) * k) * 4],
                                    static_cast<size_t>(k) * 4);
                        xf_all(o, static_cast<size_t>(k));
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
                        uint8_t *o = &out[((static_cast<size_t>(y) * k + j) * W + static_cast<size_t>(x) * k) * 4];
                        for (int i = 0; i < k; i++) {
                            uint8_t *q = o + i * 4;
                            if (std::abs(q[0] - c[0]) + std::abs(q[1] - c[1]) + std::abs(q[2] - c[2]) > 120 || q[3] < 128)
                                std::memcpy(q, c, 4);
                        }
                    }
            }
        }
    } else if (t.stretch_w) {
        // a fonte achatada serve de base para varias etiquetas (tingidas, silhuetas): reamostra uma vez
        const uint64_t key = (static_cast<uint64_t>(t.asset) << 32) | (static_cast<uint64_t>(t.stretch_w) << 16) | static_cast<uint64_t>(t.stretch_h);
        auto it = g_stretch_rgba.find(key);
        if (it == g_stretch_rgba.end()) {
            std::vector<uint8_t> px;
            resample(*src, W, H, px);
            it = g_stretch_rgba.emplace(key, std::move(px)).first;
        }
        out = it->second;
        xf_all(out.data(), out.size() / 4);
    } else {
        if (src->w != W || src->h != H) return false;
        out = src->px;
        xf_all(out.data(), out.size() / 4);
    }
    *k_out = k;
    return true;
}

Tex tag_tex(int tag) {
    const Tag &t = g_tags[tag];
    if (t.plain()) return asset_tex(t.asset);
    auto it = g_tag_tex.find(tag);
    if (it != g_tag_tex.end()) {
        it->second.last = g_frame_no;
        return {it->second.t, it->second.k};
    }
    Cached c;
    std::vector<uint8_t> px;
    int k = 1;
    if (build_rgba(t, t.prov, px, &k)) {
        c.t = make_tex(px, t.w * k, t.h * k, false);
        c.k = k;
    }
    if (!c.t) {
        c.t = tex_from_px(t.px->data(), t.w, t.h, false);
        c.k = 1;
    }
    c.w = t.w * c.k;
    c.h = t.h * c.k;
    c.last = g_frame_no;
    g_tag_tex[tag] = c;
    return {c.t, c.k};
}

// Copia 1x; com base, os pixels iguais aos da etiqueta usam o HD dela (work_bmp recortado).
Tex snap_tex(const Snap &s, bool linear) {
    const uint64_t key = s.hash ^ (linear ? 0x9e3779b97f4a7c15ull : 0);
    auto it = g_snap_tex.find(key);
    if (it != g_snap_tex.end()) {
        it->second.last = g_frame_no;
        return {it->second.t, it->second.k};
    }
    Cached c;
    if (s.base) {
        const Tag &b = g_tags[s.base];
        if (b.w == s.w && b.h == s.h && !b.stretch_w) {
            std::vector<int32_t> prov(s.px.size());
            const uint16_t *bp = b.px->data();
            for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++) {  // x na orientacao do asset
                    const size_t at = static_cast<size_t>(y) * s.w + (b.flip ? s.w - 1 - x : x);
                    const uint16_t p = s.px[at];
                    const size_t i = static_cast<size_t>(y) * s.w + x;
                    if (p == MASK_COLOR_16) prov[i] = -1;
                    else if (p != bp[at]) prov[i] = -2;
                    else prov[i] = b.prov.empty() ? ((y << 16) | x) : b.prov[i];
                }
            Tag t = b;
            t.px = std::make_shared<const std::vector<uint16_t>>(s.px);
            std::vector<uint8_t> px;
            int k = 1;
            if (build_rgba(t, prov, px, &k)) {
                // a textura e desenhada sem espelho: desfaz a orientacao do asset
                if (b.flip) {
                    const int W = s.w * k;
                    for (int y = 0; y < s.h * k; y++)
                        for (int x = 0; x < W / 2; x++)
                            for (int ch = 0; ch < 4; ch++)
                                std::swap(px[(static_cast<size_t>(y) * W + x) * 4 + ch], px[(static_cast<size_t>(y) * W + W - 1 - x) * 4 + ch]);
                }
                c.t = make_tex(px, s.w * k, s.h * k, linear);
                c.k = k;
            }
        }
    }
    if (!c.t) {
        c.t = tex_from_px(s.px.data(), s.w, s.h, linear);
        c.k = 1;
    }
    c.w = s.w * c.k;
    c.h = s.h * c.k;
    c.linear = linear;
    c.last = g_frame_no;
    g_snap_tex[key] = c;
    return {c.t, c.k};
}

void replay(const ListData &l, SDL_Texture *target, int out_w, int out_h);

// Lista de origem desenhada numa textura propria (g_k texels por pixel), com as que ela usa antes.
SDL_Texture *ensure_comp(const ListData &l) {
    auto it = g_comp_tex.find(l.hash);
    if (it != g_comp_tex.end()) {
        it->second.last = g_frame_no;
        return it->second.t;
    }
    for (const auto &s : l.subs) ensure_comp(*s);
    Cached c;
    c.w = l.w * g_k;
    c.h = l.h * g_k;
    c.k = g_k;
    c.linear = true;  // so faz diferenca quando esticada (linhas do menu)
    c.target = true;
    c.last = g_frame_no;
    c.t = new_texture(c.w, c.h, true, true);
    if (!c.t || SDL_SetRenderTarget(g_r, c.t) != 0) {
        destroy(c.t);
        return nullptr;
    }
    SDL_SetTextureBlendMode(c.t, g_bm_over);
    SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(g_r, 0, 0, 0, 0);
    SDL_RenderClear(g_r);
    replay(l, c.t, c.w, c.h);
    g_comp_tex[l.hash] = c;
    return c.t;
}

void replay(const ListData &l, SDL_Texture *target, int out_w, int out_h) {
    SDL_Renderer *r = g_r;
    const double kx = static_cast<double>(out_w) / l.w, ky = static_cast<double>(out_h) / l.h;
    // bordas arredondadas (nao larguras): pecas vizinhas se encostam sem fresta
    auto rect = [&](int x, int y, int w, int h) {
        SDL_Rect d;
        d.x = static_cast<int>(std::lround(x * kx));
        d.y = static_cast<int>(std::lround(y * ky));
        d.w = static_cast<int>(std::lround((x + w) * kx)) - d.x;
        d.h = static_cast<int>(std::lround((y + h) * ky)) - d.y;
        return d;
    };
    bool clipped = false;
    SDL_RenderSetClipRect(r, nullptr);
    auto clip_for = [&](const Op &op) {
        const int x0 = std::max({op.dx, 0}), y0 = std::max({op.dy, 0});
        const int x1 = std::min(op.dx + op.dw, l.w), y1 = std::min(op.dy + op.dh, l.h);
        const bool need = op.cx0 > x0 || op.cy0 > y0 || op.cx1 < x1 || op.cy1 < y1;
        if (need) {
            const SDL_Rect c = rect(op.cx0, op.cy0, op.cx1 - op.cx0, op.cy1 - op.cy0);
            SDL_RenderSetClipRect(r, &c);
            clipped = true;
        } else if (clipped) {
            SDL_RenderSetClipRect(r, nullptr);
            clipped = false;
        }
    };
    bool in_group = false;
    int group_alpha = kFull;
    for (const Op &op : l.ops) {
        switch (op.kind) {
            case OP_FILL: {
                clip_for(op);
                const uint16_t c = op.color;
                if (c == MASK_COLOR_16) {  // fundo transparente das listas de origem
                    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
                } else {
                    SDL_SetRenderDrawBlendMode(r, op.alpha < kFull ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                    SDL_SetRenderDrawColor(r, c5(c >> 11), c6((c >> 5) & 63), c5(c & 31), static_cast<Uint8>(std::min(255, op.alpha * 8)));
                }
                const SDL_Rect d = rect(op.dx, op.dy, op.dw, op.dh);
                SDL_RenderFillRect(r, &d);
                break;
            }
            case OP_SCALE: {  // escurece o que ja esta desenhado (preto por cima)
                clip_for(op);
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, 0, 0, 0, static_cast<Uint8>(255 - std::min(255, op.tint * 8)));
                const SDL_Rect d = rect(op.dx, op.dy, op.dw, op.dh);
                SDL_RenderFillRect(r, &d);
                break;
            }
            case OP_GROUP_BEGIN: {
                if (!g_union || g_union_w != out_w || g_union_h != out_h) {
                    destroy(g_union);
                    g_union = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, out_w, out_h);
                    g_union_w = g_union ? out_w : 0;
                    g_union_h = g_union ? out_h : 0;
                }
                if (g_union && SDL_SetRenderTarget(r, g_union) == 0) {
                    SDL_SetTextureBlendMode(g_union, g_bm_over);
                    SDL_RenderSetClipRect(r, nullptr);
                    clipped = false;
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
                SDL_RenderSetClipRect(r, nullptr);
                clipped = false;
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
                    const Tex tx = tag_tex(op.tag);
                    t = tx.t;
                    k = tx.k;
                    if (tg.flip) {  // o bitmap desenhado e o espelho do que esta na textura
                        sx = tg.w - op.sx - op.sw;
                        flip = !flip;
                    }
                } else if (op.snap >= 0) {
                    const Tex tx = snap_tex(*l.snaps[op.snap], (op.flags & LINEAR) != 0);
                    t = tx.t;
                    k = tx.k;
                } else if (op.sub >= 0) {
                    auto it = g_comp_tex.find(l.subs[op.sub]->hash);
                    if (it != g_comp_tex.end()) {
                        t = it->second.t;
                        k = it->second.k;
                    }
                }
                if (!t) break;
                clip_for(op);
                const SDL_Rect s = {sx * k, op.sy * k, op.sw * k, op.sh * k};
                const SDL_Rect d = rect(op.dx, op.dy, op.dw, op.dh);
                if (in_group) {  // silhueta preta na uniao
                    SDL_SetTextureBlendMode(t, g_bm_over);
                    SDL_SetTextureColorMod(t, 0, 0, 0);
                    SDL_SetTextureAlphaMod(t, 255);
                } else if (op.flags & ADD) {
                    const Uint8 f = static_cast<Uint8>(std::min(255, op.alpha * 8));
                    SDL_SetTextureBlendMode(t, g_bm_add);
                    SDL_SetTextureColorMod(t, f, f, f);
                    SDL_SetTextureAlphaMod(t, 255);
                } else {
                    const int a = std::min(255, op.alpha * 8), tint = std::min(255, op.tint * 8);
                    // pre-multiplicado: a cor tambem cai com o alfa
                    const int c = g_premul && !(op.flags & OPAQUE) ? tint * a / 255 : tint;
                    SDL_SetTextureBlendMode(t, (op.flags & OPAQUE) ? SDL_BLENDMODE_NONE : g_bm_over);
                    SDL_SetTextureColorMod(t, static_cast<Uint8>(c), static_cast<Uint8>(c), static_cast<Uint8>(c));
                    SDL_SetTextureAlphaMod(t, static_cast<Uint8>((op.flags & OPAQUE) ? 255 : a));
                }
                SDL_RenderCopyEx(r, t, &s, &d, 0.0, nullptr, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
                break;
            }
        }
    }
    if (in_group) SDL_SetRenderTarget(r, target);
    if (clipped || in_group) SDL_RenderSetClipRect(r, nullptr);
}

// Texturas que nao foram usadas ha um tempo saem do cache quando ele passa do orcamento (ou depois
// de max_age quadros sem uso: as cores do letreiro, as linhas do menu ja trocadas); copias grandes
// (a agua do menu, uma por quadro) voltam para o reaproveitamento ja no quadro seguinte.
template <typename Map>
void evict(Map &m, size_t budget, uint32_t min_age, uint32_t max_age, size_t big = 0) {
    size_t total = 0;
    std::vector<std::pair<uint32_t, typename Map::key_type>> old;
    for (auto it = m.begin(); it != m.end();) {
        const Cached &c = it->second;
        if ((big && c.last != g_frame_no && static_cast<size_t>(c.w) * c.h >= big) || g_frame_no - c.last > max_age) {
            recycle(c);
            it = m.erase(it);
            continue;
        }
        total += c.bytes();
        if (g_frame_no - c.last >= min_age) old.push_back({c.last, it->first});
        ++it;
    }
    if (total <= budget) return;
    std::sort(old.begin(), old.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &o : old) {
        if (total <= budget) break;
        auto it = m.find(o.second);
        total -= it->second.bytes();
        recycle(it->second);
        m.erase(it);
    }
}

// Etiquetas da interface criadas na inicializacao (fontes tingidas e achatadas, silhuetas, icones
// cinza): uma textura por quadro desde o primeiro quadro HD (a abertura), para o menu nao travar
// quando aparece (sao umas 40, de 2 a 6 ms cada). Um texel invisivel e desenhado no quadro: alguns
// renderers (D3D9) so mandam a textura para a GPU no primeiro desenho.
// A imagem grande do titulo (50 ms para decodificar e mandar) vai no primeiro quadro, ainda preto.
size_t g_warm_next = 1, g_warm_end = 0;
bool g_warm_big = false;
void touch_texture(SDL_Texture *tex) {
    if (!tex) return;
    const SDL_Rect one = {0, 0, 1, 1};
    SDL_RenderSetClipRect(g_r, nullptr);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, 0);
    SDL_RenderCopy(g_r, tex, &one, &one);
}
void prewarm() {
    if (!g_warm_big) {
        g_warm_big = true;
        for (size_t a = 1; a < g_assets.size(); a++)
            if (static_cast<size_t>(g_assets[a].w) * g_assets[a].h >= 200000) touch_texture(asset_tex(static_cast<int>(a)).t);
        return;
    }
    if (!g_warm_end) g_warm_end = g_tags.size();
    while (g_warm_next < g_warm_end) {
        const int id = static_cast<int>(g_warm_next++);
        const Tag &t = g_tags[id];
        if ((t.xf.empty() && !t.stretch_w) || !t.prov.empty() || g_tag_tex.count(id)) continue;
        touch_texture(tag_tex(id).t);
        return;
    }
}

void drop_all() {
    g_warm_next = 1;
    g_warm_big = false;
    for (Tex &t : g_asset_tex) destroy(t.t);
    g_asset_tex.clear();
    g_asset_tried.clear();
    g_asset_rgba.clear();
    g_stretch_rgba.clear();
    for (auto *m : {&g_snap_tex, &g_comp_tex})
        for (auto &kv : *m) destroy(kv.second.t);
    for (auto &kv : g_tag_tex) destroy(kv.second.t);
    g_tag_tex.clear();
    g_snap_tex.clear();
    g_comp_tex.clear();
    for (const Cached &c : g_pool) destroy(c.t);
    g_pool.clear();
}

}  // namespace

void attach(SDL_Renderer *r) {
    g_r = r;
    // alfa pre-multiplicado: modos proprios (mesmos fatores para cor e alfa, o D3D9 aceita sem
    // mistura de alfa separada); renderer que nao aceita (software) fica com os modos padrao
    const SDL_BlendMode over = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                                          SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    const SDL_BlendMode add = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
                                                         SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    g_premul = false;
    if (SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 1, 1)) {
        g_premul = SDL_SetTextureBlendMode(t, over) == 0 && SDL_SetTextureBlendMode(t, add) == 0;
        SDL_DestroyTexture(t);
    }
    g_bm_over = g_premul ? over : SDL_BLENDMODE_BLEND;
    g_bm_add = g_premul ? add : SDL_BLENDMODE_ADD;
}

void detach() {
    drop_all();
    destroy(g_union);
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
    drop_all();
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
        if (g_assets[a].w > 0 && w % g_assets[a].w == 0) g_pack_k = std::clamp(w / g_assets[a].w, 1, 8);
        break;
    }
    return g_pack_k;
}

bool frame_ready(const BITMAP *bmp) { return g_r && g_current >= 0 && bmp && list_of(bmp); }

void render(SDL_Renderer *r, SDL_Texture *target, const BITMAP *bmp, int out_w, int out_h) {
    const ListData *l = list_of(bmp);
    if (!l || r != g_r) return;
    g_frame_no++;
    g_k = render_scale();
    for (const auto &s : l->subs) ensure_comp(*s);  // antes: cada uma troca o alvo do renderer
    SDL_SetRenderTarget(r, target);
    replay(*l, target, out_w, out_h);
    evict(g_snap_tex, 32u << 20, 120, 600, 64 * 1024);
    evict(g_comp_tex, 64u << 20, 2, 120, 1u << 20);
    evict(g_tag_tex, 128u << 20, 300, UINT32_MAX);
    // etiquetas criadas depois da inicializacao (cores do letreiro, silhuetas do menu): por idade
    for (auto it = g_tag_tex.begin(); it != g_tag_tex.end();) {
        if (g_warm_end && static_cast<size_t>(it->first) >= g_warm_end && g_frame_no - it->second.last > 120) {
            recycle(it->second);
            it = g_tag_tex.erase(it);
        } else {
            ++it;
        }
    }
    // imagem grande (o titulo) sem uso ha 1800 quadros (mais que a abertura inteira) e texturas
    // paradas no reaproveitamento saem
    for (size_t a = 1; a < g_asset_tex.size(); a++) {
        Tex &t = g_asset_tex[a];
        if (t.t && g_frame_no - t.last > 1800 && static_cast<size_t>(g_assets[a].w) * g_assets[a].h >= 200000) {
            destroy(t.t);
            t = Tex();
            g_asset_tried[a] = 0;
        }
    }
    for (auto it = g_pool.begin(); it != g_pool.end();) {
        if (g_frame_no - it->last > 300) {
            destroy(it->t);
            it = g_pool.erase(it);
        } else {
            ++it;
        }
    }
    prewarm();
}

}  // namespace hd

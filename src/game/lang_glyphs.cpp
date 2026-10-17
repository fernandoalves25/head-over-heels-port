// Port: glifos extras das traducoes (ver lang_glyphs.h).
//
// Faixa extra: celulas da mesma largura da fonte, 64 por linha (uma faixa so ficaria larga demais
// para uma textura no renderizador HD), na ordem de lang_codepoints().
//   * letra acentuada: a letra da propria fonte (comprimida na vertical se nao sobra espaco em cima,
//     ou embaixo para a cedilha) + o acento na cor mais clara da fonte, com a sombra na cor mais
//     escura a esquerda e abaixo, como o contorno das letras da fonte grande;
//   * ¡ / ¿: o ! com o ponto e a barra trocados de lugar / o ? girado;
//   * chines (so fontes grandes): o glifo 16x16 da GNU Unifont (lang_cjk.inc), esticado na vertical
//     na fonte de 26 linhas (sem perder linha quando o menu reduz o item para 18), com o degrade
//     da haste do I da fonte e a mesma sombra.
// Os pixels das letras guardam o pixel de origem na fonte (proveniencia do renderizador HD); os
// acentos e o chines usam a cor 1x (-2).
#include "game/lang_glyphs.h"

#include "compat/hd_render.h"
#include "compat/platform.h"
#include "game/gfx_util.h"
#include "game/globals.h"
#include "game/lang.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace hoh {

namespace {

struct CjkGlyph {
    uint32_t cp;
    uint16_t rows[16];  // bit 15 = coluna 0
};
const CjkGlyph kCjk[] = {
#include "game/lang_cjk.inc"
};

const CjkGlyph *cjk_of(uint32_t cp) {
    for (const CjkGlyph &g : kCjk)
        if (g.cp == cp) return &g;
    return nullptr;
}

enum Mark : uint8_t { M_ACUTE, M_GRAVE, M_CIRC, M_TILDE, M_DIAER, M_CEDILLA, M_FLIP };
struct Latin {
    uint32_t cp;
    char base;
    Mark mark;
};
const Latin kLatin[] = {
    {0xC0, 'A', M_GRAVE}, {0xC1, 'A', M_ACUTE}, {0xC2, 'A', M_CIRC}, {0xC3, 'A', M_TILDE}, {0xC4, 'A', M_DIAER},
    {0xC7, 'C', M_CEDILLA},
    {0xC8, 'E', M_GRAVE}, {0xC9, 'E', M_ACUTE}, {0xCA, 'E', M_CIRC}, {0xCB, 'E', M_DIAER},
    {0xCC, 'I', M_GRAVE}, {0xCD, 'I', M_ACUTE}, {0xCE, 'I', M_CIRC}, {0xCF, 'I', M_DIAER},
    {0xD1, 'N', M_TILDE},
    {0xD2, 'O', M_GRAVE}, {0xD3, 'O', M_ACUTE}, {0xD4, 'O', M_CIRC}, {0xD5, 'O', M_TILDE}, {0xD6, 'O', M_DIAER},
    {0xD9, 'U', M_GRAVE}, {0xDA, 'U', M_ACUTE}, {0xDB, 'U', M_CIRC}, {0xDC, 'U', M_DIAER},
    {0xA1, '!', M_FLIP}, {0xBF, '?', M_FLIP},
};

// Minusculas (U+00E0..U+00FE) = maiuscula + 0x20, com a letra base minuscula.
bool latin_of(uint32_t cp, char *base, Mark *mark) {
    const bool lower = cp >= 0xE0 && cp <= 0xFE;
    const uint32_t up = lower ? cp - 0x20 : cp;
    for (const Latin &l : kLatin) {
        if (l.cp != up) continue;
        *base = lower ? static_cast<char>(l.base + 0x20) : l.base;
        *mark = l.mark;
        return true;
    }
    return false;
}

struct Shape {
    int w, h;
    const char *rows[4];
};
// Fonte grande (letras de 25 linhas com contorno): nucleo de ate 8x4, sombra a esquerda e abaixo.
const Shape kBig[] = {
    {7, 4, {"....###", "...###.", "..###..", ".###..."}},       // agudo
    {7, 4, {"###....", ".###...", "..###..", "...###."}},       // grave
    {8, 4, {"...##...", "..####..", ".##..##.", "##....##"}},   // circunflexo
    {8, 3, {".###...#", "##.##.##", "#...###."}},               // til
    {8, 2, {"###..###", "###..###"}},                           // trema
    {7, 3, {"..##...", "...###.", ".####.."}},                  // cedilha (embaixo)
};
// Fonte pequena (pixels dobrados, cor unica): 2 linhas acima da altura-x.
const Shape kSmall[] = {
    {4, 2, {"..##", "##.."}},
    {4, 2, {"##..", "..##"}},
    {6, 2, {"..##..", "##..##"}},
    {6, 2, {".##..#", "#..##."}},
    {6, 2, {"##..##", "##..##"}},
    {4, 2, {"..##", "##.."}},
};

constexpr int kCols = 64;  // celulas por linha da faixa

uint16_t mask16() { return static_cast<uint16_t>(g_mask_color); }
int lum565(uint16_t c) { return ((c >> 11) & 31) * 2 + ((c >> 5) & 63) + (c & 31) * 2; }
int popcount16(uint16_t v) {
    int n = 0;
    for (; v; v &= static_cast<uint16_t>(v - 1)) n++;
    return n;
}

// Uma celula da fonte (ou de uma faixa).
struct Glyph {
    const BITMAP *bmp;
    int x0, y0, w, h;
    uint16_t at(int x, int y) const { return bmp->line[y0 + y][x0 + x]; }
    bool opaque(int x, int y) const { return at(x, y) != mask16(); }
};

struct Extent {
    int top = -1, bottom = -1, left = -1, right = -1;
    bool ok() const { return top >= 0; }
};

// Estilo da fonte, tirado do I: cor mais clara e mais escura, degrade por linha da haste e a ultima
// linha opaca (base das letras).
struct Style {
    uint16_t bright = 0, dark = 0;
    bool shadow = false;
    int bottom = 0;
    std::vector<uint16_t> grad;
};

// Faixa em montagem: pixels e proveniencia HD.
struct Out {
    BITMAP *strip;
    std::vector<int32_t> prov;
    bool empty(int x, int y) const { return strip->line[y][x] == mask16(); }
    void set(int x, int y, uint16_t c, int32_t p) {
        strip->line[y][x] = c;
        prov[static_cast<size_t>(y) * strip->w + x] = p;
    }
};

struct Layout {
    std::vector<uint32_t> cps;                 // caractere de cada celula
    std::unordered_map<uint32_t, int> index;   // caractere -> celula
};

struct XFont {
    BITMAP *strip = nullptr;
    int cell = 0, h = 0;  // celula w x h
    std::shared_ptr<const Layout> layout;
};

std::unordered_map<const BITMAP *, XFont> g_fonts;                     // fonte -> faixa extra
std::unordered_map<const BITMAP *, std::vector<int32_t>> g_base_prov;  // proveniencia das faixas base
std::map<std::tuple<const BITMAP *, int, int, int, int>, BITMAP *> g_tint_cache;
std::vector<BITMAP *> g_owned;

Extent extent_of(const Glyph &g, const std::vector<uint8_t> *keep = nullptr) {
    Extent e;
    for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++) {
            if (!g.opaque(x, y) || (keep && !(*keep)[static_cast<size_t>(y) * g.w + x])) continue;
            if (e.top < 0) e.top = y;
            e.bottom = y;
            e.left = e.left < 0 ? x : std::min(e.left, x);
            e.right = std::max(e.right, x);
        }
    return e;
}

bool row_empty(const Glyph &g, int y) {
    for (int x = 0; x < g.w; x++)
        if (g.opaque(x, y)) return false;
    return true;
}

Style style_of(const BITMAP *font, int cell, bool mixed) {
    Style s;
    const int gi = mixed ? glyph_index_mixed('I') : glyph_index_upper('I');
    const Glyph g{font, gi * cell, 0, cell, font->h};
    int lo = INT_MAX, hi = -1;
    for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++) {
            if (!g.opaque(x, y)) continue;
            const int l = lum565(g.at(x, y));
            if (l > hi) hi = l, s.bright = g.at(x, y);
            if (l < lo) lo = l, s.dark = g.at(x, y);
        }
    s.shadow = hi > lo;
    const Extent e = extent_of(g);
    s.bottom = e.ok() ? e.bottom : font->h - 1;
    // coluna do meio da face da haste, na linha do meio
    int col = cell / 2;
    {
        const int my = g.h / 2;
        int a = -1, b = -1;
        for (int x = 0; x < g.w; x++)
            if (g.opaque(x, my) && (!s.shadow || g.at(x, my) != s.dark)) {
                if (a < 0) a = x;
                b = x;
            }
        if (a >= 0) col = (a + b) / 2;
    }
    s.grad.assign(static_cast<size_t>(font->h), s.bright);
    int first = -1;
    uint16_t last = s.bright;
    for (int y = 0; y < g.h; y++) {
        const uint16_t c = g.at(col, y);
        if (c != mask16() && (!s.shadow || c != s.dark)) {
            if (first < 0) first = y;
            last = c;
        }
        s.grad[y] = last;
    }
    for (int y = 0; y < first; y++) s.grad[y] = s.grad[first];
    return s;
}

// Sombra (cor escura a esquerda e abaixo) dos pixels de nucleo, so onde a celula esta vazia.
void add_shadow(Out &o, int ox, int oy, int cw, int ch, const std::vector<uint8_t> &core, uint16_t dark) {
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            if (!core[static_cast<size_t>(y) * cw + x]) continue;
            static const int kOff[3][2] = {{-1, 0}, {-1, 1}, {0, 1}};
            for (const auto &d : kOff) {
                const int sx = x + d[0], sy = y + d[1];
                if (sx < 0 || sy < 0 || sx >= cw || sy >= ch || core[static_cast<size_t>(sy) * cw + sx]) continue;
                if (o.empty(ox + sx, oy + sy)) o.set(ox + sx, oy + sy, dark, -2);
            }
        }
}

// Letra acentuada a partir da letra base g (celula da fonte).
void compose_latin(const Glyph &g, char base, Mark mark, bool small, const Style &st, Out &o, int ox, int oy) {
    std::vector<uint8_t> keep(static_cast<size_t>(g.w) * g.h, 1);
    Extent e = extent_of(g);
    if (!e.ok()) return;
    if (base == 'i' && small) {  // i sem pingo: tira o que vem antes da primeira linha vazia
        int y = e.top;
        while (y <= e.bottom && !row_empty(g, y)) y++;
        if (y < e.bottom)
            for (int yy = e.top; yy < y; yy++)
                for (int x = 0; x < g.w; x++) keep[static_cast<size_t>(yy) * g.w + x] = 0;
        e = extent_of(g, &keep);
    }
    const Shape &sh = (small ? kSmall : kBig)[mark];
    const int shadow_rows = st.shadow ? 1 : 0;
    const int core_bottom = small ? 2 : 3;      // ultima linha do nucleo do acento
    const int letter_top = small ? 4 : 5;       // primeira linha livre para a letra
    int nt = e.top, nb = e.bottom;
    if (mark == M_CEDILLA) nb = std::min(e.bottom, g.h - 1 - sh.h - shadow_rows);
    else nt = std::max(e.top, letter_top);
    // letra (linhas e.top..e.bottom -> nt..nb, extremos casados)
    for (int y = nt; y <= nb; y++) {
        const int sy = nb == nt ? e.top : e.top + ((y - nt) * (e.bottom - e.top) + (nb - nt) / 2) / (nb - nt);
        for (int x = 0; x < g.w; x++)
            if (g.opaque(x, sy) && keep[static_cast<size_t>(sy) * g.w + x])
                o.set(ox + x, oy + y, g.at(x, sy), (static_cast<int32_t>(g.y0 + sy) << 16) | (g.x0 + x));
    }
    // acento centrado na face da letra
    int fl = INT_MAX, fr = -1;
    for (int y = e.top; y <= e.bottom; y++)
        for (int x = 0; x < g.w; x++)
            if (g.opaque(x, y) && keep[static_cast<size_t>(y) * g.w + x] && (!st.shadow || g.at(x, y) != st.dark)) {
                fl = std::min(fl, x);
                fr = std::max(fr, x);
            }
    if (fr < 0) fl = e.left, fr = e.right;
    const int ax = std::clamp((fl + fr + 1) / 2 - sh.w / 2, shadow_rows, g.w - sh.w);
    const int ay = mark == M_CEDILLA ? nb + 1 : core_bottom - sh.h + 1;
    std::vector<uint8_t> core(static_cast<size_t>(g.w) * g.h, 0);
    for (int j = 0; j < sh.h; j++)
        for (int i = 0; i < sh.w; i++) {
            if (sh.rows[j][i] != '#') continue;
            o.set(ox + ax + i, oy + ay + j, st.bright, -2);
            core[static_cast<size_t>(ay + j) * g.w + ax + i] = 1;
        }
    if (st.shadow) add_shadow(o, ox, oy, g.w, g.h, core, st.dark);
}

// ¡ (! com o ponto em cima e a barra embaixo) ou ¿ (? girado 180 graus).
void compose_flip(const Glyph &g, char base, Out &o, int ox, int oy) {
    const Extent e = extent_of(g);
    if (!e.ok()) return;
    auto copy_row = [&](int sy, int dy, bool mirror) {
        for (int x = 0; x < g.w; x++) {
            const int sx = mirror ? e.left + e.right - x : x;
            if (sx < 0 || sx >= g.w || !g.opaque(sx, sy)) continue;
            o.set(ox + x, oy + dy, g.at(sx, sy), (static_cast<int32_t>(g.y0 + sy) << 16) | (g.x0 + sx));
        }
    };
    int g0 = e.top;
    while (g0 <= e.bottom && !row_empty(g, g0)) g0++;
    int g1 = g0;
    while (g1 <= e.bottom && row_empty(g, g1)) g1++;
    if (base == '!' && g0 <= e.bottom && g1 <= e.bottom) {
        const int len_b = e.bottom - g1 + 1, gap = g1 - g0;
        for (int y = g1; y <= e.bottom; y++) copy_row(y, e.top + (y - g1), false);
        for (int y = e.top; y < g0; y++) copy_row(y, y + len_b + gap, false);
        return;
    }
    for (int y = e.top; y <= e.bottom; y++) copy_row(y, e.top + e.bottom - y, true);
}

// Caractere chines: nucleo nas colunas 1..16. Na fonte de 18 linhas (g_font_medium) o glifo ocupa
// as linhas 1..16. Na de 26 (g_font_w18) as mesmas linhas ficam espalhadas de modo que a reducao
// dos itens do menu (stretch_sprite para 18 linhas: linha y <- y*26/18) devolva exatamente o glifo
// de 18 linhas; cada linha que a reducao pula repete uma vizinha (a com menos traco horizontal, isto
// e, pixels sem vizinho em cima e embaixo, para nao engrossar tracos finos).
void compose_cjk(const CjkGlyph &cg, int cell, int h, const Style &st, Out &o, int ox, int oy) {
    constexpr int kH = 18;
    uint16_t rows18[kH] = {};
    for (int j = 0; j < 16; j++) rows18[j + 1] = cg.rows[j];
    auto cost = [&](int y) {
        const uint16_t up = y > 0 ? rows18[y - 1] : 0, dn = y + 1 < kH ? rows18[y + 1] : 0;
        return popcount16(static_cast<uint16_t>(rows18[y] & ~(up & dn)));
    };
    std::vector<int> src(static_cast<size_t>(h), -1);  // linha de 18 de cada linha da celula
    if (h <= kH) {
        for (int y = 0; y < h; y++) src[y] = y;
    } else {
        for (int y = 0; y < kH; y++) src[static_cast<size_t>(y) * h / kH] = y;
        for (int r = 0; r < h; r++) {
            if (src[r] >= 0) continue;
            const int up = r > 0 ? src[r - 1] : -1, dn = r + 1 < h ? src[r + 1] : -1;
            src[r] = (up < 0 || (dn >= 0 && cost(dn) < cost(up))) ? dn : up;
        }
    }
    std::vector<uint8_t> core(static_cast<size_t>(cell) * h, 0);
    for (int y = 0; y < h; y++) {
        const uint16_t bits = src[y] >= 0 ? rows18[src[y]] : 0;
        for (int c = 0; c < 16 && c + 1 < cell; c++) {
            if (!((bits >> (15 - c)) & 1)) continue;
            o.set(ox + 1 + c, oy + y, st.grad[y], -2);
            core[static_cast<size_t>(y) * cell + 1 + c] = 1;
        }
    }
    if (st.shadow) add_shadow(o, ox, oy, cell, h, core, st.dark);
}

void cell_origin(int i, int cell, int h, int *x, int *y) {
    *x = (i % kCols) * cell;
    *y = (i / kCols) * h;
}

// Diagnostico: HOH_LANG_DUMP=pasta grava cada faixa base (lang_<celula>x<altura>.bmp).
void dump_strip(BITMAP *strip, int cell, int h) {
    const char *dir = std::getenv("HOH_LANG_DUMP");
    if (!dir || !*dir) return;
    char path[1024];
    std::snprintf(path, sizeof path, "%s/lang_%dx%d.bmp", dir, cell, h);
    platform_save_bitmap(strip, path);
}

BITMAP *new_strip(int cells, int cell, int h) {
    const int n = std::max(1, cells);
    BITMAP *b = create_bitmap(std::min(n, kCols) * cell, ((n + kCols - 1) / kCols) * h);
    clear_to_color(b, g_mask_color);
    g_owned.push_back(b);
    return b;
}

}  // namespace

void lang_glyphs_build(BITMAP *font, int cell, bool mixed, bool cjk) {
    if (!font || cell <= 0) return;
    auto layout = std::make_shared<Layout>();
    const int def = mixed ? glyph_index_mixed('\x01') : glyph_index_upper('\x01');
    for (uint32_t cp : lang_codepoints()) {
        char base = 0;
        Mark mark = M_ACUTE;
        bool ok = false;
        if (latin_of(cp, &base, &mark)) {
            if (!mixed && base >= 'a' && base <= 'z') base = static_cast<char>(base - 0x20);
            ok = (mixed ? glyph_index_mixed(base) : glyph_index_upper(base)) != def;
        } else if (cjk) {
            ok = cjk_of(cp) != nullptr;
        }
        if (!ok) continue;
        layout->index[cp] = static_cast<int>(layout->cps.size());
        layout->cps.push_back(cp);
    }
    const int h = font->h;
    Out o{new_strip(static_cast<int>(layout->cps.size()), cell, h), {}};
    o.prov.assign(static_cast<size_t>(o.strip->w) * o.strip->h, -1);
    const Style st = style_of(font, cell, mixed);
    for (int i = 0; i < static_cast<int>(layout->cps.size()); i++) {
        const uint32_t cp = layout->cps[i];
        int ox = 0, oy = 0;
        cell_origin(i, cell, h, &ox, &oy);
        char base = 0;
        Mark mark = M_ACUTE;
        if (latin_of(cp, &base, &mark)) {
            if (!mixed && base >= 'a' && base <= 'z') base = static_cast<char>(base - 0x20);
            const int gi = mixed ? glyph_index_mixed(base) : glyph_index_upper(base);
            const Glyph g{font, gi * cell, 0, cell, h};
            if (mark == M_FLIP) compose_flip(g, base, o, ox, oy);
            else compose_latin(g, base, mark, mixed, st, o, ox, oy);
        } else if (const CjkGlyph *cg = cjk_of(cp)) {
            compose_cjk(*cg, cell, h, st, o, ox, oy);
        }
    }
    g_base_prov[o.strip] = o.prov;
    dump_strip(o.strip, cell, h);
    hd::tag_derived(o.strip, hd::asset_of(font), std::move(o.prov));
    g_fonts[font] = XFont{o.strip, cell, h, layout};
}

void lang_glyphs_build_stretched(BITMAP *font, BITMAP *from) {
    auto it = g_fonts.find(from);
    if (!font || it == g_fonts.end()) return;
    const XFont xf = it->second;
    const BITMAP *src = xf.strip;
    const std::vector<int32_t> &sprov = g_base_prov[src];
    const int h = font->h, bands = src->h / xf.h;
    Out o{new_strip(static_cast<int>(xf.layout->cps.size()), xf.cell, h), {}};
    o.prov.assign(static_cast<size_t>(o.strip->w) * o.strip->h, -1);
    // letras: o mesmo mapeamento de linhas do stretch_sprite que fez a fonte
    for (int b = 0; b < bands; b++)
        for (int y = 0; y < h; y++) {
            const int sy = b * xf.h + static_cast<int>(static_cast<int64_t>(y) * xf.h / h);
            for (int x = 0; x < src->w; x++) {
                const uint16_t c = src->line[sy][x];
                if (c == mask16()) continue;
                const size_t at = static_cast<size_t>(sy) * src->w + x;
                o.set(x, b * h + y, c, at < sprov.size() ? sprov[at] : -2);
            }
        }
    // chines: refeito na altura desta fonte
    const Style st = style_of(font, xf.cell, false);
    for (int i = 0; i < static_cast<int>(xf.layout->cps.size()); i++) {
        const CjkGlyph *cg = cjk_of(xf.layout->cps[i]);
        if (!cg) continue;
        int ox = 0, oy = 0;
        cell_origin(i, xf.cell, h, &ox, &oy);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < xf.cell; x++) o.set(ox + x, oy + y, mask16(), -1);
        compose_cjk(*cg, xf.cell, h, st, o, ox, oy);
    }
    g_base_prov[o.strip] = o.prov;
    dump_strip(o.strip, xf.cell, h);
    hd::tag_derived(o.strip, hd::asset_of(from), std::move(o.prov));
    g_fonts[font] = XFont{o.strip, xf.cell, h, xf.layout};
}

void lang_glyphs_on_tint(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct) {
    static bool busy = false;  // o tint_bitmap da propria faixa
    if (busy || !src || !dst) return;
    auto it = g_fonts.find(src);
    if (it == g_fonts.end()) {
        g_fonts.erase(dst);  // dst deixou de ser uma fonte com faixa extra
        return;
    }
    XFont xf = it->second;  // copia: dst pode ser src
    BITMAP *&t = g_tint_cache[std::make_tuple(static_cast<const BITMAP *>(xf.strip), hue, sat, lum, boost_pct)];
    if (!t) {
        t = create_bitmap(xf.strip->w, xf.strip->h);
        g_owned.push_back(t);
        busy = true;
        tint_bitmap(xf.strip, t, hue, sat, lum, boost_pct, -1, 0, 0, 0, 0, 0, 1);
        busy = false;
    }
    xf.strip = t;
    g_fonts[dst] = xf;
}

bool lang_draw_glyph(BITMAP *dst, BITMAP *font, uint32_t cp, int x, int y) {
    auto it = g_fonts.find(font);
    if (it == g_fonts.end()) return false;
    const XFont &xf = it->second;
    auto ci = xf.layout->index.find(cp);
    if (ci == xf.layout->index.end()) return false;
    int sx = 0, sy = 0;
    cell_origin(ci->second, xf.cell, xf.h, &sx, &sy);
    masked_blit(xf.strip, dst, sx, sy, x, y, xf.cell, xf.h);
    return true;
}

void lang_glyphs_free() {
    for (BITMAP *b : g_owned) destroy_bitmap(b);
    g_owned.clear();
    g_fonts.clear();
    g_base_prov.clear();
    g_tint_cache.clear();
}

}  // namespace hoh

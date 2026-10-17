// Renderizador HD, parte sem SDL: etiquetas de origem, gravacao da lista do quadro e verificacao 1x
// (ver hd_render.h). O desenho na GPU fica em hd_render.cpp.
#include "compat/hd_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hd {

namespace detail {
std::vector<Asset> g_assets(1);
std::vector<Tag> g_tags(1);
void (*g_tag_released)(int tag) = nullptr;
}  // namespace detail

using namespace detail;

namespace {

std::vector<int> g_free_tags;
uint32_t g_serial = 0;
bool g_open = false;
int g_suppress = 0;
int g_nest = 0;

uint64_t fnv(const void *p, size_t n, uint64_t h = 1469598103934665603ull) {
    const auto *c = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
    return h;
}

void release_tag(BITMAP *bmp) {
    if (!bmp->hd_tag) return;
    const int id = bmp->hd_tag;
    g_tags[id] = Tag();
    g_free_tags.push_back(id);
    if (g_tag_released) g_tag_released(id);
    bmp->hd_tag = 0;
}

void new_tag(BITMAP *bmp, Tag t) {
    release_tag(bmp);
    t.used = true;
    t.self = bmp;
    int id;
    if (!g_free_tags.empty()) {
        id = g_free_tags.back();
        g_free_tags.pop_back();
        g_tags[id] = std::move(t);
    } else {
        id = static_cast<int>(g_tags.size());
        g_tags.push_back(std::move(t));
    }
    bmp->hd_tag = id;
}

inline uint16_t scale565(uint32_t p, uint32_t f) {
    p = ((((p & 0x7e0) << 16) | (p & 0x7e0f81f)) * f >> 5) & 0x7e0f81f;
    return static_cast<uint16_t>((p >> 16) | p);
}

}  // namespace

bool detail::live(const List *l) { return l && g_open && l->serial == g_serial && l->serial != 0; }

// ================================================================== etiquetas
void register_asset(BITMAP *bmp, const std::string &name) {
    if (!bmp || bmp->hd_tag) return;  // a mesma imagem pode aparecer duas vezes: vale a 1a
    g_assets.push_back({name, bmp});
    Tag t;
    t.asset = static_cast<int>(g_assets.size()) - 1;
    new_tag(bmp, std::move(t));
}

int asset_of(const BITMAP *bmp) {
    if (!bmp || !bmp->hd_tag) return 0;
    const Tag &t = g_tags[bmp->hd_tag];
    return (t.used && !t.flip && !t.gray && t.prov.empty()) ? t.asset : 0;
}

void tag_flip_copy(BITMAP *dst, const BITMAP *src) {
    if (!dst || !src || !src->hd_tag || dst->w != src->w || dst->h != src->h) return;
    Tag t = g_tags[src->hd_tag];
    t.flip = !t.flip;
    new_tag(dst, std::move(t));
}

void tag_gray_copy(BITMAP *dst, const BITMAP *src) {
    if (!dst || !src || !src->hd_tag || dst->w != src->w || dst->h != src->h) return;
    Tag t = g_tags[src->hd_tag];
    t.gray = true;
    new_tag(dst, std::move(t));
}

void tag_derived(BITMAP *dst, int asset, std::vector<int32_t> prov) {
    if (!dst || asset <= 0 || prov.size() != static_cast<size_t>(dst->w) * dst->h) return;
    Tag t;
    t.asset = asset;
    t.prov = std::move(prov);
    new_tag(dst, std::move(t));
}

void forget(BITMAP *bmp) {
    if (!bmp) return;
    release_tag(bmp);
    delete list_of(bmp);
    bmp->hd_list = nullptr;
}

// ================================================================== gravacao
Suppress::Suppress() { g_suppress++; }
Suppress::~Suppress() { g_suppress--; }
Nest::Nest() : outer(g_nest == 0) { g_nest++; }
Nest::~Nest() { g_nest--; }

void frame_begin(BITMAP *target) {
    if (!target) return;
    List *l = list_of(target);
    if (!l) {
        l = new List();
        target->hd_list = l;
    }
    g_serial++;
    if (g_serial == 0) g_serial = 1;
    g_open = true;
    l->serial = g_serial;
    l->ops.clear();
    l->snaps.clear();
}

void frame_done() { g_open = false; }

bool recording(const BITMAP *dst) { return dst && !g_suppress && live(list_of(dst)); }

bool touch(const BITMAP *dst) {
    List *l = dst ? list_of(dst) : nullptr;
    if (!l || g_suppress) return false;
    if (live(l)) return true;
    l->serial = 0;  // desenho fora do quadro: a lista nao vale mais
    return false;
}

void op_sprite(BITMAP *dst, BITMAP *src, int sx, int sy, int w, int h, int dx, int dy, int flags, int tint, int alpha) {
    List *l = list_of(dst);
    if (!l || !src) return;
    // recorte so contra a origem (o destino a GPU recorta)
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (w <= 0 || h <= 0) return;
    Op op;
    op.kind = OP_SPRITE;
    op.flags = static_cast<uint8_t>(flags);
    op.tint = static_cast<uint8_t>(std::clamp(tint, 0, kFull));
    op.alpha = static_cast<uint8_t>(std::clamp(alpha, 0, kFull));
    op.sx = sx; op.sy = sy; op.w = w; op.h = h; op.dx = dx; op.dy = dy;
    if (src->hd_tag && g_tags[src->hd_tag].used && !(flags & OPAQUE)) {
        op.tag = src->hd_tag;
    } else {  // copia dos pixels 1x de agora (o bitmap pode mudar antes de apresentar)
        Snap s;
        s.w = w;
        s.h = h;
        s.px.resize(static_cast<size_t>(w) * h);
        for (int j = 0; j < h; j++) std::memcpy(&s.px[static_cast<size_t>(j) * w], src->line[sy + j] + sx, w * sizeof(uint16_t));
        s.hash = fnv(&w, sizeof w, fnv(&h, sizeof h, fnv(s.px.data(), s.px.size() * 2)));
        op.snap = static_cast<int>(l->snaps.size());
        op.sx = op.sy = 0;
        l->snaps.push_back(std::move(s));
    }
    l->ops.push_back(op);
}

void op_fill(BITMAP *dst, int x1, int y1, int x2, int y2, int color) {
    List *l = list_of(dst);
    if (!l) return;
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    Op op;
    op.kind = OP_FILL;
    op.dx = x1; op.dy = y1; op.w = x2 - x1 + 1; op.h = y2 - y1 + 1;
    op.color = static_cast<uint16_t>(color);
    l->ops.push_back(op);
}

void op_clear(BITMAP *dst, int color) {
    List *l = list_of(dst);
    if (!l) return;
    l->ops.clear();  // o quadro inteiro e coberto: o que havia antes nao importa
    l->snaps.clear();
    op_fill(dst, 0, 0, dst->w - 1, dst->h - 1, color);
}

void list_copy(const BITMAP *from, BITMAP *to) {
    if (!from || !to || from == to) return;
    const List *src = list_of(from);
    List *l = list_of(to);
    if (!live(src)) {
        if (l) l->serial = 0;
        return;
    }
    if (!l) {
        l = new List();
        to->hd_list = l;
    }
    *l = *src;
}

void group_begin(BITMAP *dst, int alpha) {
    List *l = list_of(dst);
    if (!l) return;
    Op op;
    op.kind = OP_GROUP_BEGIN;
    op.alpha = static_cast<uint8_t>(std::clamp(alpha, 0, kFull));
    l->ops.push_back(op);
}

void group_end(BITMAP *dst) {
    List *l = list_of(dst);
    if (!l) return;
    Op op;
    op.kind = OP_GROUP_END;
    l->ops.push_back(op);
}

// ================================================================== verificacao 1x
// Refaz a lista com as imagens 1x (os bitmaps etiquetados ou as copias gravadas) e compara com o
// quadro de verdade: pixel diferente = desenho que nao foi gravado (ou ordem de objetos diferente).
void verify(const BITMAP *bmp) {
    static const char *path = std::getenv("HOH_HD_VERIFY");
    static int frames = 0;
    if (!path || !bmp || ++frames % 30 != 0) return;
    size_t ops = 0, snaps = 0;
    const int diff = verify_diff(bmp, &ops, &snaps);
    if (diff < 0) return;
    if (FILE *f = std::fopen(path, "a")) {
        std::fprintf(f, "[hd] ops %zu  copias %zu  pixels diferentes %d (%.2f%%)\n", ops, snaps, diff,
                     100.0 * diff / (bmp->w * bmp->h));
        std::fclose(f);
    }
}

int verify_diff(const BITMAP *bmp, size_t *ops_out, size_t *snaps_out, BITMAP *replay) {
    const List *l = bmp ? list_of(bmp) : nullptr;
    if (!l) return -1;
    if (ops_out) *ops_out = l->ops.size();
    if (snaps_out) *snaps_out = l->snaps.size();
    BITMAP *out = create_bitmap(bmp->w, bmp->h);
    BITMAP *mask = nullptr;
    int group_alpha = kFull;
    for (const Op &op : l->ops) {
        if (op.kind == OP_FILL) {
            rectfill(out, op.dx, op.dy, op.dx + op.w - 1, op.dy + op.h - 1, op.color);
        } else if (op.kind == OP_GROUP_BEGIN) {
            mask = create_bitmap(bmp->w, bmp->h);
            clear_to_color(mask, MASK_COLOR_16);
            group_alpha = op.alpha;
        } else if (op.kind == OP_GROUP_END) {
            if (!mask) continue;
            for (int y = 0; y < out->h; y++)
                for (int x = 0; x < out->w; x++)
                    if (mask->line[y][x] != MASK_COLOR_16) out->line[y][x] = scale565(out->line[y][x], kFull - group_alpha);
            destroy_bitmap(mask);
            mask = nullptr;
        } else {
            BITMAP *src = create_bitmap(op.w, op.h);
            if (op.tag) {
                blit(g_tags[op.tag].self, src, op.sx, op.sy, 0, 0, op.w, op.h);
            } else {
                const Snap &s = l->snaps[op.snap];
                std::memcpy(src->dat, s.px.data(), s.px.size() * 2);
            }
            BITMAP *dst = mask ? mask : out;
            const bool flip = (op.flags & HFLIP) != 0;
            for (int j = 0; j < op.h; j++) {
                const int y = op.dy + j;
                if (y < 0 || y >= dst->h) continue;
                for (int i = 0; i < op.w; i++) {
                    const int x = op.dx + i;
                    if (x < 0 || x >= dst->w) continue;
                    const uint16_t p = src->line[j][flip ? op.w - 1 - i : i];
                    if (p == MASK_COLOR_16 && !(op.flags & OPAQUE)) continue;
                    uint16_t &d = dst->line[y][x];
                    if (mask) {
                        d = 0;
                    } else if (op.flags & ADD) {
                        const int f = op.alpha;
                        const int sr = ((p >> 11) * f) >> 5, sg = (((p >> 6) & 31) * f) >> 5, sb = ((p & 31) * f) >> 5;
                        const int g = std::min(31, ((d >> 6) & 31) + sg);
                        d = static_cast<uint16_t>((std::min(31, (d >> 11) + sr) << 11) | (g << 6) | std::min(31, (d & 31) + sb));
                    } else if (op.alpha < kFull) {
                        const int f = op.alpha;
                        if (f == 16) {
                            d = static_cast<uint16_t>(((d >> 1) & 0x7BEF) + ((p >> 1) & 0x7BEF) + (d & p & 0x0821));
                        } else {
                            auto mix = [f](int a, int b) { return b + (((a - b) * f) >> 5); };
                            d = static_cast<uint16_t>((mix(p >> 11, d >> 11) << 11) | (mix((p >> 5) & 63, (d >> 5) & 63) << 5) |
                                                      mix(p & 31, d & 31));
                        }
                    } else if (op.tint < kFull) {
                        d = scale565(p, op.tint);
                    } else {
                        d = p;
                    }
                }
            }
            destroy_bitmap(src);
        }
    }
    int diff = 0;
    for (int y = 0; y < bmp->h; y++)
        for (int x = 0; x < bmp->w; x++)
            if (out->line[y][x] != bmp->line[y][x]) diff++;
    if (replay) blit(out, replay, 0, 0, 0, 0, out->w, out->h);
    destroy_bitmap(out);
    return diff;
}

}  // namespace hd

// Renderizador HD, parte sem SDL: etiquetas de origem, gravacao das listas e verificacao 1x
// (ver hd_render.h). O desenho na GPU fica em hd_render.cpp.
#include "compat/hd_internal.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>

namespace hd {

namespace detail {
std::vector<Asset> g_assets(1);
std::vector<Tag> g_tags(1);
}  // namespace detail

using namespace detail;

namespace {

bool g_enabled = false;
int g_suppress = 0;
int g_nest = 0;
int g_label = -1;  // estado do jogo do quadro atual (verificacao)
uint16_t (*g_tint_px)(uint16_t, const Xform &) = nullptr;
void (*g_tint_rgba)(uint8_t *, size_t, const Xform &) = nullptr;
std::map<std::vector<int32_t>, int> g_derived;  // (etiqueta, transformacoes) -> etiqueta derivada
// Lista maior que isso: desiste dela (o bitmap volta a ser copiado em 1x ate a proxima ancora).
constexpr size_t kMaxOps = 32768;
constexpr uint16_t kMask = MASK_COLOR_16;

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};
Rect meet(const Rect &a, const Rect &b) {
    return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}
bool covers(const Rect &outer, const Rect &in) {
    return outer.x0 <= in.x0 && outer.y0 <= in.y0 && outer.x1 >= in.x1 && outer.y1 >= in.y1;
}
bool disjoint(const Rect &a, const Rect &b) { return meet(a, b).empty(); }
Rect clip_of(const Op &o) { return {o.cx0, o.cy0, o.cx1, o.cy1}; }
void set_clip(Op &o, const Rect &r) {
    o.cx0 = static_cast<int16_t>(r.x0);
    o.cy0 = static_cast<int16_t>(r.y0);
    o.cx1 = static_cast<int16_t>(r.x1);
    o.cy1 = static_cast<int16_t>(r.y1);
}
Rect bounds_of(const BITMAP *b) { return {0, 0, b->w, b->h}; }
// recorte do bitmap (set_clip) dentro dos limites
Rect clip_rect(const BITMAP *b) { return b->clip ? meet({b->cl, b->ct, b->cr, b->cb}, bounds_of(b)) : bounds_of(b); }
// e - r: ate 4 retangulos
std::vector<Rect> subtract(const Rect &e, const Rect &r) {
    const Rect m = meet(e, r);
    if (m.empty()) return {e};
    std::vector<Rect> out;
    const Rect parts[4] = {{e.x0, e.y0, e.x1, m.y0}, {e.x0, m.y1, e.x1, e.y1}, {e.x0, m.y0, m.x0, m.y1}, {m.x1, m.y0, e.x1, m.y1}};
    for (const Rect &p : parts)
        if (!p.empty()) out.push_back(p);
    return out;
}

inline uint64_t splitmix(uint64_t z) {
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}
inline uint64_t mix(uint64_t h, uint64_t v) { return splitmix(h ^ splitmix(v)); }

uint64_t hash_px(const uint16_t *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull ^ n;
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        uint64_t w;
        std::memcpy(&w, p + i, sizeof w);
        h = (h ^ w) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 29;
    }
    for (; i < n; i++) {
        h = (h ^ p[i]) * 0x9e3779b97f4a7c15ull;
        h ^= h >> 29;
    }
    return splitmix(h);
}

inline uint16_t scale565(uint32_t p, uint32_t f) {
    p = ((((p & 0x7e0) << 16) | (p & 0x7e0f81f)) * f >> 5) & 0x7e0f81f;
    return static_cast<uint16_t>((p >> 16) | p);
}

// ------------------------------------------------------------------ transformacoes de pixel (1x)
uint16_t xform_px(const Xform &x, uint16_t c) {
    if (c == kMask) return c;  // magenta continua transparente (como nas rotinas do jogo)
    switch (x.kind) {
        case XF_SCALE: return scale565(c, static_cast<uint32_t>(std::clamp<int>(x.a, 0, kFull)));
        case XF_GRAY: {
            const int v = (getb(c) + getg(c) + getr(c)) / 4;  // create_grayscale_bitmap
            return static_cast<uint16_t>(makecol(v, v, v));
        }
        case XF_SILHOUETTE: return 0;  // make_silhouette
        case XF_TINT: return g_tint_px ? g_tint_px(c, x) : c;
        default: return c;
    }
}
uint16_t xform_chain(const std::vector<Xform> &xf, uint16_t c) {
    for (const Xform &x : xf) c = xform_px(x, c);
    return c;
}

bool any_mask(const std::vector<uint16_t> &px) { return std::find(px.begin(), px.end(), kMask) != px.end(); }
Pixels copy_px(const BITMAP *b) {
    return std::make_shared<const std::vector<uint16_t>>(b->dat, b->dat + static_cast<size_t>(b->w) * b->h);
}
bool same_px(const Tag &t, const BITMAP *b) {
    return t.w == b->w && t.h == b->h && std::memcmp(t.px->data(), b->dat, t.px->size() * 2) == 0;
}

// ------------------------------------------------------------------ etiquetas
int new_tag(Tag t) {
    g_tags.push_back(std::move(t));
    return static_cast<int>(g_tags.size()) - 1;
}
Rec *ensure_rec(BITMAP *b) {
    if (!b->hd_list) b->hd_list = new Rec();
    return rec_of(b);
}
void set_tag(BITMAP *b, int id) {
    b->hd_tag = id;
    if (Rec *r = rec_of(b)) r->cur.reset();  // a etiqueta descreve o bitmap inteiro
}
// Etiqueta base + transformacoes (sem duplicar); actual = o bitmap que ficou assim (pixels exatos).
int derive(int base, const std::vector<Xform> &chain, const BITMAP *actual = nullptr) {
    if (chain.empty() || base <= 0) return base;
    std::vector<int32_t> key{base};
    for (const Xform &x : chain) {
        key.push_back(x.kind | (x.bgr << 8));
        key.push_back(x.a);
        key.push_back(x.b);
        key.push_back(x.c);
        key.push_back(x.d);
    }
    auto it = g_derived.find(key);
    if (it != g_derived.end() && (!actual || same_px(g_tags[it->second], actual))) return it->second;
    Tag t = g_tags[base];
    t.xf.insert(t.xf.end(), chain.begin(), chain.end());
    if (actual && actual->w == t.w && actual->h == t.h) {
        t.px = copy_px(actual);
    } else {
        auto v = std::make_shared<std::vector<uint16_t>>(*t.px);
        for (uint16_t &c : *v) c = xform_chain(chain, c);
        t.px = v;
    }
    t.has_mask = any_mask(*t.px);
    const int id = new_tag(std::move(t));
    if (it == g_derived.end()) g_derived[key] = id;
    return id;
}

// ------------------------------------------------------------------ listas
std::shared_ptr<ListData> fresh(Rec *r, int w, int h) {
    std::shared_ptr<ListData> l;
    if (r->cur && r->cur.use_count() == 1) {  // ninguem mais ve esta versao: reaproveita
        l = r->cur;
        l->ops.clear();
        l->snaps.clear();
        l->subs.clear();
    } else {
        l = std::make_shared<ListData>();
    }
    l->w = w;
    l->h = h;
    l->hash = mix(static_cast<uint64_t>(w), static_cast<uint64_t>(h));
    l->opaque = false;
    l->inl = l->disj = -1;
    r->cur = l;
    return l;
}
ListData *mut(Rec *r) {
    if (r->cur.use_count() > 1) r->cur = std::make_shared<ListData>(*r->cur);  // copy-on-write
    r->cur->inl = r->cur->disj = -1;
    return r->cur.get();
}
uint64_t op_hash(const ListData &l, const Op &o) {
    uint64_t h = mix(static_cast<uint64_t>(o.kind) | (static_cast<uint64_t>(o.flags) << 8) |
                         (static_cast<uint64_t>(o.tint) << 16) | (static_cast<uint64_t>(o.alpha) << 24) |
                         (static_cast<uint64_t>(o.color) << 32),
                     static_cast<uint64_t>(o.tag));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(o.sx)) << 32) | static_cast<uint32_t>(o.sy));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(o.sw)) << 32) | static_cast<uint32_t>(o.sh));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(o.dx)) << 32) | static_cast<uint32_t>(o.dy));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint32_t>(o.dw)) << 32) | static_cast<uint32_t>(o.dh));
    h = mix(h, (static_cast<uint64_t>(static_cast<uint16_t>(o.cx0)) << 48) | (static_cast<uint64_t>(static_cast<uint16_t>(o.cy0)) << 32) |
                   (static_cast<uint64_t>(static_cast<uint16_t>(o.cx1)) << 16) | static_cast<uint16_t>(o.cy1));
    if (o.snap >= 0) h = mix(h, l.snaps[o.snap]->hash);
    if (o.sub >= 0) h = mix(h, l.subs[o.sub]->hash);
    return h;
}
void push(ListData *l, const Op &o) {
    l->ops.push_back(o);
    l->hash = mix(l->hash, op_hash(*l, o));
}
// Copia ops de outras listas para l, reaproveitando os indices de copias/listas ja trazidas.
struct Builder {
    ListData *l;
    std::unordered_map<const void *, int> snaps, subs;
    explicit Builder(ListData *dst) : l(dst) {
        for (size_t i = 0; i < l->snaps.size(); i++) snaps[l->snaps[i].get()] = static_cast<int>(i);
        for (size_t i = 0; i < l->subs.size(); i++) subs[l->subs[i].get()] = static_cast<int>(i);
    }
    int snap(const std::shared_ptr<const Snap> &s) {
        auto it = snaps.find(s.get());
        if (it != snaps.end()) return it->second;
        l->snaps.push_back(s);
        return snaps[s.get()] = static_cast<int>(l->snaps.size()) - 1;
    }
    int sub(const std::shared_ptr<const ListData> &s) {
        auto it = subs.find(s.get());
        if (it != subs.end()) return it->second;
        l->subs.push_back(s);
        return subs[s.get()] = static_cast<int>(l->subs.size()) - 1;
    }
    void copy(Op q, const ListData &from) {
        if (q.snap >= 0) q.snap = snap(from.snaps[q.snap]);
        if (q.sub >= 0) q.sub = sub(from.subs[q.sub]);
        push(l, q);
    }
};

void anchor_tag(BITMAP *b, Rec *r, int tag) {  // a lista comeca com a imagem da etiqueta
    ListData *l = fresh(r, b->w, b->h).get();
    Op o;
    o.flags = OPAQUE;
    o.tag = tag;
    o.sw = o.dw = b->w;
    o.sh = o.dh = b->h;
    set_clip(o, bounds_of(b));
    push(l, o);
    l->opaque = !g_tags[tag].has_mask;
}

struct Source {
    int tag = 0;
    std::shared_ptr<const Snap> snap;
    std::shared_ptr<const ListData> sub;
    bool opaque = false;  // nenhum pixel magenta
};

std::shared_ptr<Snap> make_snap(const BITMAP *src, int sx, int sy, int w, int h) {
    auto s = std::make_shared<Snap>();
    s->w = w;
    s->h = h;
    s->px.resize(static_cast<size_t>(w) * h);
    for (int j = 0; j < h; j++) std::memcpy(&s->px[static_cast<size_t>(j) * w], src->line[sy + j] + sx, w * sizeof(uint16_t));
    s->has_mask = any_mask(s->px);
    if (const Rec *r = rec_of(src)) {
        if (r->base && !sx && !sy && w == src->w && h == src->h) {
            const Tag &t = g_tags[r->base];
            if (t.w == w && t.h == h && !t.stretch_w) s->base = r->base;
        }
    }
    s->hash = mix(hash_px(s->px.data(), s->px.size()), (static_cast<uint64_t>(w) << 40) ^ (static_cast<uint64_t>(h) << 20) ^ s->base);
    return s;
}

void check_capture(const BITMAP *b, const ListData &l);

Source source_of(BITMAP *src, int sx, int sy, int w, int h) {
    Source s;
    if (src->hd_tag) {
        s.tag = src->hd_tag;
        s.opaque = !g_tags[s.tag].has_mask;
    } else if (const ListData *l = list_of(src)) {
        s.sub = rec_of(src)->cur;
        s.opaque = l->opaque;
        check_capture(src, *l);
    } else {
        s.snap = make_snap(src, sx, sy, w, h);
        s.opaque = !s.snap->has_mask;
    }
    return s;
}
bool source_opaque(const ListData &l, const Op &o) {
    if (o.tag) return !g_tags[o.tag].has_mask;
    if (o.snap >= 0) return !l.snaps[o.snap]->has_mask;
    if (o.sub >= 0) return l.subs[o.sub]->opaque;
    return false;
}

// Da para embutir os ops de s no destino (mesmo resultado que desenhar s inteiro)? So desenhos
// que substituem o pixel (sem soma, alfa ou escurecer no lugar), sobre o magenta da ancora.
bool inlinable(const ListData &s) {
    if (s.ops.empty() || s.ops.size() > 2048) return false;
    for (size_t i = 0; i < s.ops.size(); i++) {
        const Op &o = s.ops[i];
        if (o.kind == OP_FILL) {
            if ((o.color == kMask && i != 0) || o.alpha != kFull) return false;
            continue;
        }
        if (o.kind != OP_SPRITE || o.alpha != kFull || (o.flags & ADD)) return false;
        if ((o.flags & OPAQUE) && !source_opaque(s, o)) return false;  // furaria a imagem
    }
    return true;
}
bool ops_disjoint(const ListData &s) {  // para embutir com alfa: nenhum pixel misturado duas vezes
    std::vector<Rect> rs;
    for (size_t i = 0; i < s.ops.size(); i++) {
        const Op &o = s.ops[i];
        if (i == 0 && o.kind == OP_FILL && o.color == kMask) continue;
        const Rect r = clip_of(o);
        if (r.empty()) continue;
        for (const Rect &q : rs)
            if (!disjoint(q, r)) return false;
        rs.push_back(r);
        if (rs.size() > 96) return false;
    }
    return true;
}
bool can_inline(const ListData &s, const Op &op) {
    if (op.dw != op.sw || op.dh != op.sh || (op.flags & (ADD | HFLIP)) || op.tint != kFull) return false;
    if ((op.flags & OPAQUE) && !s.opaque) return false;
    if (s.inl < 0) s.inl = inlinable(s) ? 1 : 0;
    if (!s.inl) return false;
    if (op.alpha < kFull) {
        if (s.disj < 0) s.disj = ops_disjoint(s) ? 1 : 0;
        if (!s.disj) return false;
    }
    return true;
}
void inline_ops(ListData *l, const ListData &s, const Op &op, const Rect &area) {
    const int ox = op.dx - op.sx, oy = op.dy - op.sy;
    Builder b(l);
    for (size_t i = 0; i < s.ops.size(); i++) {
        Op q = s.ops[i];
        if (i == 0 && q.kind == OP_FILL && q.color == kMask) continue;  // fundo transparente
        q.dx += ox;
        q.dy += oy;
        const Rect c = meet({q.cx0 + ox, q.cy0 + oy, q.cx1 + ox, q.cy1 + oy}, area);
        if (c.empty()) continue;
        set_clip(q, c);
        if (op.alpha < kFull) q.alpha = op.alpha;
        b.copy(q, s);
    }
}

void record(BITMAP *dst, Rec *r, Source src, Op op) {
    const Rect bounds = bounds_of(dst);
    const Rect area = meet(meet({op.dx, op.dy, op.dx + op.dw, op.dy + op.dh}, clip_rect(dst)), bounds);
    if (area.empty()) return;
    set_clip(op, area);
    const bool full = !(op.flags & ADD) && op.alpha == kFull && covers(area, bounds) && ((op.flags & OPAQUE) || src.opaque);
    ListData *l;
    if (full) {  // ancora: o que havia antes nao importa
        l = fresh(r, dst->w, dst->h).get();
        l->opaque = true;
    } else {
        if (!r->cur) return;  // conteudo desconhecido
        l = mut(r);
    }
    if (src.sub && can_inline(*src.sub, op)) {
        inline_ops(l, *src.sub, op, area);
    } else {
        op.tag = src.tag;
        if (src.snap) {
            l->snaps.push_back(src.snap);
            op.snap = static_cast<int>(l->snaps.size()) - 1;
        }
        if (src.sub) {
            l->subs.push_back(src.sub);
            op.sub = static_cast<int>(l->subs.size()) - 1;
        }
        push(l, op);
    }
    if ((op.flags & OPAQUE) && !src.opaque) l->opaque = false;
    if (l->ops.size() > kMaxOps) r->cur.reset();
}

// Lista com as transformacoes aplicadas a cada desenho (dentro de rg, se dado). Nulo se algum
// desenho na regiao nao for do tipo que substitui o pixel (alfa, soma, escurecer no lugar).
std::shared_ptr<ListData> pushdown(const ListData &l, const std::vector<Xform> &chain, const Rect *rg) {
    auto out = std::make_shared<ListData>();
    out->w = l.w;
    out->h = l.h;
    out->hash = mix(static_cast<uint64_t>(l.w), static_cast<uint64_t>(l.h));
    out->opaque = l.opaque;
    Builder b(out.get());
    for (const Op &o : l.ops) {
        if (o.kind == OP_GROUP_BEGIN || o.kind == OP_GROUP_END) return nullptr;
        const Rect e = clip_of(o);
        if (rg && meet(e, *rg).empty()) {
            b.copy(o, l);
            continue;
        }
        if (o.kind == OP_SCALE) return nullptr;
        if (o.kind == OP_SPRITE && (o.alpha != kFull || (o.flags & ADD))) return nullptr;
        Op t = o;
        if (o.kind == OP_FILL) {
            if (o.color == kMask) {  // continua magenta: nada muda
                b.copy(o, l);
                continue;
            }
            t.color = xform_chain(chain, o.color);
        } else {
            std::vector<Xform> ch;
            if (o.tint < kFull) {
                Xform s;
                s.kind = XF_SCALE;
                s.a = o.tint;
                ch.push_back(s);
            }
            ch.insert(ch.end(), chain.begin(), chain.end());
            t.tint = kFull;
            if (o.tag) {
                t.tag = derive(o.tag, ch);
            } else if (o.snap >= 0) {
                auto ns = std::make_shared<Snap>(*l.snaps[o.snap]);
                for (uint16_t &c : ns->px) c = xform_chain(ch, c);
                ns->base = 0;
                ns->has_mask = any_mask(ns->px);
                ns->hash = mix(hash_px(ns->px.data(), ns->px.size()), (static_cast<uint64_t>(ns->w) << 40) ^ (static_cast<uint64_t>(ns->h) << 20));
                t.snap = b.snap(ns);
            } else if (o.sub >= 0) {
                auto ps = pushdown(*l.subs[o.sub], ch, nullptr);
                if (!ps) return nullptr;
                t.sub = b.sub(ps);
            }
        }
        if (rg && !covers(*rg, e)) {  // parte fora da regiao: o desenho original
            for (const Rect &f : subtract(e, *rg)) {
                Op q = o;
                set_clip(q, f);
                b.copy(q, l);
            }
            set_clip(t, meet(e, *rg));
        }
        push(out.get(), t);  // indices de t ja sao de out
    }
    return out;
}

const char *verify_path() {  // HOH_HD_VERIFY
    static const char *p = std::getenv("HOH_HD_VERIFY");
    return p;
}

}  // namespace

const ListData *detail::list_of(const BITMAP *b) {
    const Rec *r = b ? rec_of(b) : nullptr;
    return (r && r->tracked) ? r->cur.get() : nullptr;
}

void detail::xform_rgba(const Xform &x, uint8_t *px, size_t n) {
    switch (x.kind) {
        case XF_SCALE: {
            const int f = std::clamp<int>(x.a, 0, kFull);
            for (size_t i = 0; i < n; i++)
                for (int c = 0; c < 3; c++) px[i * 4 + c] = static_cast<uint8_t>(px[i * 4 + c] * f / kFull);
            break;
        }
        case XF_GRAY:
            for (size_t i = 0; i < n; i++) {
                uint8_t *p = px + i * 4;
                p[0] = p[1] = p[2] = static_cast<uint8_t>((p[0] + p[1] + p[2]) / 4);
            }
            break;
        case XF_SILHOUETTE:
            for (size_t i = 0; i < n; i++) px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 0;
            break;
        case XF_TINT:
            if (g_tint_rgba) g_tint_rgba(px, n, x);
            break;
        default:
            break;
    }
}

// ================================================================== liga/desliga
void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }
void set_tint_functions(uint16_t (*px)(uint16_t, const Xform &), void (*rgba)(uint8_t *, size_t, const Xform &)) {
    g_tint_px = px;
    g_tint_rgba = rgba;
}

// ================================================================== etiquetas
void register_asset(BITMAP *bmp, const std::string &name) {
    if (!g_enabled || !bmp || bmp->hd_tag) return;  // a mesma imagem pode aparecer duas vezes: vale a 1a
    Tag t;
    t.asset = static_cast<int>(g_assets.size());
    t.w = bmp->w;
    t.h = bmp->h;
    t.px = copy_px(bmp);
    t.has_mask = any_mask(*t.px);
    const int id = new_tag(std::move(t));
    g_assets.push_back({name, bmp->w, bmp->h, id});
    set_tag(bmp, id);
}

int asset_of(const BITMAP *bmp) {
    if (!bmp || !bmp->hd_tag) return 0;
    const Tag &t = g_tags[bmp->hd_tag];
    return t.plain() ? t.asset : 0;
}

void tag_flip_copy(BITMAP *dst, const BITMAP *src) {
    if (!g_enabled || !dst || !src || !src->hd_tag || dst->w != src->w || dst->h != src->h) return;
    Tag t = g_tags[src->hd_tag];
    t.flip = !t.flip;
    t.px = copy_px(dst);
    t.has_mask = any_mask(*t.px);
    set_tag(dst, new_tag(std::move(t)));
}

void tag_gray_copy(BITMAP *dst, const BITMAP *src) {
    if (!g_enabled || !dst || !src || !src->hd_tag || dst->w != src->w || dst->h != src->h) return;
    Xform x;
    x.kind = XF_GRAY;
    set_tag(dst, derive(src->hd_tag, {x}, dst));
}

void tag_derived(BITMAP *dst, int asset, std::vector<int32_t> prov) {
    if (!g_enabled || !dst || asset <= 0 || prov.size() != static_cast<size_t>(dst->w) * dst->h) return;
    Tag t;
    t.asset = asset;
    t.prov = std::move(prov);
    t.w = dst->w;
    t.h = dst->h;
    t.px = copy_px(dst);
    t.has_mask = any_mask(*t.px);
    set_tag(dst, new_tag(std::move(t)));
}

bool tag_stretch_copy(BITMAP *dst, const BITMAP *src) {
    if (!g_enabled || !dst || !src || !src->hd_tag || !g_tags[src->hd_tag].plain()) return false;
    for (int y = 0; y < dst->h; y++)  // o mesmo mapeamento de stretch_sprite
        for (int x = 0; x < dst->w; x++)
            if (dst->line[y][x] != src->line[static_cast<int64_t>(y) * src->h / dst->h][static_cast<int64_t>(x) * src->w / dst->w])
                return false;
    Tag t = g_tags[src->hd_tag];
    t.stretch_w = dst->w;
    t.stretch_h = dst->h;
    t.w = dst->w;
    t.h = dst->h;
    t.px = copy_px(dst);
    t.has_mask = any_mask(*t.px);
    set_tag(dst, new_tag(std::move(t)));
    return true;
}

void forget(BITMAP *bmp) {
    if (!bmp) return;
    bmp->hd_tag = 0;  // a etiqueta continua valida para as listas que a usam (tem os proprios pixels)
    delete rec_of(bmp);
    bmp->hd_list = nullptr;
}

// ================================================================== gravacao
Suppress::Suppress() { g_suppress++; }
Suppress::~Suppress() { g_suppress--; }
Nest::Nest() : outer(g_nest == 0) { g_nest++; }
Nest::~Nest() { g_nest--; }

void track(BITMAP *bmp) {
    if (!g_enabled || !bmp) return;
    ensure_rec(bmp)->tracked = true;
}

void frame_begin(BITMAP *target, int label) {
    track(target);
    if (label >= 0) g_label = label;
}

bool recording(const BITMAP *dst) { return g_enabled && !g_suppress && list_of(dst) != nullptr; }

bool touch(BITMAP *dst) {
    if (!g_enabled || g_suppress || !dst || (!dst->hd_list && !dst->hd_tag)) return false;
    Rec *r = rec_of(dst);
    if (dst->hd_tag) {  // desenho sobre uma imagem etiquetada: a etiqueta nao vale mais
        const int t = dst->hd_tag;
        dst->hd_tag = 0;
        if (r && r->tracked) anchor_tag(dst, r, t);
    }
    return r && r->tracked;
}

void op_sprite(BITMAP *dst, BITMAP *src, int sx, int sy, int w, int h, int dx, int dy, int flags, int tint, int alpha) {
    if (!g_enabled || !dst || !src) return;
    Rec *r = rec_of(dst);
    if (!r || !r->tracked) return;
    // recorte so contra a origem (o destino, record recorta)
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (w <= 0 || h <= 0) return;
    tint = std::clamp(tint, 0, kFull);
    alpha = std::clamp(alpha, 0, kFull);
    if (src == dst && sx == dx && sy == dy && !(flags & (OPAQUE | ADD | HFLIP)) && alpha == kFull) {
        // escurece o proprio bitmap (draw_sprite_lit(dst, dst)): o que for magenta fica
        if (!r->cur || tint >= kFull) return;
        const Rect area = meet({dx, dy, dx + w, dy + h}, bounds_of(dst));
        if (area.empty()) return;
        Op o;
        o.kind = OP_SCALE;
        o.tint = static_cast<uint8_t>(tint);
        o.dx = dx; o.dy = dy; o.dw = w; o.dh = h;
        set_clip(o, area);
        push(mut(r), o);
        return;
    }
    Source s = source_of(src, sx, sy, w, h);
    Op o;
    o.flags = static_cast<uint8_t>(flags);
    o.tint = static_cast<uint8_t>(tint);
    o.alpha = static_cast<uint8_t>(alpha);
    o.sx = s.snap ? 0 : sx;
    o.sy = s.snap ? 0 : sy;
    o.sw = o.dw = w;
    o.sh = o.dh = h;
    o.dx = dx;
    o.dy = dy;
    record(dst, r, std::move(s), o);
}

void op_stretch(BITMAP *dst, BITMAP *src, int dx, int dy, int dw, int dh, int flags) {
    if (!g_enabled || !dst || !src || dw <= 0 || dh <= 0 || src->w <= 0 || src->h <= 0) return;
    Rec *r = rec_of(dst);
    if (!r || !r->tracked) return;
    Source s = source_of(src, 0, 0, src->w, src->h);
    Op o;
    o.flags = static_cast<uint8_t>(flags);
    o.sw = src->w;
    o.sh = src->h;
    o.dx = dx;
    o.dy = dy;
    o.dw = dw;
    o.dh = dh;
    record(dst, r, std::move(s), o);
}

void op_fill(BITMAP *dst, int x1, int y1, int x2, int y2, int color) {
    if (!g_enabled || !dst) return;
    Rec *r = rec_of(dst);
    if (!r || !r->tracked) return;
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    const Rect area = meet({x1, y1, x2 + 1, y2 + 1}, clip_rect(dst));
    if (area.empty()) return;
    Op o;
    o.kind = OP_FILL;
    o.dx = x1; o.dy = y1; o.dw = x2 - x1 + 1; o.dh = y2 - y1 + 1;
    o.color = static_cast<uint16_t>(color);
    set_clip(o, area);
    ListData *l;
    if (covers(area, bounds_of(dst))) {
        l = fresh(r, dst->w, dst->h).get();
        l->opaque = o.color != kMask;
    } else {
        if (!r->cur) return;
        l = mut(r);
        if (o.color == kMask) l->opaque = false;
    }
    push(l, o);
    if (l->ops.size() > kMaxOps) r->cur.reset();
}

void op_clear(BITMAP *dst, int color) {
    if (!g_enabled || !dst) return;
    Rec *r = rec_of(dst);
    if (!r || !r->tracked) return;
    ListData *l = fresh(r, dst->w, dst->h).get();  // clear_to_color ignora o recorte
    Op o;
    o.kind = OP_FILL;
    o.dw = dst->w;
    o.dh = dst->h;
    o.color = static_cast<uint16_t>(color);
    set_clip(o, bounds_of(dst));
    push(l, o);
    l->opaque = o.color != kMask;
}

void on_blit(BITMAP *s, BITMAP *d, int sx, int sy, int dx, int dy, int w, int h) {
    if (!g_enabled || !s || !d) return;
    const bool whole = !sx && !sy && !dx && !dy && w == s->w && h == s->h && s->w == d->w && s->h == d->h;
    if (whole && !g_suppress) {
        if (const ListData *sl = list_of(s)) {  // quadro inteiro: a lista vai junto
            d->hd_tag = 0;
            Rec *dr = ensure_rec(d);
            dr->tracked = true;
            dr->cur = rec_of(s)->cur;
            check_capture(s, *sl);
            return;
        }
    }
    Rec *dr = rec_of(d);
    if (whole && s->hd_tag && !(dr && dr->tracked)) {  // copia inteira de uma imagem (work_bmp): dica
        d->hd_tag = 0;
        ensure_rec(d)->base = s->hd_tag;
        return;
    }
    if (touch(d)) op_sprite(d, s, sx, sy, w, h, dx, dy, OPAQUE);
}

void tinted(BITMAP *src, BITMAP *dst, const Xform &x, int sx, int sy, int dx, int dy, int w, int h, bool copy_mask) {
    if (!g_enabled || !src || !dst || w <= 0 || h <= 0) return;
    const bool full = !sx && !sy && !dx && !dy && w == src->w && h == src->h && w == dst->w && h == dst->h;
    if (full && (copy_mask || src == dst)) {  // dst inteiro = T(src)
        if (src->hd_tag) {
            set_tag(dst, derive(src->hd_tag, {x}, dst));
            return;
        }
        if (const ListData *sl = list_of(src)) {
            if (auto pd = pushdown(*sl, {x}, nullptr)) {
                dst->hd_tag = 0;
                Rec *dr = ensure_rec(dst);
                dr->tracked = true;
                dr->cur = pd;
                return;
            }
        }
        wrote(dst, 0, 0, w, h);  // sem descricao: copia 1x
        return;
    }
    if (src == dst) {  // regiao no proprio bitmap
        Rec *r = rec_of(dst);
        const Rect rg{dx, dy, dx + w, dy + h};
        if (dst->hd_tag) {
            const int t = dst->hd_tag;
            dst->hd_tag = 0;
            if (!r || !r->tracked) return;
            anchor_tag(dst, r, t);
        }
        if (r && r->tracked && r->cur) {
            if (auto pd = pushdown(*r->cur, {x}, &rg)) {
                r->cur = pd;
                return;
            }
        }
        wrote(dst, dx, dy, w, h);
        return;
    }
    if (!touch(dst)) return;  // src -> regiao de dst, com mascara (ou copiando o magenta)
    Rec *r = rec_of(dst);
    Source s;
    if (src->hd_tag) {
        s.tag = derive(src->hd_tag, {x});
        s.opaque = !g_tags[s.tag].has_mask;
    } else if (const ListData *sl = list_of(src)) {
        if (auto pd = pushdown(*sl, {x}, nullptr)) {
            s.opaque = pd->opaque;
            s.sub = pd;
        }
    }
    if (!s.tag && !s.sub) {
        wrote(dst, dx, dy, w, h);
        return;
    }
    Op o;
    o.flags = copy_mask ? OPAQUE : MASKED;
    o.sx = sx;
    o.sy = sy;
    o.sw = o.dw = w;
    o.sh = o.dh = h;
    o.dx = dx;
    o.dy = dy;
    record(dst, r, std::move(s), o);
}

void wrote(BITMAP *bmp, int x, int y, int w, int h, int flags) {
    if (!g_enabled || !bmp) return;
    Rec *r = rec_of(bmp);
    const Rect bounds = bounds_of(bmp);
    const Rect area = meet({x, y, x + w, y + h}, bounds);
    const bool full = covers(area, bounds);
    if (bmp->hd_tag) {
        const int t = bmp->hd_tag;
        bmp->hd_tag = 0;
        if (r && r->tracked && !full) anchor_tag(bmp, r, t);
    }
    if (!r || !r->tracked || area.empty()) return;
    auto s = make_snap(bmp, area.x0, area.y0, area.x1 - area.x0, area.y1 - area.y0);
    Op o;
    o.flags = static_cast<uint8_t>(OPAQUE | (flags & LINEAR));
    o.sw = o.dw = s->w;
    o.sh = o.dh = s->h;
    o.dx = area.x0;
    o.dy = area.y0;
    set_clip(o, area);
    ListData *l;
    if (full) {
        l = fresh(r, bmp->w, bmp->h).get();
        l->opaque = !s->has_mask;
    } else {
        if (!r->cur) return;
        l = mut(r);
        if (s->has_mask) l->opaque = false;
    }
    l->snaps.push_back(s);
    o.snap = static_cast<int>(l->snaps.size()) - 1;
    push(l, o);
    if (l->ops.size() > kMaxOps) r->cur.reset();
}

void group_begin(BITMAP *dst, int alpha) {
    Rec *r = dst ? rec_of(dst) : nullptr;
    if (!g_enabled || !r || !r->tracked || !r->cur) return;
    Op o;
    o.kind = OP_GROUP_BEGIN;
    o.alpha = static_cast<uint8_t>(std::clamp(alpha, 0, kFull));
    set_clip(o, bounds_of(dst));
    push(mut(r), o);
}

void group_end(BITMAP *dst) {
    Rec *r = dst ? rec_of(dst) : nullptr;
    if (!g_enabled || !r || !r->tracked || !r->cur) return;
    Op o;
    o.kind = OP_GROUP_END;
    set_clip(o, bounds_of(dst));
    push(mut(r), o);
}

// ================================================================== verificacao 1x
// Refaz a lista com as imagens 1x (os pixels das etiquetas, as copias gravadas e, recursivamente,
// as listas de origem) e compara com o bitmap de verdade: pixel diferente = desenho que nao foi
// gravado (ou ordem de objetos diferente).
namespace {

inline uint16_t blend_px(uint16_t q, uint16_t d, int f) {  // fblend_trans
    if (f >= kFull) return q;
    if (f == 16) return static_cast<uint16_t>(((d >> 1) & 0x7BEF) + ((q >> 1) & 0x7BEF) + (d & q & 0x0821));
    auto m = [f](int a, int b) { return b + (((a - b) * f) >> 5); };
    return static_cast<uint16_t>((m(q >> 11, d >> 11) << 11) | (m((q >> 5) & 63, (d >> 5) & 63) << 5) | m(q & 31, d & 31));
}
inline uint16_t add_px(uint16_t q, uint16_t d, int f) {  // fblend_add
    const int sr = ((q >> 11) * f) >> 5, sg = (((q >> 6) & 31) * f) >> 5, sb = ((q & 31) * f) >> 5;
    const int g = std::min(31, ((d >> 6) & 31) + sg);
    return static_cast<uint16_t>((std::min(31, (d >> 11) + sr) << 11) | (g << 6) | std::min(31, (d & 31) + sb));
}

struct Replayer {
    std::unordered_map<const ListData *, std::vector<uint16_t>> cache;

    const std::vector<uint16_t> &get(const ListData &l) {
        auto it = cache.find(&l);
        if (it != cache.end()) return it->second;
        std::vector<uint16_t> px(static_cast<size_t>(l.w) * l.h, kMask);
        run(l, px);
        return cache[&l] = std::move(px);
    }

    void run(const ListData &l, std::vector<uint16_t> &out) {
        std::vector<uint16_t> mask;  // grupo de sombras: pixels marcados
        int group_alpha = kFull;
        bool in_group = false;
        const int W = l.w;
        for (const Op &op : l.ops) {
            const Rect c = meet(clip_of(op), {0, 0, l.w, l.h});
            if (op.kind == OP_GROUP_BEGIN) {
                mask.assign(out.size(), 0);
                group_alpha = op.alpha;
                in_group = true;
                continue;
            }
            if (op.kind == OP_GROUP_END) {
                if (!in_group) continue;
                for (size_t i = 0; i < out.size(); i++)
                    if (mask[i]) out[i] = scale565(out[i], static_cast<uint32_t>(kFull - group_alpha));
                in_group = false;
                continue;
            }
            if (c.empty()) continue;
            if (op.kind == OP_FILL) {
                for (int y = c.y0; y < c.y1; y++)
                    for (int x = c.x0; x < c.x1; x++) {
                        uint16_t &d = out[static_cast<size_t>(y) * W + x];
                        d = op.alpha < kFull ? blend_px(op.color, d, op.alpha) : op.color;
                    }
                continue;
            }
            if (op.kind == OP_SCALE) {
                for (int y = c.y0; y < c.y1; y++)
                    for (int x = c.x0; x < c.x1; x++) {
                        uint16_t &d = out[static_cast<size_t>(y) * W + x];
                        if (d != kMask) d = scale565(d, op.tint);
                    }
                continue;
            }
            const uint16_t *sp;
            int spw, sph;
            if (op.tag) {
                const Tag &t = g_tags[op.tag];
                sp = t.px->data();
                spw = t.w;
                sph = t.h;
            } else if (op.snap >= 0) {
                const Snap &s = *l.snaps[op.snap];
                sp = s.px.data();
                spw = s.w;
                sph = s.h;
            } else if (op.sub >= 0) {
                const ListData &s = *l.subs[op.sub];
                sp = get(s).data();
                spw = s.w;
                sph = s.h;
            } else {
                continue;
            }
            const bool flip = (op.flags & HFLIP) != 0, opaque = (op.flags & OPAQUE) != 0;
            for (int y = c.y0; y < c.y1; y++) {
                const int j = y - op.dy;
                const int syp = op.sy + (op.dh == op.sh ? j : static_cast<int>(static_cast<int64_t>(j) * op.sh / op.dh));
                if (syp < 0 || syp >= sph) continue;
                for (int x = c.x0; x < c.x1; x++) {
                    const int i = x - op.dx;
                    const int ii = op.dw == op.sw ? i : static_cast<int>(static_cast<int64_t>(i) * op.sw / op.dw);
                    const int sxp = op.sx + (flip ? op.sw - 1 - ii : ii);
                    if (sxp < 0 || sxp >= spw) continue;
                    const uint16_t p = sp[static_cast<size_t>(syp) * spw + sxp];
                    if (p == kMask && !opaque) continue;
                    const size_t at = static_cast<size_t>(y) * W + x;
                    if (in_group) {
                        mask[at] = 1;
                        continue;
                    }
                    uint16_t &d = out[at];
                    const uint16_t q = (op.tint < kFull && p != kMask) ? scale565(p, op.tint) : p;
                    if (opaque) d = q;
                    else if (op.flags & ADD) d = add_px(q, d, op.alpha);
                    else d = blend_px(q, d, op.alpha);
                }
            }
        }
    }
};

int compare(const std::vector<uint16_t> &a, const BITMAP *b) {
    int diff = 0;
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++)
            if (a[static_cast<size_t>(y) * b->w + x] != b->line[y][x]) diff++;
    return diff;
}

void log_line(const char *fmt, ...) {
    const char *path = verify_path();
    if (!path) return;
    if (FILE *f = std::fopen(path, "a")) {
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(f, fmt, ap);
        va_end(ap);
        std::fclose(f);
    }
}

void check_capture(const BITMAP *b, const ListData &l) {
    // HOH_HD_VERIFY_DEEP=1: toda lista usada como origem e refeita em 1x e comparada com o bitmap
    static const bool deep = verify_path() && std::getenv("HOH_HD_VERIFY_DEEP");
    static int reported = 0;
    if (!deep || reported >= 200 || b->w != l.w || b->h != l.h) return;
    Replayer rp;
    const int diff = compare(rp.get(l), b);
    if (diff) {
        reported++;
        log_line("[hd] estado %d: lista de origem %dx%d com %u ops nao bate com o bitmap: %d pixels\n", g_label, b->w, b->h,
                 static_cast<unsigned>(l.ops.size()), diff);
    }
}

}  // namespace

void verify(const BITMAP *bmp) {
    static const int every = [] {
        const char *e = std::getenv("HOH_HD_VERIFY_EVERY");
        return e ? std::max(1, std::atoi(e)) : 30;
    }();
    static int frames = 0;
    if (!verify_path() || !bmp || !g_enabled || ++frames % every != 0) return;
    size_t ops = 0, snaps = 0;
    const int diff = verify_diff(bmp, &ops, &snaps);
    if (diff < 0) {
        log_line("[hd] estado %d: sem lista (quadro 1x)\n", g_label);
        return;
    }
    log_line("[hd] estado %d: ops %u  copias %u  pixels diferentes %d (%.2f%%)\n", g_label, static_cast<unsigned>(ops),
             static_cast<unsigned>(snaps), diff, 100.0 * diff / (bmp->w * bmp->h));
}

int verify_diff(const BITMAP *bmp, size_t *ops_out, size_t *snaps_out, BITMAP *replay) {
    const ListData *l = bmp ? list_of(bmp) : nullptr;
    if (!l || l->w != bmp->w || l->h != bmp->h) return -1;
    if (ops_out) *ops_out = l->ops.size();
    if (snaps_out) *snaps_out = l->snaps.size();
    Replayer rp;
    const std::vector<uint16_t> &px = rp.get(*l);
    const int diff = compare(px, bmp);
    if (replay && replay->w == bmp->w && replay->h == bmp->h) std::memcpy(replay->dat, px.data(), px.size() * 2);
    return diff;
}

}  // namespace hd

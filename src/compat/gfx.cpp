// Bitmaps e primitivas de desenho 16 bpp (RGB565), semantica do Allegro 4.0.
#include "compat/allegro.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

// Tabelas _rgb_scale_5/_rgb_scale_6 do Allegro (arredondamento para o mais proximo).
inline int scale5(int v) { return (v * 255 + 15) / 31; }
inline int scale6(int v) { return (v * 255 + 31) / 63; }

struct ClipRect { int l, t, r, b; };  // r/b exclusivos

inline ClipRect clip_of(const BITMAP *bmp) {
    if (bmp->clip) return {bmp->cl, bmp->ct, bmp->cr, bmp->cb};
    return {0, 0, bmp->w, bmp->h};
}

// Recorta um blit contra os limites da origem e o recorte do destino.
bool clip_blit(const BITMAP *s, const BITMAP *d, int &sx, int &sy, int &dx, int &dy, int &w, int &h) {
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    ClipRect c = clip_of(d);
    if (dx < c.l) { w -= c.l - dx; sx += c.l - dx; dx = c.l; }
    if (dy < c.t) { h -= c.t - dy; sy += c.t - dy; dy = c.t; }
    if (dx + w > c.r) w = c.r - dx;
    if (dy + h > c.b) h = c.b - dy;
    return w > 0 && h > 0;
}

BITMAP *new_bitmap(int w, int h, int is_video) {
    if (w < 0 || h < 0) return nullptr;
    BITMAP *b = new BITMAP();
    b->w = w;
    b->h = h;
    b->is_video = is_video;
    b->dat = static_cast<uint16_t *>(std::calloc(std::max(1, w * h), sizeof(uint16_t)));
    b->line = new uint16_t *[std::max(1, h)];
    for (int y = 0; y < h; y++) b->line[y] = b->dat + static_cast<size_t>(y) * w;
    b->clip = 1;
    b->cl = 0; b->ct = 0; b->cr = w; b->cb = h;
    return b;
}

template <typename PixelOp>
void sprite_op(BITMAP *src, BITMAP *dst, int x, int y, PixelOp op) {
    int sx = 0, sy = 0, w = src->w, h = src->h;
    if (!clip_blit(src, dst, sx, sy, x, y, w, h)) return;
    for (int j = 0; j < h; j++) {
        const uint16_t *s = src->line[sy + j] + sx;
        uint16_t *d = dst->line[y + j] + x;
        for (int i = 0; i < w; i++)
            if (s[i] != MASK_COLOR_16) d[i] = op(s[i], d[i]);
    }
}

inline int r5(uint16_t c) { return c >> 11; }
inline int g6(uint16_t c) { return (c >> 5) & 63; }
inline int b5(uint16_t c) { return c & 31; }
inline uint16_t pack565(int r, int g, int b) { return static_cast<uint16_t>((r << 11) | (g << 5) | b); }

}  // namespace

BITMAP *create_bitmap(int width, int height) { return new_bitmap(width, height, 0); }
BITMAP *create_video_bitmap(int width, int height) { return new_bitmap(width, height, 1); }

void destroy_bitmap(BITMAP *bmp) {
    if (!bmp) return;
    std::free(bmp->dat);
    delete[] bmp->line;
    delete bmp;
}

void set_clip(BITMAP *bmp, int x1, int y1, int x2, int y2) {
    if (!x1 && !y1 && !x2 && !y2) {  // Allegro: set_clip(b,0,0,0,0) desliga o recorte
        bmp->clip = 0;
        bmp->cl = 0; bmp->ct = 0; bmp->cr = bmp->w; bmp->cb = bmp->h;
        return;
    }
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    bmp->clip = 1;
    bmp->cl = std::clamp(x1, 0, bmp->w);
    bmp->ct = std::clamp(y1, 0, bmp->h);
    bmp->cr = std::clamp(x2 + 1, 0, bmp->w);
    bmp->cb = std::clamp(y2 + 1, 0, bmp->h);
}

int makecol(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }
int getr(int c) { return scale5((c >> 11) & 0x1F); }
int getg(int c) { return scale6((c >> 5) & 0x3F); }
int getb(int c) { return scale5(c & 0x1F); }

int getpixel(BITMAP *bmp, int x, int y) {
    if (x < 0 || y < 0 || x >= bmp->w || y >= bmp->h) return -1;
    return bmp->line[y][x];
}

void putpixel(BITMAP *bmp, int x, int y, int color) {
    ClipRect c = clip_of(bmp);
    if (x < c.l || y < c.t || x >= c.r || y >= c.b) return;
    bmp->line[y][x] = static_cast<uint16_t>(color);
}

void clear_to_color(BITMAP *bmp, int color) {
    std::fill(bmp->dat, bmp->dat + static_cast<size_t>(bmp->w) * bmp->h, static_cast<uint16_t>(color));
}

void rectfill(BITMAP *bmp, int x1, int y1, int x2, int y2, int color) {
    if (x2 < x1) std::swap(x1, x2);
    if (y2 < y1) std::swap(y1, y2);
    ClipRect c = clip_of(bmp);
    x1 = std::max(x1, c.l); y1 = std::max(y1, c.t);
    x2 = std::min(x2, c.r - 1); y2 = std::min(y2, c.b - 1);
    for (int y = y1; y <= y2; y++)
        std::fill(bmp->line[y] + x1, bmp->line[y] + x2 + 1, static_cast<uint16_t>(color));
}

void circlefill(BITMAP *bmp, int x, int y, int radius, int color) {
    if (radius < 0) return;
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)) + 0.5);
        rectfill(bmp, x - dx, y + dy, x + dx, y + dy, color);
    }
}

void blit(BITMAP *s, BITMAP *d, int sx, int sy, int dx, int dy, int w, int h) {
    if (!s || !d || !clip_blit(s, d, sx, sy, dx, dy, w, h)) return;
    // Mesma imagem com sobreposicao: copiar de baixo para cima quando o destino esta abaixo.
    if (s == d && dy > sy) {
        for (int j = h - 1; j >= 0; j--)
            std::memmove(d->line[dy + j] + dx, s->line[sy + j] + sx, w * sizeof(uint16_t));
    } else {
        for (int j = 0; j < h; j++)
            std::memmove(d->line[dy + j] + dx, s->line[sy + j] + sx, w * sizeof(uint16_t));
    }
}

void masked_blit(BITMAP *s, BITMAP *d, int sx, int sy, int dx, int dy, int w, int h) {
    if (!s || !d || !clip_blit(s, d, sx, sy, dx, dy, w, h)) return;
    for (int j = 0; j < h; j++) {
        const uint16_t *src = s->line[sy + j] + sx;
        uint16_t *dst = d->line[dy + j] + dx;
        for (int i = 0; i < w; i++)
            if (src[i] != MASK_COLOR_16) dst[i] = src[i];
    }
}

void draw_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y) {
    masked_blit(sprite, bmp, 0, 0, x, y, sprite->w, sprite->h);
}

// Espelhado na horizontal (vtable +0x4c no HoH.exe), com mascara.
void draw_sprite_h_flip(BITMAP *bmp, BITMAP *sprite, int x, int y) {
    if (!bmp || !sprite) return;
    ClipRect c = clip_of(bmp);
    int x0 = std::max(x, c.l), x1 = std::min(x + sprite->w, c.r);
    int y0 = std::max(y, c.t), y1 = std::min(y + sprite->h, c.b);
    for (int dy = y0; dy < y1; dy++) {
        const uint16_t *src = sprite->line[dy - y];
        uint16_t *dst = bmp->line[dy];
        for (int dx = x0; dx < x1; dx++) {
            uint16_t p = src[sprite->w - 1 - (dx - x)];
            if (p != MASK_COLOR_16) dst[dx] = p;
        }
    }
}

void hline(BITMAP *bmp, int x1, int y, int x2, int color) { rectfill(bmp, x1, y, x2, y, color); }
void vline(BITMAP *bmp, int x, int y1, int y2, int color) { rectfill(bmp, x, y1, x, y2, color); }

void stretch_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int w, int h) {
    if (!bmp || !sprite || w <= 0 || h <= 0) return;
    ClipRect c = clip_of(bmp);
    int x0 = std::max(x, c.l), x1 = std::min(x + w, c.r);
    int y0 = std::max(y, c.t), y1 = std::min(y + h, c.b);
    for (int dy = y0; dy < y1; dy++) {
        const uint16_t *src = sprite->line[static_cast<int64_t>(dy - y) * sprite->h / h];
        uint16_t *dst = bmp->line[dy];
        for (int dx = x0; dx < x1; dx++) {
            uint16_t p = src[static_cast<int64_t>(dx - x) * sprite->w / w];
            if (p != MASK_COLOR_16) dst[dx] = p;
        }
    }
}

// Semantica do FBlend em 16 bpp (HoH.exe 0x42a510 / 0x429d10): a opacidade 0..255 vira
// f = (fact + 7) >> 3, em 32 niveis; pixels com a cor de mascara nao sao desenhados.

// 0x42a510: f == 32 -> masked_blit; f == 16 -> media exata por mascara de bits;
// senao, por canal: d + ((s - d) * f >> 5).
void fblend_trans(BITMAP *src, BITMAP *dst, int x, int y, int fact) {
    if (!src || !dst) return;
    int f = (std::clamp(fact, 0, 255) + 7) >> 3;
    if (f >= 32) { draw_sprite(dst, src, x, y); return; }
    if (f <= 0) return;
    if (f == 16) {
        sprite_op(src, dst, x, y, [](uint16_t s, uint16_t d) {
            return static_cast<uint16_t>(((d >> 1) & 0x7BEF) + ((s >> 1) & 0x7BEF) + (d & s & 0x0821));
        });
        return;
    }
    sprite_op(src, dst, x, y, [f](uint16_t s, uint16_t d) {
        auto mix = [f](int a, int b) { return b + (((a - b) * f) >> 5); };
        return pack565(mix(r5(s), r5(d)), mix(g6(s), g6(d)), mix(b5(s), b5(d)));
    });
}

// 0x429d10: soma saturada; o verde usa so os 5 bits altos (mascara 0x7C0), como o FBlend.
void fblend_add(BITMAP *src, BITMAP *dst, int x, int y, int fact) {
    if (!src || !dst) return;
    int f = (std::clamp(fact, 0, 255) + 7) >> 3;
    if (f <= 0) return;
    f = std::min(f, 32);
    sprite_op(src, dst, x, y, [f](uint16_t s, uint16_t d) {
        int sr = (r5(s) * f) >> 5, sg = (((s >> 6) & 31) * f) >> 5, sb = (b5(s) * f) >> 5;
        int g = std::min(31, ((d >> 6) & 31) + sg);
        return pack565(std::min(31, r5(d) + sr), g << 1, std::min(31, b5(d) + sb));
    });
}

fixed fixsqrt(fixed x) {
    if (x <= 0) return 0;
    return static_cast<fixed>(std::sqrt(static_cast<double>(x) / 65536.0) * 65536.0 + 0.5);
}

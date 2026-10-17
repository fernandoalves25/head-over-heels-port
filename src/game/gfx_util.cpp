// Texto em bitmap, cores (HSL/tingimento) e montagem dos graficos de interface.
// Portado de HoH.exe (Retrospec 2003); pseudo-C em re/port/frontend.c, FPU conferida no disassembly.
#include "game/gfx_util.h"

#include "compat/hd_render.h"
#include "game/data_bridge.h"
#include "game/globals.h"
#include "game/menu.h"
#include "game/sound.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace hoh {

namespace {

constexpr double kInv255 = 0.00392156862745098;  // 1/255 (constante do .rdata 0x4302d0)

// Bloco N, direcao d, quadro f (sprite). No original: *(BITMAP**)(block + 0x1c0 + (d*20+f)*4).
BITMAP *block_frame(int block, int dir, int frame) {
    return g_data->blocks[block].dirs[dir].frames[frame].a;
}

// Parametros de tint_bitmap ja em double, como no comeco de tint_bitmap/tint_bitmap_bgr.
struct TintParams {
    double hue_d, sat_d, lum_d, boost;
    int boost_pct;
};
TintParams tint_params(int hue, int sat, int lum, int boost_pct) {
    TintParams p;
    p.hue_d = (double)hue * kInv255;
    p.boost = 0.0;
    p.sat_d = (double)sat * kInv255;
    p.lum_d = (double)lum * kInv255;
    p.boost_pct = boost_pct;
    if (0 < boost_pct) p.boost = (double)boost_pct * 0.01 + 1.0;
    return p;
}

// Cor de saida de um luma (0..255): o meio do laco de tint_bitmap.
inline void tint_luma(uint32_t luma, const TintParams &p, int *r, int *g, int *b) {
    const double l = (double)luma * p.lum_d * kInv255;
    hsl_to_rgb(p.hue_d, l, p.sat_d, r, g, b);
    if (0 < p.boost_pct) {
        *r = (int)((double)*r * p.boost);  // _ftol: trunca
        *g = (int)((double)*g * p.boost);
        *b = (int)((double)*b * p.boost);
        if (0xff < *r) *r = 0xff;
        if (0xff < *g) *g = 0xff;
        if (0xff < *b) *b = 0xff;
    }
}

// Um pixel nao-mascara de tint_bitmap (kBgr troca R/B na saida). Port: fora do laco para o
// renderizador HD usar exatamente a mesma conta.
template <bool kBgr>
inline uint16_t tint_pixel(uint16_t c, const TintParams &p) {
    int b = (c & 5) << 3;  // original: bug (and eax,5 em 0x40c9fd); deveria ser (c & 0x1f) << 3
    int g = (c >> 3) & 0xfc;
    int r = (c >> 8) & 0xf8;
    const uint32_t luma = ((uint32_t)g_luma_weight_b * (uint32_t)b +
                           (uint32_t)g_luma_weight_g * (uint32_t)g +
                           (uint32_t)g_luma_weight_r * (uint32_t)r) >> 16;
    tint_luma(luma, p, &r, &g, &b);
    const int hi = kBgr ? b : r;  // canal gravado nos bits altos
    const int lo = kBgr ? r : b;  // canal gravado nos bits baixos
    const uint32_t px = ((((uint32_t)hi & 0xfffffff8u) << 5 | ((uint32_t)g & 0xfffffffcu)) << 3) |
                        (uint32_t)(lo >> 3);
    return (uint16_t)px;
}

// Corpo comum de tint_bitmap (0x40c8c0) e tint_bitmap_bgr (0x40c620). O original tem o laco
// duplicado nas duas funcoes; as copias so diferem na montagem do pixel de saida (kBgr troca R/B).
template <bool kBgr>
void tint_region(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct, int src_x,
                 int src_y, int dst_x, int dst_y, int w, int h, int copy_mask) {
    const TintParams p = tint_params(hue, sat, lum, boost_pct);
    // original: tambem guarda o maior L calculado numa local (local_30) que nunca e lida.
    if (src_x == -1) {  // bitmap inteiro; original: dst_x NAO e zerado
        src_y = 0;
        src_x = 0;
        dst_y = 0;
        w = src->w;
        h = src->h;
    }
    const uint16_t mask = (uint16_t)g_mask_color;
    // Colunas por fora, linhas por dentro (como no original).
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            const uint16_t c = src->line[src_y + j][src_x + i];
            if (c == mask) {
                if ((uint8_t)copy_mask != 0) dst->line[dst_y + j][dst_x + i] = c;  // so o byte baixo
                continue;
            }
            dst->line[dst_y + j][dst_x + i] = tint_pixel<kBgr>(c, p);
        }
    }
    // port (HD): a regiao de dst agora e a de src tingida
    hd::Xform x;
    x.kind = hd::XF_TINT;
    x.bgr = kBgr;
    x.a = (int16_t)hue;
    x.b = (int16_t)sat;
    x.c = (int16_t)lum;
    x.d = (int16_t)boost_pct;
    hd::tinted(src, dst, x, src_x, src_y, dst_x, dst_y, w, h, (uint8_t)copy_mask != 0);
}

// port (HD): o tingimento para o renderizador. 1x: o mesmo pixel de tint_bitmap.
uint16_t hd_tint_px(uint16_t c, const hd::Xform &x) {
    const TintParams p = tint_params(x.a, x.b, x.c, x.d);
    return x.bgr ? tint_pixel<true>(c, p) : tint_pixel<false>(c, p);
}

// Texels do pacote: o luma sai do RGB8 (o azul com o mesmo bug do 1x) e a cor de uma tabela por
// luma com as contas de tint_bitmap, sem arredondar para 16 bits.
void hd_tint_rgba(uint8_t *px, size_t n, const hd::Xform &x) {
    const TintParams p = tint_params(x.a, x.b, x.c, x.d);
    uint8_t lut[256][3];
    for (int l = 0; l < 256; l++) {
        int r = 0, g = 0, b = 0;
        tint_luma((uint32_t)l, p, &r, &g, &b);
        if (x.bgr) std::swap(r, b);  // como a variante BGR grava (R e B trocados)
        lut[l][0] = (uint8_t)std::clamp(r, 0, 255);
        lut[l][1] = (uint8_t)std::clamp(g, 0, 255);
        lut[l][2] = (uint8_t)std::clamp(b, 0, 255);
    }
    for (size_t i = 0; i < n; i++) {
        uint8_t *t = px + i * 4;
        if (t[3] == 0) continue;
        const uint32_t b = (uint32_t)(((t[2] >> 3) & 5) << 3);
        const uint32_t luma = std::min<uint32_t>(255, ((uint32_t)g_luma_weight_b * b + (uint32_t)g_luma_weight_g * t[1] +
                                                       (uint32_t)g_luma_weight_r * t[0]) >> 16);
        t[0] = lut[luma][0];
        t[1] = lut[luma][1];
        t[2] = lut[luma][2];
    }
}

}  // namespace

// 0x004025f0 glyph_index_upper
int glyph_index_upper(char c) {
    if ('@' < c && c < '[') return c - 0x37;  // A..Z -> 10..35
    if ('/' < c && c < ':') return c - 0x30;  // 0..9 -> 0..9
    switch (c) {
    case '!': return 0x28;
    case '#': return 0x2d;
    case '$': return 0x2e;
    case '%': return 0x31;
    case ',': return 0x26;
    case '-': return 0x24;
    case '.': return 0x25;
    case ':': return 0x29;
    case '>': return 0x30;
    case '?': return 0x27;
    case '[': return 0x2a;
    case ']': return 0x2b;
    case '_': return 0x32;
    case '`': return 0x2c;
    default: return 0x2f;
    }
}

// 0x00402710 glyph_index_mixed
int glyph_index_mixed(char c) {
    if ('@' < c && c < '[') return c - 0x41;  // A..Z -> 0..25
    if ('`' < c && c < '{') return c - 0x47;  // a..z -> 26..51
    if ('/' < c && c < ':') return c + 4;     // 0..9 -> 52..61
    switch (c) {
    case '!': return 0x42;
    case '#': return 0x44;
    case '+': return 0x3e;
    case ':': return 0x40;
    case '@': return 0x3f;
    case '`': return 0x43;
    default: return 0x41;
    }
}

// 0x004027f0 draw_text_w18
void draw_text_w18(BITMAP *dst, BITMAP *font_strip, const char *text, int x, int y) {
    const int len = (int)std::strlen(text);
    for (int i = 0; i < len; i++) {
        const int glyph = glyph_index_upper(text[i]);
        masked_blit(font_strip, dst, glyph * 0x12, 0, x, y, 0x12, font_strip->h);
        x += 0x12;
    }
}

// 0x00402850 draw_text_w10
void draw_text_w10(BITMAP *dst, BITMAP *font_strip, const char *text, int x, int y) {
    const int len = (int)std::strlen(text);
    for (int i = 0; i < len; i++) {
        const int glyph = glyph_index_upper(text[i]);
        masked_blit(font_strip, dst, glyph * 10, 0, x, y, 10, font_strip->h);
        x += 10;
    }
}

// 0x004028b0 draw_text_w8
void draw_text_w8(BITMAP *dst, BITMAP *font_strip, const char *text, int x, int y) {
    const int len = (int)std::strlen(text);
    for (int i = 0; i < len; i++) {
        const int glyph = glyph_index_mixed(text[i]);
        masked_blit(font_strip, dst, glyph << 3, 0, x, y, 8, font_strip->h);
        x += 8;
    }
}

// 0x0040c470 hsl_hue_to_rgb
// Assinatura reconstruida do FPU: cdecl (double m1 @esp+4, double m2 @esp+0xc, double hue @esp+0x14),
// retorno int via _ftol (truncamento).
int hsl_hue_to_rgb(double m1, double m2, double hue) {
    if (hue < 0.0) hue = hue + 1.0;
    if (1.0 < hue) hue = hue - 1.0;
    if (hue * 6.0 < 1.0) return (int)((m1 + (m2 - m1) * hue * 6.0) * 255.0);
    if (hue + hue < 1.0) return (int)(m2 * 255.0);
    if (hue * 3.0 < 2.0) return (int)((m1 + (m2 - m1) * (2.0 / 3.0 - hue) * 6.0) * 255.0);
    return (int)(m1 * 255.0);
}

// 0x0040c530 hsl_to_rgb
// Assinatura reconstruida: (double h @ebp+8, double l @ebp+0x10, double s @ebp+0x18, int *r, *g, *b).
void hsl_to_rgb(double h, double l, double s, int *r, int *g, int *b) {
    if (s == 0.0) {
        const int v = (int)(l * 255.0);
        *b = v;
        *g = v;
        *r = v;
        return;
    }
    double m2;
    if (0.5 < l)
        m2 = (l + s) - l * s;
    else
        m2 = (s + 1.0) * l;
    const double m1 = (l + l) - m2;
    *r = hsl_hue_to_rgb(m1, m2, h + 1.0 / 3.0);
    *g = hsl_hue_to_rgb(m1, m2, h);
    *b = hsl_hue_to_rgb(m1, m2, h - 1.0 / 3.0);
}

// 0x0040c8c0 tint_bitmap
void tint_bitmap(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct, int src_x,
                 int src_y, int dst_x, int dst_y, int w, int h, int copy_mask) {
    if (g_convert_colors == 0) {  // original: le so o byte de g_convert_colors
        tint_region<false>(src, dst, hue, sat, lum, boost_pct, src_x, src_y, dst_x, dst_y, w, h, copy_mask);
        return;
    }
    tint_bitmap_bgr(src, dst, hue, sat, lum, boost_pct, src_x, src_y, dst_x, dst_y, w, h, copy_mask);
}

// 0x0040c620 tint_bitmap_bgr
void tint_bitmap_bgr(BITMAP *src, BITMAP *dst, int hue, int sat, int lum, int boost_pct, int src_x,
                     int src_y, int dst_x, int dst_y, int w, int h, int copy_mask) {
    tint_region<true>(src, dst, hue, sat, lum, boost_pct, src_x, src_y, dst_x, dst_y, w, h, copy_mask);
}

// 0x0040cbb0 make_silhouette
void make_silhouette(BITMAP *src, BITMAP *dst) {
    const uint16_t mask = (uint16_t)g_mask_color;
    const int w = src->w;
    const int h = src->h;
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (src->line[y][x] == mask)
                dst->line[y][x] = mask;
            else
                dst->line[y][x] = 0;
        }
    }
    hd::Xform sil;  // port (HD): dst = silhueta de src
    sil.kind = hd::XF_SILHOUETTE;
    hd::tinted(src, dst, sil, 0, 0, 0, 0, w, h, true);
}

// 0x0040e7f0 create_grayscale_bitmap
BITMAP *create_grayscale_bitmap(BITMAP *src) {
    BITMAP *dst = create_bitmap(src->w, src->h);
    for (int x = 0; x < dst->w; x++) {
        for (int y = 0; y < dst->h; y++) {
            int c = getpixel(src, x, y);  // original: vtable+0x20
            int out = g_mask_color;
            if (c != g_mask_color) {
                const int sum = getb(c) + getg(c) + getr(c);
                const int v = sum / 4;  // original: divide por 4 (nao 3)
                out = makecol(v, v, v);
            }
            putpixel(dst, x, y, out);  // original: vtable+0x24
        }
    }
    hd::tag_gray_copy(dst, src);  // port: no HD, a imagem ampliada em cinza
    return dst;
}

// 0x00417ca0 bitmap_make_h_flip
void bitmap_make_h_flip(BITMAP *src, BITMAP *dst) {
    clear_to_color(dst, makecol(0xff, 0, 0xff));
    draw_sprite_h_flip(dst, src, 0, 0);
    hd::tag_flip_copy(dst, src);  // port: a versao HD e a mesma imagem, espelhada
}

// 0x0040e8b0 register_bitmap
void register_bitmap(BITMAP *bmp) {
    // original: sem verificacao de limite
    g_bitmap_registry[g_bitmap_registry_count] = bmp;
    g_bitmap_registry_count = g_bitmap_registry_count + 1;
}

// 0x00405ab0 build_tinted_fonts
void build_tinted_fonts() {
    int hue = DAT_00431d0c;  // 126
    for (int i = 0; i < 8; i++) {
        tint_bitmap(g_font_w18, g_font_w18_tinted[i], hue, 0x9b, 0xd3, 0, -1, 0, 0, 0, 0, 0, 1);
        hue -= 0xc;
    }
}

// 0x0040e9c0 init_ui_graphics
void init_ui_graphics() {
    g_mask_color = makecol(0xff, 0, 0xff);
    hd::set_tint_functions(hd_tint_px, hd_tint_rgba);  // port (HD)

    // Icones do HUD e versoes cinza.
    g_icon_head = block_frame(0, 1, 1);  // g_blocks[0] + 0x214
    g_icon_head_grey = create_grayscale_bitmap(g_icon_head);
    register_bitmap(g_icon_head_grey);
    g_icon_heels = block_frame(1, 0, 1);  // g_blocks[1] + 0x1c4
    g_icon_heels_grey = create_grayscale_bitmap(g_icon_heels);
    register_bitmap(g_icon_heels_grey);
    g_icon_bag = block_frame(3, 0, 0);
    g_icon_bag_grey = create_grayscale_bitmap(g_icon_bag);
    register_bitmap(g_icon_bag_grey);
    g_icon_doughnuts = block_frame(4, 0, 0);
    g_icon_doughnut_grey = create_grayscale_bitmap(g_icon_doughnuts);
    register_bitmap(g_icon_doughnut_grey);
    g_icon_hooter = block_frame(5, 0, 0);
    g_icon_hooter_grey = create_grayscale_bitmap(g_icon_hooter);
    register_bitmap(g_icon_hooter_grey);

    // original: laco sobre 0xa4a48c..0xa4a494 (g_hud_old, g_hud_new)
    g_hud_old = create_bitmap(0x280, 0xa0);
    register_bitmap(g_hud_old);
    g_hud_new = create_bitmap(0x280, 0xa0);
    // port (HD): bitmaps de trabalho com lista propria (g_hud_old fica em 1x: os 160 circulos por
    // quadro da transicao do HUD nao cabem numa lista)
    hd::track(g_hud_new);
    register_bitmap(g_hud_new);

    // Procura, a partir de g_images[2], a primeira imagem com w == 16 ou h == 17; `base` fica no
    // indice seguinte (6 com o HoHOriginal.dat original) e as imagens da UI sao relativas a ele.
    int w = 0;
    int h = 0;
    int base = 2;
    if (2 < g_image_count) {
        do {
            if (w == 0x10 || h == 0x11) break;
            BITMAP *img = g_images[base];
            base++;
            w = img->w;
            h = img->h;
        } while (base < g_image_count);
    }
    DAT_005392ec = g_images[base - 1];
    DAT_00a4a47c = g_images[base];
    g_font_small = g_images[base + 1];
    g_img_ending_logo = g_images[base + 2];
    g_bar_image = g_images[base + 3];
    DAT_0044b7bc = g_images[base + 4];
    DAT_00a4a488 = g_font_small;
    tint_bitmap(g_font_small, g_font_small, 0x3c, 0xbe, 0xb4, 0, -1, 0, 0, 0, 0, 0, 1);
    g_font_w10 = g_images[base + 12];
    g_img_title = g_images[base + 11];
    g_font_w18 = g_images[base + 5];

    // Fontes derivadas da fonte de 18 px.
    g_font_title = create_bitmap(g_font_w18->w, g_font_w18->h);
    register_bitmap(g_font_title);
    for (int i = 0; i < 8; i++) {
        g_font_w18_tinted[i] = create_bitmap(g_font_w18->w, g_font_w18->h);
        register_bitmap(g_font_w18_tinted[i]);
    }
    g_font_medium = create_bitmap(g_font_w18->w, 0x12);
    clear_to_color(g_font_medium, g_mask_color);  // original: vtable+0x9c
    stretch_sprite(g_font_medium, g_font_w18, 0, 0, g_font_w18->w, 0x12);
    hd::tag_stretch_copy(g_font_medium, g_font_w18);  // port (HD): a fonte do pacote, achatada
    register_bitmap(g_font_medium);
    g_font_tinted = create_bitmap(g_font_medium->w, g_font_medium->h);
    hd::track(g_font_tinted);
    register_bitmap(g_font_tinted);

    // Logo HEAD OVER HEELS (+ Head e Heels) e sua silhueta.
    g_bmp_logo = create_bitmap(0x112, 0x5e);
    g_bmp_logo_shadow = create_bitmap(0x112, 0x5e);
    hd::track(g_bmp_logo);
    hd::track(g_bmp_logo_shadow);
    clear_to_color(g_bmp_logo, g_mask_color);
    clear_to_color(g_bmp_logo_shadow, g_mask_color);
    tint_bitmap(g_font_w18, g_font_title, 0, 200, 0xd7, 0, -1, 0, 0, 0, 0, 0, 1);
    draw_text_w18(g_bmp_logo, g_font_title, s_HEAD_004333d8, 0, 0);
    draw_text_w18(g_bmp_logo, g_font_title, s_HEELS_004333d0, 0xb4, 0);
    tint_bitmap(g_font_medium, g_font_tinted, 0x6e, 0xb4, 0xaa, 0, -1, 0, 0, 0, 0, 0, 1);
    draw_text_w18(g_bmp_logo, g_font_tinted, s_OVER_004333c8, 0x5a, 8);
    draw_sprite(g_bmp_logo, g_icon_head, 0xf, 0x28);
    draw_sprite(g_bmp_logo, g_icon_heels, 200, 0x28);
    make_silhouette(g_bmp_logo, g_bmp_logo_shadow);
    register_bitmap(g_bmp_logo);
    register_bitmap(g_bmp_logo_shadow);

    // Bitmaps de trabalho: painel do menu e linha de texto.
    g_bmp_panel = create_bitmap(0x17c, 0xfa);
    g_bmp_panel_shadow = create_bitmap(0x17c, 0xfa);
    hd::track(g_bmp_panel);
    hd::track(g_bmp_panel_shadow);
    register_bitmap(g_bmp_panel);
    register_bitmap(g_bmp_panel_shadow);
    g_bmp_text_line = create_bitmap(0x2d0, g_font_w18->h);
    g_bmp_text_line_shadow = create_bitmap(0x2d0, g_font_w18->h);
    hd::track(g_bmp_text_line);
    hd::track(g_bmp_text_line_shadow);
    register_bitmap(g_bmp_text_line);
    register_bitmap(g_bmp_text_line_shadow);

    // COPYRIGHT 2003 RETROSPEC, com cada letra de RETROSPEC (x = 270..414) num matiz (0, 26, 52...).
    g_bmp_copyright = create_bitmap(0x1b0, g_font_medium->h);
    g_bmp_copyright_shadow = create_bitmap(0x1b0, g_font_medium->h);
    hd::track(g_bmp_copyright);
    hd::track(g_bmp_copyright_shadow);
    clear_to_color(g_bmp_copyright, g_mask_color);
    draw_text_w18(g_bmp_copyright, g_font_medium, s_COPYRIGHT_2003_RETROSPEC_004333ac, 0, 0);
    {
        int hue = 0;
        int x = 0x10e;
        do {
            tint_bitmap(g_bmp_copyright, g_bmp_copyright, hue, 0x9b, 200, 0, x, 0, x, 0, 0x12, 0x12, 0);
            x += 0x12;
            hue += 0x1a;
        } while (x < 0x1b0);
    }
    make_silhouette(g_bmp_copyright, g_bmp_copyright_shadow);
    register_bitmap(g_bmp_copyright);
    register_bitmap(g_bmp_copyright_shadow);

    // Figuras que emergem na agua do menu principal.
    for (int i = 0; i < 5; i++) g_water_shapes[i] = g_images[base + 6 + i];

    // Frases do ticker de creditos pre-renderizadas (len*18 x 18) e suas silhuetas.
    for (int i = 0; i < 24; i++) {
        const int tw = (int)std::strlen(g_ticker_texts[i]) * 0x12;
        g_bmp_ticker[i] = create_bitmap(tw, g_font_medium->h);
        g_bmp_ticker_shadow[i] = create_bitmap(tw, g_font_medium->h);
        hd::track(g_bmp_ticker[i]);
        hd::track(g_bmp_ticker_shadow[i]);
        clear_to_color(g_bmp_ticker[i], g_mask_color);  // original: vtable+0x9c
        draw_text_w18(g_bmp_ticker[i], g_font_medium, g_ticker_texts[i], 0, 0);
        make_silhouette(g_bmp_ticker[i], g_bmp_ticker_shadow[i]);
        register_bitmap(g_bmp_ticker[i]);
        register_bitmap(g_bmp_ticker_shadow[i]);
    }

    build_tinted_fonts();  // original: chamada com um argumento 0 que a funcao ignora

    // Coroa (bloco 9): silhueta e versao cinza.
    g_icon_crown = block_frame(9, 0, 0);
    g_icon_crown_shadow = create_bitmap(g_icon_crown->w, g_icon_crown->h);
    make_silhouette(g_icon_crown, g_icon_crown_shadow);
    register_bitmap(g_icon_crown_shadow);
    g_icon_crown_gray = create_grayscale_bitmap(g_icon_crown);
    register_bitmap(g_icon_crown_gray);

    g_popup_bitmap = create_bitmap(0x24, g_font_w18->h);
    hd::track(g_popup_bitmap);
    register_bitmap(g_popup_bitmap);

    scan_save_slots();

    // original: laco sobre 0x44b7d0..0x44b7f0, passo 8
    for (int i = 0; i < 4; i++) g_voice_slots[i].voice = -1;
}

}  // namespace hoh

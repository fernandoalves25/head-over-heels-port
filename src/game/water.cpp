// Efeito de agua do fundo dos menus (estados 0, 2 e 4) e transicoes de tela.
//
// Os mapas de altura sao dois int16[480][640] (g_water_buf_a / g_water_buf_b) varridos de
// forma LINEAR, como no original: a simulacao e o render percorrem 0x4ab00 = 478*640 celulas
// seguidas a partir da linha 1, entao o vizinho "esquerdo" da coluna 0 e a coluna 639 da linha
// anterior (e vice-versa). O render escreve 0x4ab00 pixels contiguos a partir de line[1] do
// bitmap de destino (assume 640 de largura com pitch == w, o que vale para g_backbuffer).
#include "game/water.h"

#include <cstdlib>
#include <cstring>

#include "compat/hd_render.h"
#include "game/globals.h"

namespace hoh {

namespace {
constexpr int kWaterW = 0x280;          // 640
constexpr int kWaterCells = 0x4ab00;    // 478 * 640: linhas 1..478
}  // namespace

// 0x00402910 water_init
void water_init() {
    // original: rep stosd de 0x25800 dwords em cada buffer (480*640 int16)
    std::memset(g_water_buf_a, 0, sizeof(g_water_buf_a));
    std::memset(g_water_buf_b, 0, sizeof(g_water_buf_b));
    g_water_cur = &g_water_buf_a[0][0];
    g_water_prev = &g_water_buf_b[0][0];
}

// 0x00402950 water_propagate
// Um passo da equacao de onda: prev[k] = n - n/32, n = (cima+esq+dir+baixo)/2 - prev[k].
void water_propagate(int16_t *cur, int16_t *prev) {
    for (int k = kWaterW; k < kWaterW + kWaterCells; k++) {
        // original: soma em 32 bits, sar 1, depois subtracao em 16 bits (sub ax, [prev])
        int16_t n = static_cast<int16_t>(
            ((cur[k - kWaterW] + cur[k - 1] + cur[k + 1] + cur[k + kWaterW]) >> 1) - prev[k]);
        // original: sar di,5 em 16 bits; resultado truncado para 16 bits
        prev[k] = static_cast<int16_t>(n - (n >> 5));
    }
}

// 0x004029b0 water_render_bgr
// Variante com R e B trocados (g_convert_colors): R = 31, G = s>>2, B = s>>3.
void water_render_bgr(BITMAP *dst, int16_t *water) {
    const int16_t *h = water + 0x281;  // original: param_2 + 0x502 bytes (linha 1, coluna 1)
    uint16_t *out = dst->line[1];      // original: *(bmp + 0x44) = line[1]
    for (int i = 0; i < kWaterCells; i++) {
        // original: mov ax,[h-4]; sub ax,[h] (16 bits); clamp com sinal em [-127, 127]
        int16_t d = static_cast<int16_t>(h[i - 2] - h[i]);
        uint32_t v;
        if (d < 0x80) {
            if (d < -0x7f)
                v = 0xffffff81u;
            else
                v = static_cast<uint32_t>(d);
        } else {
            v = 0x7f;
        }
        uint32_t s = v + 0x7f;
        out[i] = static_cast<uint16_t>(static_cast<uint16_t>(((s & 0xfffffffcu) | 0xffffff00u) << 3) |
                                       (static_cast<int16_t>(s) >> 3));
    }
    int16_t c = static_cast<int16_t>(makecol(0x7f, 0x7f, 0xff));
    // original: hline(dst, 0, 0, 0x27f, c) e hline(dst, 0, 0x1df, 0x27f, c) (vtable +0x2c)
    rectfill(dst, 0, 0, 0x27f, 0, c);
    rectfill(dst, 0, 0x1df, 0x27f, 0x1df, c);
    hd::wrote(dst, 0, 0, dst->w, dst->h, hd::LINEAR);  // port (HD): o quadro vira uma copia 1x (com filtro)
}

// 0x00402a50 water_render
// s = clamp(h[x-1] - h[x+1], -127, 127) + 127; pixel R = s>>3, G = s>>2, B = 31.
void water_render(BITMAP *dst, int16_t *water) {
    if (g_convert_colors != 0) {
        water_render_bgr(dst, water);
        return;
    }
    const int16_t *h = water + 0x281;  // original: param_2 + 0x502 bytes
    uint16_t *out = dst->line[1];      // original: *(bmp + 0x44) = line[1]
    for (int i = 0; i < kWaterCells; i++) {
        int16_t d = static_cast<int16_t>(h[i - 2] - h[i]);
        uint32_t v;
        if (d < 0x80) {
            if (d < -0x7f)
                v = 0xffffff81u;
            else
                v = static_cast<uint32_t>(d);
        } else {
            v = 0x7f;
        }
        uint32_t s = v + 0x7f;
        out[i] = static_cast<uint16_t>(static_cast<uint16_t>((((s & 0xfffffff8u) << 5) | s) << 3) | 0x1f);
    }
    int16_t c = static_cast<int16_t>(makecol(0x7f, 0x7f, 0xff));
    // original: hline(dst, 0, 0, 0x27f, c) e hline(dst, 0, 0x1df, 0x27f, c) (vtable +0x2c)
    rectfill(dst, 0, 0, 0x27f, 0, c);
    rectfill(dst, 0, 0x1df, 0x27f, 0x1df, c);
    hd::wrote(dst, 0, 0, dst->w, dst->h, hd::LINEAR);  // port (HD): o quadro vira uma copia 1x (com filtro)
}

// 0x00402b10 water_random_drop
// Gota de altura 0..1023 em x 1..638, y 1..478.
void water_random_drop(int16_t *water) {
    int rx = std::rand();  // original: 1o rand = x, 2o = y, 3o = altura
    int ry = std::rand();
    int rv = std::rand();
    water[(ry % 0x1de + 1) * kWaterW + rx % 0x27e + 1] = static_cast<int16_t>(rv % 0x400);
}

// 0x00402b60 water_stamp_bitmap
// Escreve `value` em toda posicao nao transparente de `shape`, com canto em (x, y).
// original: sem recorte; o chamador garante que a figura cabe no mapa.
void water_stamp_bitmap(BITMAP *shape, int16_t *water, int x, int y, int16_t value) {
    int h = shape->h;
    int w = shape->w;
    if (0 < h) {
        int16_t *row = water + (y * kWaterW + x);
        for (int j = 0; j < h; j++) {
            const uint16_t *src = shape->line[j];
            for (int i = 0; i < w; i++) {
                // original: comparacao de 16 bits com a palavra baixa de g_mask_color
                if (static_cast<int16_t>(src[i]) != static_cast<int16_t>(g_mask_color))
                    row[i] = value;
            }
            row += kWaterW;
        }
    }
}

// 0x00402bd0 water_update_title_shapes
// Faz emergir na agua do menu principal, uma de cada vez, as 5 figuras de g_water_shapes
// (nomes dos creditos), com altura crescente (10 por quadro ate 255) durante 340 quadros.
void water_update_title_shapes(int16_t *water) {
    if (0 < g_menu_timer) {
        g_menu_timer = g_menu_timer - 1;
        return;
    }
    if (0 < g_water_shape_timer) {
        int v = (0x154 - g_water_shape_timer) * 10;
        if (0xff < v)
            v = 0xff;
        water_stamp_bitmap(g_water_shapes[g_water_shape_idx], water, g_water_shape_x, g_water_shape_y,
                           static_cast<int16_t>(v));
        g_water_shape_timer = g_water_shape_timer - 1;
        return;
    }
    int r = std::rand();
    g_water_shape_timer = 0x154;
    g_menu_timer = r % 0x32 + 100;
    g_water_shape_idx = g_water_shape_idx + 1;
    if (g_water_shape_idx == 5)
        g_water_shape_idx = 0;
    r = std::rand();
    g_water_shape_x = r % (0xaa - g_water_shapes[g_water_shape_idx]->w) + 2;
    r = std::rand();
    g_water_shape_y = r % (0x1a4 - g_water_shapes[g_water_shape_idx]->h) + 2;
    // original: rand() % 2 com o idioma de resto com sinal do MSVC (and 0x80000001)
    if (std::rand() % 2 != 0)
        g_water_shape_x = g_water_shape_x + 0x1d2;  // metade direita da tela
}

// 0x0040e090 draw_water_background
// No maximo um passo de simulacao por tick de logica (4 gotas + propagacao); nos outros
// quadros so redesenha. E o unico vsync() dos estados 0, 2 e 4 sem triple buffering.
void draw_water_background(BITMAP *dst) {
    if (g_triple_buffer == 0)
        vsync();
    if (g_tick_counter - g_water_last_tick < 1) {
        water_render(dst, g_water_cur);
        return;
    }
    g_water_last_tick = g_tick_counter;
    water_random_drop(g_water_cur);
    water_random_drop(g_water_cur);
    water_random_drop(g_water_cur);
    water_random_drop(g_water_cur);
    water_propagate(g_water_cur, g_water_prev);
    // original: desenha o mapa ANTIGO (com as gotas) e so depois troca os buffers
    water_render(dst, g_water_cur);
    int16_t *t = g_water_cur;
    g_water_rendered = g_water_cur;  // original: escrito e nunca lido
    g_water_cur = g_water_prev;
    g_water_prev = t;
}

// 0x00405440 start_screen_transition
void start_screen_transition(int type) {
    g_transition_frame = 0;
    g_transition_active = 1;
    // original: -(uint)(type != -1) & type
    g_transition_type = (type != -1) ? type : 0;
}

// port (HD): copia de dst antes da transicao (ver transition_column_wipe).
static BITMAP *wipe_source(BITMAP *dst) {
    static BITMAP *copy = nullptr;
    if (!copy || copy->w != dst->w || copy->h != dst->h) {
        destroy_bitmap(copy);
        copy = create_bitmap(dst->w, dst->h);
    }
    blit(dst, copy, 0, 0, 0, 0, dst->w, dst->h);
    return copy;
}

// 0x0040d1f0 transition_column_wipe
// 20 colunas de 32 px: as (step-31) primeiras vem inteiras de src; as seguintes deslizam src
// empurrando dst, com larguras 31, 30, ... (ou step, step-1, ...). step = 52 = tela toda src.
void transition_column_wipe(BITMAP *dst, int step, bool from_left, BITMAP *src) {
    // port (HD): cada copia de dst para dst so le a propria coluna, que este passo ainda nao mexeu;
    // com a lista HD gravando, elas leem de uma copia de dst (o mesmo resultado) para a lista nao
    // encadear uma versao do quadro por coluna.
    BITMAP *from = (from_left && hd::recording(dst)) ? wipe_source(dst) : dst;
    int slide = step;        // largura de src na coluna atual
    int full = step - 0x1f;  // colunas inteiras de src
    if (full < 1) {
        full = 0;
    } else {
        slide = step - full;  // = 31
        int x = from_left ? 0 : full * -0x20 + 0x27f;
        blit(src, dst, x, 0, x, 0, full * 0x20, 0x1e0);
    }
    int col = 0;
    if (0 < slide) {
        int x = full * 0x20;            // borda esquerda da coluna (from_left)
        int keep = 0x20 - slide;        // largura de dst empurrada
        int xr = full * -0x20 + 0x29f;  // 671 - x
        do {
            int sx;
            int dx;
            if (!from_left) {
                // original: caminho nunca usado; as contas nao sao o espelho exato do outro lado
                // (0x25f/0x27f em vez de 0x260/0x280 e src/dst da 2a copia deslocados de 32 px)
                blit(dst, dst, (slide - x) + 0x25f, 0, xr - 0x40, 0, keep, 0x1e0);
                dx = xr - slide;
                sx = xr - 0x20;
            } else {
                blit(from, dst, x, 0, x + slide, 0, keep, 0x1e0);
                sx = (col + 1 + full) * 0x20 - slide;
                dx = x;
            }
            blit(src, dst, sx, 0, dx, 0, slide, 0x1e0);
            slide = slide - 1;
            keep = keep + 1;
            col = col + 1;
            xr = xr - 0x20;
            x = x + 0x20;
        } while (0 < slide);
    }
}

}  // namespace hoh

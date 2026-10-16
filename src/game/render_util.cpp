// Rotinas pequenas de desenho/consulta usadas pelo front-end e pelo jogo.
#include "game/globals.h"
#include "game/pending.h"

#include <cstring>

namespace hoh {

// 0x004158a0 draw_sprite_lit: draw_sprite 16 bpp com recorte (so contra o tamanho do destino),
// pulando magenta e multiplicando o RGB565 por light/256 (em 32 avos: light >> 3).
void draw_sprite_lit(BITMAP *dst, BITMAP *spr, int x, int y, int light) {
    const uint32_t mask = static_cast<uint32_t>(makecol(0xff, 0, 0xff));
    const int y_orig = y;
    int w = spr->w, h = spr->h, sx0 = 0, sy0 = 0;  // w/h = fim (exclusivo) em coordenadas da origem
    if (y < 0) { sy0 = -y; y = 0; }
    if (x < 0) { sx0 = -x; x = 0; }
    if (dst->w < spr->w + x) w = dst->w - x;            // largura: com o x ja recortado
    if (dst->h < spr->h + y_orig) h = dst->h - y_orig;  // altura: com o y ORIGINAL (como no exe)
    int f = light >> 3;
    for (int j = sy0; j < h; j++) {
        if (sx0 >= w) continue;
        const uint16_t *s = spr->line[j] + sx0;
        uint16_t *d = dst->line[y + (j - sy0)] + x;
        for (int i = sx0; i < w; i++, s++, d++) {
            uint32_t p = *s;
            if (p == mask) continue;
            p = ((((p & 0x7e0) << 16) | (p & 0x7e0f81f)) * static_cast<uint32_t>(f) >> 5) & 0x7e0f81f;
            *d = static_cast<uint16_t>((p >> 16) | p);
        }
    }
}

// 0x00406020 world_index3_from_named: compara o prefixo do nome do sprite nomeado com a lista de
// mundos (0x431f94: Egyptus, Dungeon, Book, Moonbase, Safari, Armoury, Market) e devolve 3*indice.
int world_index3_from_named(int named_index) {
    int found = 0;
    for (int i = 0; i < 7; i++) {
        const char *w = g_world_names[i];
        if (std::strncmp(w, &g_named_names[named_index * 0xff], std::strlen(w)) == 0) found = i * 3;
    }
    return found;
}

}  // namespace hoh

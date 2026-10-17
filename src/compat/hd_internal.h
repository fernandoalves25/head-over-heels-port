// Partes internas do renderizador HD compartilhadas entre hd_core.cpp (gravacao, sem SDL, na
// hohcore) e hd_render.cpp (texturas e desenho na GPU, na hohplatform).
#pragma once

#include "compat/hd_render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd::detail {

struct Asset {
    std::string name;
    BITMAP *bmp = nullptr;
};
struct Tag {
    bool used = false;
    int asset = 0;
    bool flip = false;
    bool gray = false;          // copia em cinza (create_grayscale_bitmap)
    std::vector<int32_t> prov;  // vazio = o asset inteiro
    BITMAP *self = nullptr;     // o bitmap etiquetado (fallback 1x e verificacao)
};

enum Kind : uint8_t { OP_SPRITE, OP_FILL, OP_GROUP_BEGIN, OP_GROUP_END };
struct Snap {
    int w = 0, h = 0;
    std::vector<uint16_t> px;
    uint64_t hash = 0;
};
struct Op {
    Kind kind = OP_SPRITE;
    uint8_t flags = 0;
    uint8_t tint = kFull, alpha = kFull;
    int tag = 0, snap = -1;
    int sx = 0, sy = 0, w = 0, h = 0, dx = 0, dy = 0;
    uint16_t color = 0;
};
struct List {
    uint32_t serial = 0;
    std::vector<Op> ops;
    std::vector<Snap> snaps;
};

extern std::vector<Asset> g_assets;  // indice 0 = nenhum
extern std::vector<Tag> g_tags;      // indice 0 = nenhuma
// Chamado quando uma etiqueta deixa de existir (o renderizador solta a textura derivada dela).
extern void (*g_tag_released)(int tag);

inline List *list_of(const BITMAP *b) { return static_cast<List *>(b->hd_list); }
bool live(const List *l);  // lista do quadro aberto

}  // namespace hd::detail

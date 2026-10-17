// Partes internas do renderizador HD compartilhadas entre hd_core.cpp (gravacao, sem SDL, na
// hohcore) e hd_render.cpp (texturas e desenho na GPU, na hohplatform).
#pragma once

#include "compat/hd_render.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hd::detail {

using Pixels = std::shared_ptr<const std::vector<uint16_t>>;

struct Asset {
    std::string name;
    int w = 0, h = 0;  // tamanho 1x (o pacote tem w*k x h*k)
    int tag = 0;       // etiqueta do asset puro (pixels 1x)
};
// Etiqueta: descricao imutavel de um bitmap inteiro. Geometria (o asset inteiro, um recorte por
// pixel ou o asset esticado), espelho e transformacoes de pixel, mais os pixels 1x de quando foi
// criada (verificacao e textura de reserva quando o pacote nao tem a imagem).
struct Tag {
    int asset = 0;
    bool flip = false;
    std::vector<int32_t> prov;   // vazio = o asset inteiro (ou esticado)
    int stretch_w = 0, stretch_h = 0;
    std::vector<Xform> xf;       // em ordem
    int w = 0, h = 0;
    Pixels px;
    bool has_mask = false;       // algum pixel magenta
    bool plain() const { return !flip && prov.empty() && !stretch_w && xf.empty(); }
};

enum Kind : uint8_t { OP_SPRITE, OP_FILL, OP_GROUP_BEGIN, OP_GROUP_END, OP_SCALE };

// Copia dos pixels 1x de uma origem sem descricao. base: etiqueta de que estes pixels sao um
// recorte (o work_bmp de um objeto): onde o pixel e igual ao da etiqueta, vale o HD dela.
struct Snap {
    int w = 0, h = 0;
    std::vector<uint16_t> px;
    uint64_t hash = 0;
    bool has_mask = false;
    int base = 0;
};

struct ListData;
struct Op {
    Kind kind = OP_SPRITE;
    uint8_t flags = 0;
    uint8_t tint = kFull, alpha = kFull;
    int tag = 0, snap = -1, sub = -1;    // origem (OP_SPRITE): etiqueta, copia ou lista
    int sx = 0, sy = 0, sw = 0, sh = 0;  // retangulo na origem
    int dx = 0, dy = 0, dw = 0, dh = 0;  // no destino (dw x dh != sw x sh: esticado); OP_FILL/OP_SCALE: o retangulo
    int16_t cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // recorte no destino (x1/y1 exclusivos)
    uint16_t color = 0;
};
// Uma versao da lista de um bitmap. Depois de compartilhada (copiada para o quadro, usada como
// origem) nao muda mais: quem grava em cima faz uma copia (copy-on-write).
struct ListData {
    int w = 0, h = 0;
    std::vector<Op> ops;
    std::vector<std::shared_ptr<const Snap>> snaps;
    std::vector<std::shared_ptr<const ListData>> subs;
    uint64_t hash = 0;     // do conteudo (cache de texturas na GPU)
    bool opaque = false;   // nenhum pixel magenta possivel
    mutable int8_t inl = -1, disj = -1;  // cache de embutibilidade (-1 = nao calculado)
};
struct Rec {
    bool tracked = false;
    std::shared_ptr<ListData> cur;  // nulo = conteudo desconhecido
    int base = 0;                   // dica: ultima copia inteira veio desta etiqueta
};

extern std::vector<Asset> g_assets;  // indice 0 = nenhum
extern std::vector<Tag> g_tags;      // indice 0 = nenhuma

inline Rec *rec_of(const BITMAP *b) { return static_cast<Rec *>(b->hd_list); }
const ListData *list_of(const BITMAP *b);  // lista valida de um bitmap acompanhado (ou nulo)
// Transformacoes de pixel sobre texels RGBA8 (retos, alfa intacto).
void xform_rgba(const Xform &x, uint8_t *px, size_t n);

}  // namespace hd::detail

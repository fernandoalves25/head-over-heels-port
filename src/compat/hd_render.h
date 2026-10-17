// Renderizador HD (port; nao existe no original).
//
// O jogo continua desenhando o quadro em 640x480/RGB565 exatamente como antes (a logica, a oclusao
// e os efeitos nao mudam). Enquanto isso, cada desenho feito num bitmap acompanhado (o quadro e os
// bitmaps de trabalho das telas: painel do menu, linha de texto, logo, letreiro...) e gravado numa
// lista (origem, posicao, mascara, espelho, luz, transparencia...). Na hora de apresentar, se ha um
// pacote HD ligado, a lista do quadro e redesenhada na GPU na resolucao do pacote, trocando cada
// imagem do HoHOriginal.dat pela versao ampliada (<pasta hd>/<pacote>/<nome>.qoi).
//
// Origens de um desenho:
//   * etiqueta: o bitmap inteiro e uma imagem do arquivo ("asset", com o nome do export de
//     re/tools/hohfmt.py), ou derivada de uma: espelhada, recortada (ladrilho de piso, celula de
//     parede: de que pixel do asset veio cada pixel), esticada, e/ou com transformacoes de pixel
//     (cinza, silhueta, luz, tingimento HSL das fontes). Etiquetas sao imutaveis;
//   * lista: o bitmap de origem tambem e acompanhado; a versao atual da lista dele vai junto
//     (embutida na lista de destino quando da o mesmo resultado, senao desenhada numa textura
//     propria na GPU, ex.: linha de menu esticada, painel em fade);
//   * copia: os pixels 1x no momento do desenho (texto de depuracao, agua, efeitos), ampliada.
// Uma lista vale a partir de uma "ancora" (clear_to_color, desenho que cobre o bitmap inteiro,
// copia do quadro inteiro); escrita direta nos pixels sem gancho deixa a lista invalida (o bitmap
// volta a ser copiado em 1x) ate a proxima ancora.
// Objetos da sala nao passam pela lista generica: o jogo recorta cada um onde outro esta na
// frente (pinta magenta); no HD eles sao emitidos inteiros e ordenados por profundidade.
#pragma once

#include "compat/allegro.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace hd {

// ------------------------------------------------------------------ liga/desliga
// Gravacao ligada so no PC com pacotes (ou nos testes); desligada, todo gancho sai na hora (R36S).
void set_enabled(bool on);
bool enabled();

// ------------------------------------------------------------------ transformacoes de pixel
enum XformKind : uint8_t { XF_SCALE = 1, XF_GRAY, XF_SILHOUETTE, XF_TINT };
struct Xform {
    uint8_t kind = 0;
    uint8_t bgr = 0;                      // XF_TINT: variante tint_bitmap_bgr
    int16_t a = 0, b = 0, c = 0, d = 0;   // XF_SCALE: a = luz em 32 avos; XF_TINT: matiz, sat., lum., reforco
};
// O tingimento HSL e codigo do jogo (gfx_util.cpp): pixel 1x (identico a tint_bitmap) e texels RGBA8
// do pacote (n texels, alfa intacto).
void set_tint_functions(uint16_t (*px)(uint16_t c, const Xform &x), void (*rgba)(uint8_t *px, size_t n, const Xform &x));

// ------------------------------------------------------------------ etiquetas (bitmap inteiro)
void register_asset(BITMAP *bmp, const std::string &name);
// Asset de onde bmp veio (0 se nao for um asset direto).
int asset_of(const BITMAP *bmp);
// dst = src espelhado na horizontal (bitmap_make_h_flip).
void tag_flip_copy(BITMAP *dst, const BITMAP *src);
// dst = src em cinza ((r+g+b)/4, como create_grayscale_bitmap).
void tag_gray_copy(BITMAP *dst, const BITMAP *src);
// prov[y*w + x]: (sy << 16) | sx no asset; -1 = transparente; -2 = usar o pixel 1x do proprio bitmap.
void tag_derived(BITMAP *dst, int asset, std::vector<int32_t> prov);
// dst inteiro = stretch_sprite(src) (confere os pixels; false se nao bate).
bool tag_stretch_copy(BITMAP *dst, const BITMAP *src);
void forget(BITMAP *bmp);  // destroy_bitmap

// ------------------------------------------------------------------ gravacao
enum Flags {
    MASKED = 1,   // magenta e transparente (draw_sprite / masked_blit)
    HFLIP = 2,    // espelhado na horizontal
    ADD = 4,      // soma (fblend_add)
    LINEAR = 8,   // copia 1x ampliada com filtro (sombras, agua)
    OPAQUE = 16,  // blit sem mascara (magenta copiado como transparente)
};
constexpr int kFull = 32;  // luz/alfa em 32 avos, como o jogo

// Bitmap com lista propria (o quadro e os bitmaps de trabalho das telas).
void track(BITMAP *bmp);
// Comeco do desenho de um quadro: acompanha target e marca o estado do jogo (rotulo da verificacao).
void frame_begin(BITMAP *target, int label = -1);
bool recording(const BITMAP *dst);  // lista valida gravando agora
// Antes de um desenho por gancho: true se dst e acompanhado (grave com op_*). Desenho num bitmap
// etiquetado tira a etiqueta (ela vira a ancora da lista, se acompanhado).
bool touch(BITMAP *dst);

void op_sprite(BITMAP *dst, BITMAP *src, int sx, int sy, int w, int h, int dx, int dy, int flags,
               int tint = kFull, int alpha = kFull);
// stretch_sprite: src inteiro esticado para dw x dh em (dx, dy).
void op_stretch(BITMAP *dst, BITMAP *src, int dx, int dy, int dw, int dh, int flags);
void op_fill(BITMAP *dst, int x1, int y1, int x2, int y2, int color);  // inclusivo, como rectfill
void op_clear(BITMAP *dst, int color);
// blit (antes da copia): quadro inteiro de um bitmap com lista = a lista vai junto.
void on_blit(BITMAP *s, BITMAP *d, int sx, int sy, int dx, int dy, int w, int h);
// Depois de tint_bitmap/make_silhouette: dst (regiao) = T(src); copy_mask como tint_bitmap.
void tinted(BITMAP *src, BITMAP *dst, const Xform &x, int sx, int sy, int dx, int dy, int w, int h, bool copy_mask);
// Depois de uma escrita direta nos pixels (agua, efeitos): a regiao vira uma copia 1x.
void wrote(BITMAP *bmp, int x, int y, int w, int h, int flags = 0);
// Grupo de sombras sem escurecer duas vezes (SHADOWS COMPLEX): uniao das silhuetas, depois
// escurece com alpha/32.
void group_begin(BITMAP *dst, int alpha);
void group_end(BITMAP *dst);

// Enquanto vivo, desenhos no quadro nao sao gravados nem invalidam a lista (o equivalente HD e
// emitido a parte, ex.: objetos ordenados).
struct Suppress {
    Suppress();
    ~Suppress();
};
// Evita gravar duas vezes quando uma primitiva chama outra (fblend_trans -> draw_sprite).
struct Nest {
    Nest();
    ~Nest();
    bool outer;
};

// ------------------------------------------------------------------ pacotes e apresentacao
void attach(SDL_Renderer *r);  // depois de criar o renderer; detach() antes de destrui-lo
void detach();
void scan_packs(const std::string &dir);
const std::vector<std::string> &packs();
int current_pack();             // -1 = desligado
void select_pack(int index);    // -1 desliga
bool frame_ready(const BITMAP *bmp);
// Texels por pixel 1x do pacote atual (4 nos pacotes do Real-ESRGAN): o quadro HD e montado nessa
// resolucao (640*k x 480*k), sem filtro, e so entao reduzido para a janela.
int render_scale();
// Redesenha a lista de bmp no alvo atual do renderer (target), de tamanho out_w x out_h.
void render(SDL_Renderer *r, SDL_Texture *target, const BITMAP *bmp, int out_w, int out_h);
// Diagnostico (HOH_HD_VERIFY=arquivo): refaz a lista em 1x e conta os pixels diferentes do quadro.
void verify(const BITMAP *bmp);
// Mesma comparacao, agora: pixels diferentes (-1 sem lista); ops/copias da lista em *ops/*snaps.
int verify_diff(const BITMAP *bmp, size_t *ops = nullptr, size_t *snaps = nullptr, BITMAP *replay = nullptr);

}  // namespace hd

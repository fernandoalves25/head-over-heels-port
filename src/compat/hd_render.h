// Renderizador HD (port; nao existe no original).
//
// O jogo continua desenhando o quadro em 640x480/RGB565 exatamente como antes (a logica, a oclusao
// e os efeitos nao mudam). Enquanto isso, cada desenho feito no bitmap do quadro e gravado numa
// lista (sprite de origem, posicao, mascara, espelho, luz, transparencia...). Na hora de apresentar,
// se ha um pacote HD ligado, a lista e redesenhada na GPU na resolucao da janela, trocando cada
// imagem do HoHOriginal.dat pela versao ampliada do pacote (<pasta hd>/<pacote>/<nome>.qoi).
//
// Origens:
//   * imagem do arquivo ("asset", registrada com o nome do export de re/tools/hohfmt.py);
//   * copia espelhada de uma etiquetada (tag_flip_copy);
//   * derivada (ladrilho de piso, celula de parede): para cada pixel, de que pixel do asset ele
//     veio (tag_derived); o ladrilho HD e montado com os blocos k x k correspondentes;
//   * qualquer outra (sombras recebidas, textos, icones gerados): copia dos pixels 1x no momento
//     do desenho, ampliada sem filtro.
// Objetos da sala nao passam pela lista generica: o jogo recorta cada um onde outro esta na
// frente (pinta magenta); no HD eles sao emitidos inteiros e ordenados por profundidade.
#pragma once

#include "compat/allegro.h"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace hd {

// ------------------------------------------------------------------ origens (etiquetas)
void register_asset(BITMAP *bmp, const std::string &name);
// Asset de onde bmp veio (0 se nao for um asset direto).
int asset_of(const BITMAP *bmp);
// dst = src espelhado na horizontal (bitmap_make_h_flip).
void tag_flip_copy(BITMAP *dst, const BITMAP *src);
// dst = src em cinza ((r+g+b)/4, como create_grayscale_bitmap).
void tag_gray_copy(BITMAP *dst, const BITMAP *src);
// prov[y*w + x]: (sy << 16) | sx no asset; -1 = transparente; -2 = usar o pixel 1x do proprio bitmap.
void tag_derived(BITMAP *dst, int asset, std::vector<int32_t> prov);
void forget(BITMAP *bmp);  // destroy_bitmap

// ------------------------------------------------------------------ gravacao
enum Flags {
    MASKED = 1,   // magenta e transparente (draw_sprite / masked_blit)
    HFLIP = 2,    // espelhado na horizontal
    ADD = 4,      // soma (fblend_add)
    LINEAR = 8,   // copia 1x ampliada com filtro (sombras)
    OPAQUE = 16,  // blit sem mascara: sempre copia 1x, magenta incluido
};
constexpr int kFull = 32;  // luz/alfa em 32 avos, como o jogo

void frame_begin(BITMAP *target);  // comeca a lista do quadro (render_game_frame)
bool recording(const BITMAP *dst);
// Algum desenho no bitmap: grava se ha lista aberta nele; desenho fora do quadro invalida a lista.
// Retorna true se a operacao deve ser gravada (e o chamador grava com op_*).
bool touch(const BITMAP *dst);

void op_sprite(BITMAP *dst, BITMAP *src, int sx, int sy, int w, int h, int dx, int dy, int flags,
               int tint = kFull, int alpha = kFull);
void op_fill(BITMAP *dst, int x1, int y1, int x2, int y2, int color);  // inclusivo, como rectfill
void op_clear(BITMAP *dst, int color);
void list_copy(const BITMAP *from, BITMAP *to);  // blit do quadro inteiro
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
void frame_done();              // fecha o quadro (todo present chama)
// Redesenha a lista de bmp no alvo atual do renderer (target), de tamanho out_w x out_h.
void render(SDL_Renderer *r, SDL_Texture *target, const BITMAP *bmp, int out_w, int out_h);
// Diagnostico (HOH_HD_VERIFY=arquivo): refaz a lista em 1x e conta os pixels diferentes do quadro.
void verify(const BITMAP *bmp);
// Mesma comparacao, agora: pixels diferentes (-1 sem lista); ops/copias da lista em *ops/*snaps.
int verify_diff(const BITMAP *bmp, size_t *ops = nullptr, size_t *snaps = nullptr, BITMAP *replay = nullptr);

}  // namespace hd

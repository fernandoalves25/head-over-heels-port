// Port: modo responsivo (nao existe no original).
//
// O jogo continua desenhando o quadro de 640x480 (o "miolo") exatamente como antes, e a logica nao
// muda. A imagem que vai para a janela e uma tela de pintura com a proporcao da janela (lado menor
// fixo: 480 de altura em tela larga, 640 de largura em pe), com o miolo no centro:
//   * no jogo, a sala e o HUD sao desenhados direto na tela larga (a sala no centro, a camera usando
//     a largura extra, as paredes do HUD continuando ate as bordas); o miolo e copiado de volta para
//     g_backbuffer, que as transicoes e a pausa usam;
//   * transicoes e pausa escurecem a ultima tela larga como o jogo escurece o miolo;
//   * abertura, menus, mapa e resultado: fundo desfocado e escurecido do proprio quadro nas laterais.
// Desligado (port.cfg "responsive = 0", F7) ou no R36S, a tela e o proprio miolo e nada disto roda.
#pragma once

#include "compat/allegro.h"

namespace hoh {

struct CanvasGeom {
    int w = 640, h = 480;  // tela de pintura
    int ox = 0, oy = 0;    // posicao do miolo no jogo (em pe, encostado na base: sala e HUD juntos)
    int oy_menu = 0;       // nas outras telas o miolo fica centrado
};
extern CanvasGeom g_canvas_geom;

// Mede a janela e (re)cria os bitmaps da tela larga se o tamanho mudou. Chamar antes de desenhar.
void canvas_update();
bool canvas_active();
// Quadro do jogo na tela larga (no lugar de render_game_frame(g_backbuffer)).
void render_game_frame_wide();
// Apresenta o quadro: monta a tela larga a partir do miolo (target) e a mostra; inativo, mostra target.
void present_frame(BITMAP *target);
// game_render marcou um relampago neste quadro (o miolo inteiro branco).
void canvas_flash();
void free_canvas();

}  // namespace hoh

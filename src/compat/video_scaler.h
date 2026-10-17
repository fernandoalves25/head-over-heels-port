// Ampliacao do quadro 640x480 do jogo para a resolucao da janela/tela (port; nao existe no original).
#pragma once

#include "compat/allegro.h"

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class ScaleMode {
    Sharp,     // amplia por inteiro (vizinho mais proximo) e ajusta suave ao tamanho final: nitido e uniforme
    Smooth2x,  // suaviza bordas (algoritmo EPX/Scale2x) e filtra: contornos arredondados
    Smooth3x,  // idem com Scale3x (AdvMAME3x)
    Nearest,   // pixels crus esticados ate a tela
    Integer,   // so multiplos inteiros, com tarjas: pixel perfeito
    Count
};

const char *scale_mode_name(ScaleMode m);
bool scale_mode_from_name(const char *name, ScaleMode *out);

class VideoScaler {
public:
    bool init(SDL_Renderer *r, int w, int h);
    void shutdown();
    // Limpa a tela, desenha o quadro com o modo pedido (mantendo 4:3) e apresenta.
    void present(const BITMAP *frame, ScaleMode mode);
    // Mostra de novo o ultimo quadro, sem ampliar nem enviar a textura (com vsync: mais um retraco).
    void present_again();
    // Quadro desenhado por fora (renderizador HD): draw(renderer, alvo, w, h) desenha num alvo de
    // w x h (a resolucao nativa do pacote), que depois e reduzido com filtro para a area 4:3 da janela.
    void present_custom(int w, int h, const std::function<void(SDL_Renderer *, SDL_Texture *, int, int)> &draw);
    // SDL_GetPerformanceCounter() logo antes do ultimo SDL_RenderPresent (fim do trabalho do quadro).
    uint64_t submit_time() const { return submit_time_; }
    // Teste: salva em BMP o quadro final (na resolucao de saida) no proximo present.
    void capture_next(const char *path) { capture_path_ = path ? path : ""; }

private:
    SDL_Texture *make_texture(int w, int h, int access, bool linear);
    void fit(int out_w, int out_h, double *scale, SDL_Rect *dst) const;
    void draw_final(SDL_Texture *t, const SDL_Rect &dst);  // copia para a tela e guarda para present_again

    SDL_Renderer *r_ = nullptr;
    int w_ = 0, h_ = 0;
    SDL_Texture *src_ = nullptr;        // w x h, filtro vizinho mais proximo
    SDL_Texture *prescaled_ = nullptr;  // alvo de render (w*k x h*k), amostrado com filtro linear
    int prescale_k_ = 0;
    bool targets_ok_ = true;
    SDL_Texture *epx_ = nullptr;        // saida do Scale2x/3x, filtro linear
    int epx_k_ = 0;
    std::vector<uint16_t> epx_buf_;
    std::string capture_path_;
    ScaleMode current_mode_ = ScaleMode::Sharp;
    SDL_Texture *custom_ = nullptr;    // alvo do present_custom
    bool hd_frame_ = false;            // finish() de um present_custom (rotulo do diagnostico)
    int custom_w_ = 0, custom_h_ = 0;
    SDL_Texture *last_tex_ = nullptr;  // ultima textura copiada para a tela
    SDL_Rect last_dst_ = {0, 0, 0, 0};
    uint64_t submit_time_ = 0;
    void finish();
};

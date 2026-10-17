#include "compat/video_scaler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const char *kNames[] = {"sharp", "smooth2x", "smooth3x", "nearest", "integer"};

// Scale2x / EPX:   A
//                C P B   ->  E0 E1
//                  D         E2 E3
void scale2x(const uint16_t *src, int w, int h, uint16_t *dst) {
    const int ow = w * 2;
    for (int y = 0; y < h; y++) {
        const uint16_t *row = src + y * w;
        const uint16_t *up = y > 0 ? row - w : row;
        const uint16_t *dn = y < h - 1 ? row + w : row;
        uint16_t *o0 = dst + (y * 2) * ow, *o1 = o0 + ow;
        for (int x = 0; x < w; x++) {
            uint16_t P = row[x], A = up[x], D = dn[x];
            uint16_t C = x > 0 ? row[x - 1] : P, B = x < w - 1 ? row[x + 1] : P;
            uint16_t e0 = P, e1 = P, e2 = P, e3 = P;
            if (C == A && C != D && A != B) e0 = A;
            if (A == B && A != C && B != D) e1 = B;
            if (D == C && D != B && C != A) e2 = C;
            if (B == D && B != A && D != C) e3 = D;
            o0[x * 2] = e0; o0[x * 2 + 1] = e1;
            o1[x * 2] = e2; o1[x * 2 + 1] = e3;
        }
    }
}

// Scale3x / AdvMAME3x:  A B C
//                       D E F   ->  3x3
//                       G H I
void scale3x(const uint16_t *src, int w, int h, uint16_t *dst) {
    const int ow = w * 3;
    for (int y = 0; y < h; y++) {
        const uint16_t *row = src + y * w;
        const uint16_t *up = y > 0 ? row - w : row;
        const uint16_t *dn = y < h - 1 ? row + w : row;
        uint16_t *o0 = dst + (y * 3) * ow, *o1 = o0 + ow, *o2 = o1 + ow;
        for (int x = 0; x < w; x++) {
            int xl = x > 0 ? x - 1 : x, xr = x < w - 1 ? x + 1 : x;
            uint16_t A = up[xl], B = up[x], C = up[xr], D = row[xl], E = row[x], F = row[xr];
            uint16_t G = dn[xl], H = dn[x], I = dn[xr];
            uint16_t e[9] = {E, E, E, E, E, E, E, E, E};
            if (B != H && D != F) {
                e[0] = D == B ? D : E;
                e[1] = (D == B && E != C) || (B == F && E != A) ? B : E;
                e[2] = B == F ? F : E;
                e[3] = (D == B && E != G) || (D == H && E != A) ? D : E;
                e[5] = (B == F && E != I) || (H == F && E != C) ? F : E;
                e[6] = D == H ? D : E;
                e[7] = (D == H && E != I) || (H == F && E != G) ? H : E;
                e[8] = H == F ? F : E;
            }
            for (int k = 0; k < 3; k++) {
                o0[x * 3 + k] = e[k];
                o1[x * 3 + k] = e[3 + k];
                o2[x * 3 + k] = e[6 + k];
            }
        }
    }
}

}  // namespace

const char *scale_mode_name(ScaleMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < static_cast<int>(ScaleMode::Count)) ? kNames[i] : "sharp";
}

bool scale_mode_from_name(const char *name, ScaleMode *out) {
    for (int i = 0; i < static_cast<int>(ScaleMode::Count); i++)
        if (name && std::strcmp(name, kNames[i]) == 0) {
            *out = static_cast<ScaleMode>(i);
            return true;
        }
    return false;
}

SDL_Texture *VideoScaler::make_texture(int w, int h, int access, bool linear) {
    // O filtro de cada textura vem da dica no momento da criacao (funciona em qualquer SDL2).
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "linear" : "nearest");
    Uint32 fmt = access == SDL_TEXTUREACCESS_TARGET ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB565;
    return SDL_CreateTexture(r_, fmt, access, w, h);
}

bool VideoScaler::init(SDL_Renderer *r, int w, int h) {
    shutdown();
    r_ = r;
    w_ = w;
    h_ = h;
    src_ = make_texture(w, h, SDL_TEXTUREACCESS_STREAMING, false);
    targets_ok_ = SDL_RenderTargetSupported(r) == SDL_TRUE;
    return src_ != nullptr;
}

void VideoScaler::shutdown() {
    for (SDL_Texture **t : {&src_, &prescaled_, &epx_, &custom_})
        if (*t) {
            SDL_DestroyTexture(*t);
            *t = nullptr;
        }
    prescale_k_ = epx_k_ = 0;
    custom_w_ = custom_h_ = 0;
    last_tex_ = nullptr;
}

void VideoScaler::fit(int out_w, int out_h, double *scale, SDL_Rect *dst) const {
    double s = std::min(static_cast<double>(out_w) / w_, static_cast<double>(out_h) / h_);
    int dw = static_cast<int>(std::lround(w_ * s)), dh = static_cast<int>(std::lround(h_ * s));
    *scale = s;
    *dst = {(out_w - dw) / 2, (out_h - dh) / 2, dw, dh};
}

namespace {
// Diagnostico: HOH_PROFILE=<arquivo> grava a media de cada etapa a cada 60 quadros.
struct Prof {
    const char *path = std::getenv("HOH_PROFILE");
    double t[4] = {0, 0, 0, 0};  // ampliacao na CPU, envio da textura, desenho, present (inclui vsync)
    int n = 0;
    uint64_t mark = 0;
    void start() { mark = SDL_GetPerformanceCounter(); }
    void lap(int i) {
        uint64_t now = SDL_GetPerformanceCounter();
        t[i] += (now - mark) * 1000.0 / SDL_GetPerformanceFrequency();
        mark = now;
    }
    void frame(const char *mode) {
        if (!path || ++n < 60) return;
        if (FILE *f = std::fopen(path, "a")) {
            std::fprintf(f, "[%s] cpu %.2f  upload %.2f  draw %.2f  present %.2f ms" "\n", mode, t[0] / n, t[1] / n, t[2] / n, t[3] / n);
            std::fclose(f);
        }
        t[0] = t[1] = t[2] = t[3] = 0;
        n = 0;
    }
} g_prof;
}  // namespace

void VideoScaler::present(const BITMAP *frame, ScaleMode mode) {
    if (!r_ || !src_ || !frame) return;
    g_prof.start();
    current_mode_ = mode;
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(r_, &ow, &oh);
    double s;
    SDL_Rect dst;
    fit(ow, oh, &s, &dst);

    SDL_SetRenderTarget(r_, nullptr);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);

    if (mode == ScaleMode::Smooth2x || mode == ScaleMode::Smooth3x) {
        int k = mode == ScaleMode::Smooth2x ? 2 : 3;
        if (epx_k_ != k) {
            if (epx_) SDL_DestroyTexture(epx_);
            epx_ = make_texture(w_ * k, h_ * k, SDL_TEXTUREACCESS_STREAMING, true);
            epx_k_ = epx_ ? k : 0;
            epx_buf_.assign(static_cast<size_t>(w_ * k) * h_ * k, 0);
        }
        if (epx_) {
            if (k == 2) scale2x(frame->dat, w_, h_, epx_buf_.data());
            else scale3x(frame->dat, w_, h_, epx_buf_.data());
            g_prof.lap(0);
            SDL_UpdateTexture(epx_, nullptr, epx_buf_.data(), w_ * k * static_cast<int>(sizeof(uint16_t)));
            g_prof.lap(1);
            draw_final(epx_, dst);
            g_prof.lap(2);
            finish();
            return;
        }
        mode = ScaleMode::Sharp;
    }

    g_prof.lap(0);
    SDL_UpdateTexture(src_, nullptr, frame->dat, frame->w * static_cast<int>(sizeof(uint16_t)));
    g_prof.lap(1);
    if (mode == ScaleMode::Integer) {
        int k = std::max(1, static_cast<int>(std::floor(s)));
        SDL_Rect d = {(ow - w_ * k) / 2, (oh - h_ * k) / 2, w_ * k, h_ * k};
        draw_final(src_, d);
    } else if (mode == ScaleMode::Sharp && targets_ok_) {
        // "sharp bilinear": vizinho mais proximo ate o multiplo inteiro logo acima da escala
        // final e filtro linear no resto; nenhum pixel fica maior que o vizinho.
        int k = std::clamp(static_cast<int>(std::ceil(s - 1e-6)), 1, 8);
        if (prescale_k_ != k) {
            if (prescaled_) SDL_DestroyTexture(prescaled_);
            prescaled_ = make_texture(w_ * k, h_ * k, SDL_TEXTUREACCESS_TARGET, true);
            prescale_k_ = prescaled_ ? k : 0;
            if (!prescaled_) targets_ok_ = false;
        }
        if (prescaled_ && SDL_SetRenderTarget(r_, prescaled_) == 0) {
            SDL_RenderCopy(r_, src_, nullptr, nullptr);
            SDL_SetRenderTarget(r_, nullptr);
            draw_final(prescaled_, dst);
        } else {
            SDL_SetRenderTarget(r_, nullptr);
            draw_final(src_, dst);
        }
    } else {
        draw_final(src_, dst);
    }
    finish();
}

void VideoScaler::finish() {
    if (!capture_path_.empty()) {
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(r_, &ow, &oh);
        std::vector<uint32_t> px(static_cast<size_t>(ow) * oh);
        if (SDL_RenderReadPixels(r_, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), ow * 4) == 0) {
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), ow, oh, 32, ow * 4, SDL_PIXELFORMAT_ARGB8888);
            if (s) {
                SDL_SaveBMP(s, capture_path_.c_str());
                SDL_FreeSurface(s);
            }
        }
        capture_path_.clear();
    }
    if (current_mode_ != ScaleMode::Smooth2x && current_mode_ != ScaleMode::Smooth3x) g_prof.lap(2);
    submit_time_ = SDL_GetPerformanceCounter();
    SDL_RenderPresent(r_);
    g_prof.lap(3);
    g_prof.frame(hd_frame_ ? "hd" : scale_mode_name(current_mode_));
}

void VideoScaler::draw_final(SDL_Texture *t, const SDL_Rect &dst) {
    SDL_RenderCopy(r_, t, nullptr, &dst);
    last_tex_ = t;
    last_dst_ = dst;
}

void VideoScaler::present_again() {
    if (!r_ || !last_tex_) return;
    // o conteudo do back buffer depois de um present e indefinido: redesenha a mesma textura
    SDL_SetRenderTarget(r_, nullptr);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    SDL_RenderCopy(r_, last_tex_, nullptr, &last_dst_);
    SDL_RenderPresent(r_);
}

void VideoScaler::present_custom(int w, int h, const std::function<void(SDL_Renderer *, SDL_Texture *, int, int)> &draw) {
    if (!r_ || w <= 0 || h <= 0) return;
    g_prof.start();
    int ow = 0, oh = 0;
    SDL_GetRendererOutputSize(r_, &ow, &oh);
    double s;
    SDL_Rect dst;
    fit(ow, oh, &s, &dst);
    if (dst.w <= 0 || dst.h <= 0) return;
    if (!custom_ || custom_w_ != w || custom_h_ != h) {
        if (custom_) SDL_DestroyTexture(custom_);
        custom_ = make_texture(w, h, SDL_TEXTUREACCESS_TARGET, true);  // reduzido com filtro na tela
        custom_w_ = custom_ ? w : 0;
        custom_h_ = custom_ ? h : 0;
    }
    if (!custom_ || SDL_SetRenderTarget(r_, custom_) != 0) return;
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    draw(r_, custom_, w, h);
    g_prof.lap(0);
    SDL_SetRenderTarget(r_, nullptr);
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    SDL_SetTextureBlendMode(custom_, SDL_BLENDMODE_NONE);
    draw_final(custom_, dst);
    g_prof.lap(2);
    hd_frame_ = true;
    finish();
    hd_frame_ = false;
}

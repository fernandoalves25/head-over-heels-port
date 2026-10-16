// Partes da biblioteca C do MSVC 6 cujo comportamento o jogo depende.
//
// rand(): o HoH.exe usa o rand() do msvcrt.dll (RAND_MAX = 0x7fff, LCG 214013/2531011).
// O rand() do glibc/MinGW tem outra sequencia e outro RAND_MAX, o que muda expressoes como
// `rand() / 800`. Dentro do namespace hoh, `rand()`/`srand()` resolvem para estes.
#pragma once

#include <cstdint>

namespace hoh {

constexpr int MSVC_RAND_MAX = 0x7fff;

inline uint32_t &msvc_rand_state() {
    static uint32_t holdrand = 1;
    return holdrand;
}
inline void srand(unsigned seed) { msvc_rand_state() = seed; }
inline int rand() {
    uint32_t &s = msvc_rand_state();
    s = s * 214013u + 2531011u;
    return static_cast<int>((s >> 16) & 0x7fff);
}

}  // namespace hoh

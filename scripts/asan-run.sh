#!/usr/bin/env bash
# Compila o jogo com AddressSanitizer e roda sem janela com teclas roteirizadas (HOH_KEYS),
# para achar crashes/acessos invalidos com a pilha exata.
# Uso: HOH_KEYS="..." scripts/asan-run.sh [pasta_do_jogo]
cd "$(dirname "$0")/.."
DATA="${1:-/mnt/f/headover/HeadOverHeels}"
mkdir -p build
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" > build/asan.log 2>&1 || { tail build/asan.log; exit 1; }
cmake --build build/asan -j"$(nproc)" --target hoh >> build/asan.log 2>&1 || { tail -20 build/asan.log; exit 1; }
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export HOH_AUTOQUIT_MS="${HOH_AUTOQUIT_MS:-15000}"
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=0" UBSAN_OPTIONS="print_stacktrace=1"
timeout 120 ./build/asan/hoh --data "$DATA" 2>&1 | head -80
echo "exit=${PIPESTATUS[0]}"

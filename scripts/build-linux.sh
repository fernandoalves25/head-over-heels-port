#!/usr/bin/env bash
# Compila o port para Linux (nativo / WSL) e roda o teste dos dados.
# Uso: scripts/build-linux.sh [pasta_do_jogo] [--wait]
#   --wait: espera (ate 1 h) os pacotes libsdl2-dev e libvorbis-dev aparecerem.
set -u
cd "$(dirname "$0")/.."
DATA="${1:-/mnt/f/headover/HeadOverHeels}"

if [ "${2:-}" = "--wait" ]; then
  for _ in $(seq 1 360); do
    pkg-config --exists sdl2 vorbisfile && break
    sleep 10
  done
fi
if ! pkg-config --exists sdl2 vorbisfile; then
  echo "FALTAM PACOTES: sudo apt install -y cmake ninja-build libsdl2-dev libvorbis-dev"
  exit 3
fi
echo "sdl2 $(pkg-config --modversion sdl2), vorbisfile $(pkg-config --modversion vorbisfile)"

GEN=""
command -v ninja >/dev/null && GEN="-G Ninja"
mkdir -p build && cmake -S . -B build/linux $GEN -DHOH_DATA_DIR="$DATA" > build/linux.log 2>&1 || { tail -20 build/linux.log; exit 1; }
cmake --build build/linux -j"$(nproc)" >> build/linux.log 2>&1
rc=$?
grep -E "error|warning:" build/linux.log | head -40
[ $rc -eq 0 ] || { echo "BUILD FALHOU"; tail -20 build/linux.log; exit 1; }
echo "BUILD OK: $(ls -la build/linux/hoh)"
./build/linux/hoh-datatest "$DATA" | tail -1

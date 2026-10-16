#!/usr/bin/env bash
# Compila o port para Windows x64 (MinGW-w64, no Linux/WSL) e monta dist/windows/.
#
# Dependencias (baixadas uma vez em $DEPS/dl, ver README):
#   SDL2-devel-2.32.10-mingw.tar.gz   github.com/libsdl-org/SDL (release oficial)
#   libogg-1.3.5.tar.gz, libvorbis-1.3.7.tar.gz   downloads.xiph.org (SHA-256 conferido)
# Uso: scripts/build-windows.sh [pasta_deps]   (padrao: /mnt/f/headover/tools/deps)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
DEPS="${1:-/mnt/f/headover/tools/deps}"
PREFIX="$DEPS/win64"
TC="$ROOT/cmake/mingw-w64-x86_64.cmake"
WORK=/tmp/hoh-windeps
SDL_VER=2.32.10

if [ ! -f "$PREFIX/.done" ]; then
  echo "== montando dependencias em $PREFIX"
  rm -rf "$WORK" "$PREFIX" && mkdir -p "$WORK" "$PREFIX"
  (cd "$DEPS/dl" && sha256sum -c --ignore-missing <(grep -h "libogg-1.3.5.tar.gz\|libvorbis-1.3.7.tar.gz" ogg.SHA256SUMS vorbis.SHA256SUMS))
  tar -xzf "$DEPS/dl/SDL2-devel-$SDL_VER-mingw.tar.gz" -C "$WORK"
  cp -r "$WORK/SDL2-$SDL_VER/x86_64-w64-mingw32/." "$PREFIX/"
  for lib in libogg-1.3.5 libvorbis-1.3.7; do
    tar -xzf "$DEPS/dl/$lib.tar.gz" -C "$WORK"
    cmake -S "$WORK/$lib" -B "$WORK/$lib/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" -DHOH_WIN_DEPS="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=OFF \
      -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF > "$WORK/$lib.log" 2>&1 || { tail -20 "$WORK/$lib.log"; exit 1; }
    cmake --build "$WORK/$lib/build" -j"$(nproc)" >> "$WORK/$lib.log" 2>&1 || { tail -20 "$WORK/$lib.log"; exit 1; }
    cmake --install "$WORK/$lib/build" >> "$WORK/$lib.log" 2>&1
    echo "ok $lib"
  done
  touch "$PREFIX/.done"
fi

echo "== compilando hoh.exe"
cmake -S . -B build/windows -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" -DHOH_WIN_DEPS="$PREFIX" \
  -DCMAKE_BUILD_TYPE=Release > build/windows.log 2>&1 || { tail -30 build/windows.log; exit 1; }
cmake --build build/windows -j"$(nproc)" >> build/windows.log 2>&1 || { grep -E "error" build/windows.log | head -30; exit 1; }
grep -E "warning:" build/windows.log | grep -v "Wunused-result\|format-truncation" | head -10 || true

DIST="$ROOT/dist/windows"
rm -rf "$DIST" && mkdir -p "$DIST"
cp build/windows/hoh.exe build/windows/hoh-viewer.exe "$PREFIX/bin/SDL2.dll" "$DIST/"
cp packaging/windows/* "$DIST/"
x86_64-w64-mingw32-strip "$DIST/hoh.exe" "$DIST/hoh-viewer.exe"
echo "== DLLs importadas por hoh.exe:"
x86_64-w64-mingw32-objdump -p "$DIST/hoh.exe" | grep "DLL Name" | sort -u
ls -la "$DIST"

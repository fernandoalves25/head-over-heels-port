#!/usr/bin/env bash
# Compila o port para o R36S (aarch64, ArkOS glibc 2.30) com o Zig e monta dist/r36s/ports/.
#
# Dependencias em $DEPS/dl: libogg-1.3.5.tar.gz, libvorbis-1.3.7.tar.gz (downloads.xiph.org,
# SHA-256 conferido) e o Zig em $ZIG (ziglang.org, 0.14.1). O SDL2 nao e embarcado: o binario usa
# o libSDL2-2.0.so.0 do proprio aparelho (com KMSDRM); para linkar, gera-se um stub com o mesmo
# soname a partir dos headers do SDL2 instalados no WSL.
# Uso: scripts/build-r36s.sh [pasta_deps]
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
DEPS="${1:-/mnt/f/headover/tools/deps}"
ZIG="${ZIG:-$HOME/tools/zig-x86_64-linux-0.14.1/zig}"
TARGET=aarch64-linux-gnu.2.28
PREFIX="$HOME/.cache/hoh-r36s-deps"   # no ext4 do WSL (mais rapido que /mnt/f)
WRAP="$PREFIX/zigwrap"
TC="$ROOT/cmake/zig-aarch64-linux.cmake"
WORK=/tmp/hoh-r36sdeps
export HOH_ZIG_WRAPPERS="$WRAP" HOH_R36S_DEPS="$PREFIX"

[ -x "$ZIG" ] || { echo "Zig nao encontrado em $ZIG"; exit 1; }

if [ ! -f "$PREFIX/.done" ]; then
  echo "== montando dependencias aarch64 em $PREFIX"
  rm -rf "$WORK" "$PREFIX" && mkdir -p "$WORK" "$WRAP" "$PREFIX/include" "$PREFIX/lib"
  printf '#!/bin/sh\nexec "%s" cc -target %s "$@"\n' "$ZIG" "$TARGET" > "$WRAP/cc"
  printf '#!/bin/sh\nexec "%s" c++ -target %s "$@"\n' "$ZIG" "$TARGET" > "$WRAP/c++"
  printf '#!/bin/sh\nexec "%s" ar "$@"\n' "$ZIG" > "$WRAP/ar"
  printf '#!/bin/sh\nexec "%s" ranlib "$@"\n' "$ZIG" > "$WRAP/ranlib"
  chmod +x "$WRAP"/*
  (cd "$DEPS/dl" && sha256sum -c --ignore-missing <(grep -h "libogg-1.3.5.tar.gz\|libvorbis-1.3.7.tar.gz" ogg.SHA256SUMS vorbis.SHA256SUMS))
  for lib in libogg-1.3.5 libvorbis-1.3.7; do
    tar -xzf "$DEPS/dl/$lib.tar.gz" -C "$WORK"
    cmake -S "$WORK/$lib" -B "$WORK/$lib/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=OFF \
      -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF > "$WORK/$lib.log" 2>&1 || { tail -20 "$WORK/$lib.log"; exit 1; }
    cmake --build "$WORK/$lib/build" -j"$(nproc)" >> "$WORK/$lib.log" 2>&1 || { tail -20 "$WORK/$lib.log"; exit 1; }
    cmake --install "$WORK/$lib/build" >> "$WORK/$lib.log" 2>&1
    echo "ok $lib"
  done
  # Headers do SDL2 (os do WSL; a config Linux serve para aarch64) + stub com o soname real.
  cp -r /usr/include/SDL2 "$PREFIX/include/"
  # config Linux x86_64 sem os intrinsics de x86 (o resto vale igual para aarch64)
  grep -vE "HAVE_(IMM|MM|X|E|P|S|T|N|W)?[A-Z]*INTRIN_H|SDL_ALTIVEC" /usr/include/x86_64-linux-gnu/SDL2/_real_SDL_config.h     > "$PREFIX/include/SDL2/_real_SDL_config.h"
  python3 - /usr/include/SDL2 > "$WORK/sdl_stub.c" <<'PY'
import glob, re, sys
names = set()
for h in glob.glob(sys.argv[1] + "/*.h"):
    names.update(re.findall(r"SDLCALL\s+(SDL_\w+)\s*\(", open(h, encoding="latin1").read()))
for n in sorted(names):
    print(f"void {n}(void) {{}}")
PY
  "$WRAP/cc" -shared -fPIC -Wl,-soname,libSDL2-2.0.so.0 -o "$PREFIX/lib/libSDL2-2.0.so" "$WORK/sdl_stub.c"
  echo "ok stub SDL2 ($(wc -l < "$WORK/sdl_stub.c") simbolos)"
  touch "$PREFIX/.done"
fi

echo "== compilando hoh (aarch64)"
cmake -S . -B build/r36s -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_BUILD_TYPE=Release \
  -DHOH_STATIC_VORBIS=ON -DHOH_SDL2_INCLUDE_DIR="$PREFIX/include/SDL2" \
  -DHOH_SDL2_LIBRARY="$PREFIX/lib/libSDL2-2.0.so" > build/r36s.log 2>&1 || { tail -30 build/r36s.log; exit 1; }
cmake --build build/r36s -j"$(nproc)" --target hoh >> build/r36s.log 2>&1 || { grep -E "error" build/r36s.log | head -30; exit 1; }

DIST="$ROOT/dist/r36s/ports"
rm -rf "$ROOT/dist/r36s" && mkdir -p "$DIST"
cp -r packaging/r36s/. "$DIST/"
cp LICENSE "$DIST/hohnative/LICENSE.txt"                                   # licenca do port (MIT)
cp third_party/unifont/OFL-1.1.txt "$DIST/hohnative/UNIFONT-OFL-1.1.txt"   # ideogramas (Unifont, OFL 1.1)
"$ZIG" objcopy --strip-all build/r36s/hoh "$DIST/hohnative/hoh" 2>/dev/null || cp build/r36s/hoh "$DIST/hohnative/hoh"
[ -s "$DIST/hohnative/hoh" ] || cp build/r36s/hoh "$DIST/hohnative/hoh"
chmod +x "$DIST/hohnative/hoh" "$DIST/Head Over Heels (Nativo).sh"
echo "== binario:"; file "$DIST/hohnative/hoh"
echo "== bibliotecas dinamicas:"; readelf -d "$DIST/hohnative/hoh" | grep NEEDED
echo "== maior versao de glibc exigida:"; readelf -V "$DIST/hohnative/hoh" | grep -oE "GLIBC_[0-9.]+" | sort -V -u | tail -1
(cd "$ROOT/dist/r36s" && rm -f ../hoh-r36s.zip && python3 -c "
import os, zipfile
with zipfile.ZipFile('../hoh-r36s.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk('ports'):
        for f in fs:
            p = os.path.join(d, f); info = zipfile.ZipInfo.from_file(p); info.external_attr = (0o755 if f.endswith(('.sh', 'hoh')) else 0o644) << 16
            z.writestr(info, open(p, 'rb').read(), zipfile.ZIP_DEFLATED)
")
ls -la "$ROOT/dist/hoh-r36s.zip" "$DIST/hohnative"

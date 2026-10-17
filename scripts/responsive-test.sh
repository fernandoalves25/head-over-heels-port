#!/usr/bin/env bash
# Teste do modo responsivo: roda o jogo sem janela em varias proporcoes (menus, mapa, jogo, caminhada,
# pausa), salva os quadros 1x em build/responsive/<proporcao>_*.bmp e, com os pacotes HD, confere se
# a lista refaz cada quadro pixel a pixel (HOH_HD_VERIFY). Usa uma COPIA dos dados.
# Uso: scripts/responsive-test.sh [pasta_do_jogo] [pacote]
cd "$(dirname "$0")/.."
SRC="${1:-/mnt/f/headover/HeadOverHeels}"
PACK="${2:-anime}"
OUT=$(pwd)/build/responsive
BIN=$(pwd)/build/linux/hoh
cmake --build build/linux -j"$(nproc)" --target hoh > build/responsive-build.log 2>&1 || { grep -E "error" build/responsive-build.log | head; exit 1; }
rm -rf "$OUT"; mkdir -p "$OUT"
rc=0
for case in "16x9:1708x960" "21x9:2240x960" "em_pe:720x1280" "4x3:1280x960"; do
  name=${case%%:*}; size=${case#*:}
  WORK=/tmp/hoh-resp-$name
  rm -rf "$WORK"; mkdir -p "$WORK/Sound"
  cp "$SRC/HoHOriginal.dat" "$WORK/"; cp "$SRC"/Sound/* "$WORK/Sound/"
  (
    cd "$WORK"
    export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOH_WINDOW_SIZE="$size" HOH_RESPONSIVE=1
    export HOH_HD_DIR="$SRC/hd" HOH_HD="$PACK" HOH_HD_VERIFY="$OUT/${name}_verify.txt"
    export HOH_KEYS="3000:ENTER,6500:ENTER,8500:ENTER,10000:ENTER,14000:ENTER,17000:RIGHT/2500,21000:ESC"
    export HOH_SHOTS="7500:$OUT/${name}_1menu.bmp,11500:$OUT/${name}_2mapa.bmp,16500:$OUT/${name}_3jogo.bmp,19000:$OUT/${name}_4andando.bmp,22500:$OUT/${name}_5pausa.bmp"
    export HOH_AUTOQUIT_MS=24000
    timeout 90 "$BIN" --data "$WORK" > "$OUT/${name}_run.log" 2>&1
  )
  r=$?
  [ $r -ne 0 ] && rc=$r
  diffs=$(grep -c "pixels diferentes [1-9]" "$OUT/${name}_verify.txt" 2>/dev/null)
  frames=$(grep -c "pixels diferentes" "$OUT/${name}_verify.txt" 2>/dev/null)
  echo "$name ($size): exit=$r  quadros conferidos=$frames  com diferenca=$diffs"
  [ "${diffs:-0}" != "0" ] && rc=1
done
exit $rc

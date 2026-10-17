#!/usr/bin/env bash
# Teste do renderizador HD: desenha cada uma das 276 salas como no jogo, confere se a lista gravada
# refaz o quadro 1x pixel a pixel (build/hd-tour/salas.txt) e salva algumas capturas HD.
# Usa uma COPIA dos dados. Uso: scripts/hd-tour.sh [pasta_do_jogo] [pacote]
#   pacotes em <pasta_do_jogo>/hd/<pacote>/*.qoi (ver README, "Graficos HD")
cd "$(dirname "$0")/.."
SRC="${1:-/mnt/f/headover/HeadOverHeels}"
PACK="${2:-anime}"
cmake --build build/linux -j"$(nproc)" --target hoh > build/hd-tour-build.log 2>&1 || { grep -E "error" build/hd-tour-build.log | head; exit 1; }
WORK=/tmp/hoh-hd-tour
OUT=$(pwd)/build/hd-tour
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK/Sound" "$OUT"
cp "$SRC/HoHOriginal.dat" "$WORK/"; cp "$SRC"/Sound/* "$WORK/Sound/"
cd "$WORK"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOH_HD_DIR="$SRC/hd" HOH_HD="$PACK"
export HOH_HD_ROOMTOUR="$OUT" HOH_WINDOW_SIZE=1280x960
timeout 600 "$OLDPWD/build/linux/hoh" --data "$WORK" > "$OUT/run.log" 2>&1
rc=$?
echo "exit=$rc"
tail -1 "$OUT/salas.txt"
grep -v "diferentes 0$" "$OUT/salas.txt" | grep -v "^total" | head -20
exit $rc

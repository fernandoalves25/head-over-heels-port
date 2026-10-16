#!/usr/bin/env bash
# Teste de jogabilidade sem janela: NEW GAME -> mapa dos planetas -> primeira sala -> anda/pula.
# Usa uma COPIA dos dados (nao mexe no Config.dat/saves do jogo original). Capturas em build/play/.
# Uso: scripts/play-test.sh [pasta_do_jogo] [binario]   (binario padrao: build/linux/hoh)
cd "$(dirname "$0")/.."
SRC="${1:-/mnt/f/headover/HeadOverHeels}"
BIN="${2:-./build/linux/hoh}"
WORK=/tmp/hoh-play-data
OUT="$(pwd)/build/play"
rm -rf "$WORK" "$OUT" && mkdir -p "$WORK/Sound" "$OUT"
cp "$SRC/HoHOriginal.dat" "$WORK/" && cp "$SRC"/Sound/* "$WORK/Sound/"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
# intro -> titulo -> menu -> PLAY THE GAME -> NEW GAME -> (mapa) -> jogo; depois anda e pula
export HOH_KEYS="${HOH_KEYS:-3000:ENTER,6500:ENTER,8500:ENTER,10000:ENTER,14000:ENTER,17000:RIGHT,17300:RIGHT,17600:RIGHT,18500:UP,18800:UP,19500:SPACE,21000:LEFT,21300:LEFT,22500:DOWN,23500:RCTRL,25000:SPACE}"
export HOH_SHOTS="${HOH_SHOTS:-12000:$OUT/1_map.bmp,16000:$OUT/2_room.bmp,19000:$OUT/3_walk.bmp,19800:$OUT/4_jump.bmp,24000:$OUT/5_swap.bmp,26500:$OUT/6_end.bmp}"
export HOH_AUTOQUIT_MS="${HOH_AUTOQUIT_MS:-28000}"
timeout 120 "$BIN" --data "$WORK" > "$OUT/run.log" 2>&1
rc=$?
echo "exit=$rc"; grep -v "^$" "$OUT/run.log" | head -40
ls "$OUT"
exit $rc

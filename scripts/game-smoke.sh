#!/usr/bin/env bash
# Roda o JOGO portado sem janela, com teclas roteirizadas, e salva capturas em build/game-smoke/.
# Uso: scripts/game-smoke.sh [pasta_do_jogo]
cd "$(dirname "$0")/.."
DATA="${1:-/mnt/f/headover/HeadOverHeels}"
OUT="$(pwd)/build/game-smoke"
mkdir -p "$OUT" && rm -f "$OUT"/*.bmp
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export HOH_KEYS="${HOH_KEYS:-3000:ENTER;6500:ENTER;9500:DOWN;10500:DOWN}"
export HOH_SHOTS="${HOH_SHOTS:-1500:$OUT/1_intro.bmp;5000:$OUT/2_title.bmp;8500:$OUT/3_menu.bmp;11500:$OUT/4_menu_down2.bmp}"
export HOH_AUTOQUIT_MS="${HOH_AUTOQUIT_MS:-12500}"
timeout 60 ./build/linux/hoh --data "$DATA" > "$OUT/run.log" 2>&1
rc=$?
echo "exit=$rc"; cat "$OUT/run.log" | head -30
ls -la "$OUT" | grep bmp

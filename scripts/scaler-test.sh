#!/usr/bin/env bash
# Captura o quadro final AMPLIADO em cada modo de escala (sem janela), em build/scaler/.
cd "$(dirname "$0")/.."
DATA="${1:-/mnt/f/headover/HeadOverHeels}"
SIZE="${2:-1600x1200}"
OUT="$(pwd)/build/scaler"; mkdir -p "$OUT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software HOH_WINDOW_SIZE="$SIZE"
for m in sharp smooth2x smooth3x nearest integer; do
  HOH_SCALER=$m HOH_KEYS="3000:ENTER,6500:ENTER" HOH_SCALED_SHOTS="9000:$OUT/$m.bmp" HOH_AUTOQUIT_MS=9800 \
    timeout 60 ./build/linux/hoh --data "$DATA" > "$OUT/$m.log" 2>&1; echo "$m exit=$?"
done
ls -la "$OUT"/*.bmp

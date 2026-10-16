#!/usr/bin/env bash
# Teste sem janela: roda o visualizador com drivers "dummy" do SDL e salva capturas de tela.
# Uso: scripts/smoke-test.sh [pasta_do_jogo] [pasta_saida]
set -u
cd "$(dirname "$0")/.."
DATA="${1:-/mnt/f/headover/HeadOverHeels}"
OUT="${2:-build/smoke}"
mkdir -p "$OUT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
fail=0
run() {
  local name="$1"; shift
  if timeout 30 ./build/linux/hoh-viewer --data "$DATA" --frames 20 --shot "$OUT/$name.bmp" "$@" > "$OUT/$name.log" 2>&1; then
    echo "ok   $name ($(stat -c %s "$OUT/$name.bmp") bytes)"
  else
    echo "FALHOU $name (exit $?)"; tail -5 "$OUT/$name.log"; fail=1
  fi
}
run title
for c in 0 1 2 3 4; do run "browse$c" --browse "$c"; done
exit $fail

#!/usr/bin/env bash
# Teste do renderizador HD fora das salas: percorre com teclas roteirizadas a abertura (com o
# "PRESENTS"), a tela-titulo, os menus (controles, som, opcoes, sair, slots), o mapa dos planetas,
# o jogo, a pausa (sair/voltar), a tela de resultado (HOH_TEST_GAMEOVER_MS) e o QUIT TO SYSTEM, e
# confere a cada 2 quadros se a lista refaz o quadro 1x pixel a pixel (HOH_HD_VERIFY), inclusive
# cada lista de origem no momento em que e usada (HOH_HD_VERIFY_DEEP). Passa com 0 diferencas em
# todo estado. Usa uma COPIA dos dados. Saida em build/hd-states/.
# Uso: scripts/hd-states.sh [pasta_do_jogo] [pacote] [binario]
cd "$(dirname "$0")/.."
SRC="${1:-/mnt/f/headover/HeadOverHeels}"
PACK="${2:-anime}"
BIN="${3:-./build/linux/hoh}"
[ "$BIN" = "./build/linux/hoh" ] && { cmake --build build/linux -j"$(nproc)" --target hoh > build/hd-states-build.log 2>&1 || { grep -E "error" build/hd-states-build.log | head; exit 1; }; }
WORK=/tmp/hoh-hd-states
OUT=$(pwd)/build/hd-states
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK/Sound" "$OUT"
cp "$SRC/HoHOriginal.dat" "$WORK/"; cp "$SRC"/Sound/* "$WORK/Sound/"
K=""
add() { K="$K${K:+,}$1"; }
# abertura: espera o PRESENTS (~22 s), pula; titulo -> menu
add 25000:ENTER; add 28500:ENTER
# CHANGE CONTROLS (11 itens) e volta com ESC
add 30500:DOWN; add 31200:ENTER; add 32800:DOWN; add 33300:DOWN; add 34000:ESC
# ADJUST THE SOUND: muda o modo e o volume, volta
add 35500:DOWN; add 36200:ENTER; add 37600:RIGHT; add 38300:DOWN; add 38900:LEFT; add 39500:ESC
# GAME OPTIONS: SHADOWS da a volta (3x), desce ate FRAME RATE, volta
add 41000:DOWN; add 41700:ENTER; add 43200:RIGHT; add 43800:RIGHT; add 44400:RIGHT
add 45000:DOWN; add 45600:DOWN; add 46200:DOWN; add 46800:DOWN; add 47400:DOWN; add 48600:ESC
# QUIT TO SYSTEM -> BACK (volta ao item 0)
add 50000:DOWN; add 50700:ENTER; add 52200:DOWN; add 52900:ENTER
# PLAY THE GAME -> OLD GAME (slots) -> BACK -> NEW GAME -> mapa -> jogo
add 54500:ENTER; add 56000:DOWN; add 56700:ENTER; add 58200:ESC; add 59700:UP; add 60400:ENTER
add 63500:ENTER
# jogo: anda, pula, pausa -> QUIT GAME -> BACK -> RESUME
add 66000:RIGHT/1500; add 68000:SPACE; add 69500:ESC
add 71000:DOWN; add 71700:ENTER; add 73200:DOWN; add 73900:ENTER; add 75400:ENTER
add 77000:LEFT/1000; add 78500:SPACE
# resultado (HOH_TEST_GAMEOVER_MS=80000): S (nome), digita, envia (sem rede: erro), sai para o menu
add 82500:S; add 83500:H; add 84000:D; add 85000:ENTER; add 87000:ENTER
# QUIT TO SYSTEM -> YES (transicao para preto e sai)
add 90000:DOWN; add 90600:DOWN; add 91200:DOWN; add 91800:DOWN; add 92500:ENTER; add 94000:ENTER
cd "$WORK"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOH_HD_DIR="$SRC/hd" HOH_HD="$PACK" HOH_WINDOW_SIZE=1280x960
export HOH_HD_VERIFY="$OUT/verify.txt" HOH_HD_VERIFY_EVERY="${HOH_HD_VERIFY_EVERY:-2}" HOH_HD_VERIFY_DEEP=1
export HOH_KEYS="$K" HOH_TEST_GAMEOVER_MS=80000 HOH_AUTOQUIT_MS=100000
export HOH_SHOTS="${HOH_SHOTS:-23500:$OUT/1_presents.bmp,27800:$OUT/2_title.bmp,29500:$OUT/3_menu.bmp,32500:$OUT/4_controls.bmp,44000:$OUT/5_options.bmp,57500:$OUT/6_slots.bmp,62600:$OUT/7_map.bmp,64300:$OUT/8_wipe.bmp,72500:$OUT/9_pause.bmp,84500:$OUT/10_result.bmp,94600:$OUT/11_quit.bmp}"
start=$(date +%s)
timeout 300 "$OLDPWD/$BIN" --data "$WORK" > "$OUT/run.log" 2>&1
rc=$?
echo "exit=$rc  ($(( $(date +%s) - start )) s)"
V="$OUT/verify.txt"
[ -f "$V" ] || { echo "sem $V"; exit 1; }
# por estado: quadros conferidos, com diferenca e sem lista
for st in 5 0 2 1 3 4; do
  n=$(grep -c "estado $st: ops" "$V"); bad=$(grep "estado $st: ops" "$V" | grep -vc "diferentes 0 ")
  nolist=$(grep -c "estado $st: sem lista" "$V")
  printf "estado %d: %4d quadros conferidos, %d com diferenca, %d sem lista\n" "$st" "$n" "$bad" "$nolist"
done
deep=$(grep -c "lista de origem" "$V")
echo "listas de origem que nao batem: $deep"
grep -E "diferentes [1-9]|sem lista|lista de origem" "$V" | grep -v "estado -1" | head -10
fail=$(grep -E "diferentes [1-9]|lista de origem" "$V" | grep -vc "estado -1")
fail=$(( fail + $(grep "sem lista" "$V" | grep -vc "estado -1") ))
for st in 5 0 2 1 3 4; do grep -q "estado $st: ops" "$V" || { echo "estado $st nao foi conferido"; fail=$((fail + 1)); }; done
[ $rc -eq 0 ] && [ $fail -eq 0 ] && echo "OK: 0 diferencas em todos os estados" && exit 0
echo "FALHOU ($fail)"; exit 1

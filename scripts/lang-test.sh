#!/usr/bin/env bash
# Teste do seletor de idioma: abre GAME OPTIONS, desce ate LANGUAGE e troca o idioma com RIGHT
# (EN -> PT -> ES -> ZH -> EN) e LEFT (EN -> ZH), com uma captura depois de cada troca; volta ao
# menu principal (o letreiro de creditos tem que estar no idioma novo) e confere o port.cfg.
# Usa uma COPIA dos dados. Saida em build/lang-test/.
# Uso: scripts/lang-test.sh [pasta_do_jogo] [binario]   (ex.: binario ./build/asan/hoh)
cd "$(dirname "$0")/.."
SRC="${1:-/mnt/f/headover/HeadOverHeels}"
BIN="${2:-./build/linux/hoh}"
WORK=/tmp/hoh-lang-test
OUT=$(pwd)/build/lang-test
rm -rf "$WORK" "$OUT"; mkdir -p "$WORK/Sound" "$OUT"
cp "$SRC/HoHOriginal.dat" "$WORK/"; cp "$SRC"/Sound/* "$WORK/Sound/"
K=""
add() { K="$K${K:+,}$1"; }
add 25000:ENTER; add 28500:ENTER                                       # abertura -> titulo -> menu
add 29500:DOWN; add 30200:DOWN; add 30900:DOWN; add 31600:ENTER        # GAME OPTIONS
for t in 33000 33600 34200 34800 35400 36000; do add $t:DOWN; done    # ate LANGUAGE (item 6)
add 37000:RIGHT; add 38500:RIGHT; add 40000:RIGHT; add 41500:RIGHT     # PT, ES, ZH, EN
add 43000:LEFT; add 44500:ESC                                          # ZH; volta ao menu
S="38000:$OUT/1_pt.bmp,39500:$OUT/2_es.bmp,41000:$OUT/3_zh.bmp,42500:$OUT/4_en.bmp,44000:$OUT/5_zh.bmp"
S="$S,46500:$OUT/6_menu_zh.bmp,48500:$OUT/7_ticker_zh.bmp"
cd "$WORK"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOH_KEYS="$K" HOH_SHOTS="$S" HOH_AUTOQUIT_MS=50000
# (sem detect_leaks: as salas da abertura ja vazam controladores, fora do escopo deste teste)
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS="print_stacktrace=1"
unset HOH_LANG
timeout 300 "$OLDPWD/$BIN" --data "$WORK" > "$OUT/run.log" 2>&1
rc=$?
echo "exit=$rc"
grep -E "ERROR|runtime error|LeakSanitizer" "$OUT/run.log" | head
n=$(ls "$OUT"/*.bmp 2>/dev/null | wc -l)
lang=$(grep -E "^lang" "$WORK/port.cfg" 2>/dev/null)  # o port.cfg fica na pasta de trabalho
echo "capturas: $n de 7; port.cfg: $lang"
[ $rc -eq 0 ] && [ "$n" -eq 7 ] && [ "$lang" = "lang = zh" ] && ! grep -qE "ERROR|runtime error" "$OUT/run.log" && echo "OK" && exit 0
echo "FALHOU"; exit 1

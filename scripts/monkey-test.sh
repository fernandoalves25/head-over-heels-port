#!/usr/bin/env bash
# Teste de estresse: build com AddressSanitizer/UBSan, teclas aleatorias por todos os menus.
# Usa uma COPIA dos dados (nao mexe no Config.dat/saves do jogo original).
# Uso: scripts/monkey-test.sh [pasta_do_jogo] [semente] [n_teclas]
cd "$(dirname "$0")/.."
SRC="${1:-/mnt/f/headover/HeadOverHeels}"
SEED="${2:-1}"
N="${3:-70}"
WORK=/tmp/hoh-monkey-data
rm -rf "$WORK" && mkdir -p "$WORK/Sound"
cp "$SRC/HoHOriginal.dat" "$WORK/" && cp "$SRC"/Sound/* "$WORK/Sound/"
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" > build/asan.log 2>&1 &&
  cmake --build build/asan -j"$(nproc)" --target hoh >> build/asan.log 2>&1 || { tail -20 build/asan.log; exit 1; }
KEYS=$(python3 - "$SEED" "$N" <<'PY'
import random, sys
random.seed(int(sys.argv[1])); n = int(sys.argv[2])
pool = ["DOWN"] * 6 + ["UP"] * 4 + ["ENTER"] * 6 + ["LEFT"] * 3 + ["RIGHT"] * 3 + ["ESC"] * 2 + ["SPACE", "W", "M"]
t, out = 3000, []
for _ in range(n):
    out.append(f"{t}:{random.choice(pool)}")
    t += random.randint(250, 900)
print(",".join(out)); print(t + 3000, file=sys.stderr)
PY
)
END=$(python3 - "$SEED" "$N" <<'PY' 2>&1 >/dev/null
import random, sys
random.seed(int(sys.argv[1])); n = int(sys.argv[2])
pool = ["DOWN"] * 6 + ["UP"] * 4 + ["ENTER"] * 6 + ["LEFT"] * 3 + ["RIGHT"] * 3 + ["ESC"] * 2 + ["SPACE", "W", "M"]
t = 3000
for _ in range(n):
    random.choice(pool); t += random.randint(250, 900)
print(t + 3000, file=sys.stderr)
PY
)
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOH_KEYS="$KEYS" HOH_AUTOQUIT_MS="$END"
export ASAN_OPTIONS="detect_leaks=0" UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
timeout 300 ./build/asan/hoh --data "$WORK" > build/monkey.log 2>&1
rc=$?
grep -E "ERROR|runtime error|SUMMARY|#[0-9]+ " build/monkey.log | head -40
echo "semente=$SEED teclas=$N duracao=${END}ms exit=$rc"
exit $rc

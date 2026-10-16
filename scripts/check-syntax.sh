#!/usr/bin/env bash
# Verifica a sintaxe (sem linkar) de arquivos do port.
# Uso: scripts/check-syntax.sh src/game/menu.cpp [outros.cpp ...]   (sem argumentos: todos de src/game)
cd "$(dirname "$0")/.."
files=("$@")
[ ${#files[@]} -eq 0 ] && files=(src/game/*.cpp)
CFLAGS="$(pkg-config --cflags sdl2 2>/dev/null)"
fail=0
for f in "${files[@]}"; do
  out=$(g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter -Isrc $CFLAGS -fsyntax-only "$f" 2>&1)
  if [ -n "$out" ]; then
    echo "== $f"; echo "$out" | grep -E "error|warning" | head -20
    echo "$out" | grep -q "error" && fail=1
  else
    echo "ok $f"
  fi
done
exit $fail

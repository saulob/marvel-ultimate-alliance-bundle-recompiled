#!/usr/bin/env bash
# Checks that generated/mua1, generated/mua1-gold and generated/mua2 were written by tools/codegen.ps1 for the supported
# default.xex of each game. Codegen itself only runs on Windows. Exit status: 0 current, 2 stale or unverifiable.
#
#   tools/check-guest-code.sh [ROOT]
set -euo pipefail

ROOT="$(cd "${1:-$(dirname "$0")/..}" && pwd)"

stale() { echo "Guest code: stale ($*)" >&2; echo "Run tools\codegen.ps1 on Windows, then build again." >&2; exit 2; }

# key, the game folders its default.xex may be in, the supported default.xex (src/game_profile.cpp)
check_module() {
  local key="$1" folders="$2" xex_sha256="$3" out="$ROOT/generated/$1" xex="" folder output
  [ -f "$out/sources.cmake" ] || stale "generated/$key is missing"
  [ -f "$out/codegen.stamp" ] || stale "generated/$key has no successful codegen record"
  while IFS= read -r output; do
    [ -f "$out/$output" ] || stale "generated/$key/$output is missing"
  done < <(sed -n '/"outputs"/,/\]/p' "$out/codegen.stamp" | grep -o '"[^"]*"' | tr -d '"' | grep -v '^outputs$')
  for folder in $folders; do
    xex="$(find "$ROOT/game/$folder" -maxdepth 1 -iname default.xex 2>/dev/null | head -n 1)"
    [ -n "$xex" ] && break
  done
  [ -n "$xex" ] || stale "default.xex of $key is missing (game/${folders%% *})"
  [ "$(sha256sum "$xex" | cut -d' ' -f1)" = "$xex_sha256" ] || stale "game/$folder/default.xex is not the supported $key version"
}

check_module mua1 mua1 44e5eec135073224f2abe09deaef765a6d130c81aa575d5c376338f470bef92a
check_module mua1-gold "mua1-gold extracted" 06d1d7811e1668a5c16afef5102ca59b00684dc1ea6184849418f59f022b86b7
check_module mua2 mua2 2613dc17af39cefacb7226855f62281d83b6e991c9d467d9e8ebb0a681ab0440
echo "Guest code: current"

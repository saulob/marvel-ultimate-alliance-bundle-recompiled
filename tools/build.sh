#!/usr/bin/env bash
# The normal Linux build. Makes sure tools/rexglue/linux-amd64 holds the ReXGlue runtime and GPU plugin built from this
# branch's patches, building them with tools/build-patched-rexruntime.sh when it does not (git and network access the
# first time), then configures and builds the matching preset into out/build/<preset>: the launcher and the three game
# programs. The Debug libraries stay stock.
#
#   tools/build.sh                  linux-amd64-release
#   tools/build.sh relwithdebinfo   linux-amd64-relwithdebinfo (or debug)
#
# Needs the extracted linux-amd64 SDK in tools/rexglue, generated/mua1, generated/mua1-gold and generated/mua2
# (tools/codegen.ps1 on Windows), and the TRG launcher (third_party/trg-launcher or MUA_TRG_LAUNCHER_DIR). See
# docs/linux.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/tools/rexglue/linux-amd64"
PATCHED="$ROOT/tools/build-patched-rexruntime.sh"

fail() { echo "ERROR: $*" >&2; exit 1; }

case "${1:-release}" in
  release) CONFIG=Release ;;
  relwithdebinfo) CONFIG=RelWithDebInfo ;;
  debug) CONFIG=Debug ;;
  *) fail "usage: $0 [release|relwithdebinfo|debug]" ;;
esac
PRESET="linux-amd64-${1:-release}"
[ "$(uname -s)" = Linux ] || fail "builds on Linux only"
# Ubuntu 24.04's clang 20.1.2 ignores an explicit argument for a parameter whose default names an inline constexpr object
# (C++23), which tests/frame_limiter_tests.cpp catches; the release environment's apt.llvm.org clang 20.1.8 does not.
MIN_CLANG=20.1.8
CLANG_VERSION="$(clang++-20 --version 2>/dev/null | sed -nE '1s/.*version ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p')"
[ -n "$CLANG_VERSION" ] && [ "$(printf '%s\n' "$MIN_CLANG" "$CLANG_VERSION" | sort -V | head -1)" = "$MIN_CLANG" ] ||
  fail "clang++-20 ${CLANG_VERSION:-is missing}; this build needs $MIN_CLANG or newer (tools/setup-linux-build-env.sh installs it)"
[ -d "$SDK/lib/cmake/rexglue" ] || fail "missing $SDK (extract rexglue-sdk-0.10.0-linux-amd64.zip into tools/rexglue)"
bash "$ROOT/tools/check-guest-code.sh" "$ROOT" || fail "generated/ is not current (run tools/codegen.ps1 on Windows first)"

if [ "$CONFIG" != Debug ] && ! bash "$PATCHED" --check "$SDK" >/dev/null 2>&1; then
  echo "== Building the patched ReXGlue runtime and GPU plugin"
  bash "$PATCHED" "$SDK"
fi

cd "$ROOT"
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
echo "Built out/build/$PRESET: marvel_ultimate_alliance, marvel_ultimate_alliance_mua1, marvel_ultimate_alliance_mua1_gold, marvel_ultimate_alliance_mua2"

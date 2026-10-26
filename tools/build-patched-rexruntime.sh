#!/usr/bin/env bash
# Builds ReXGlue's Release and RelWithDebInfo librexruntime.so and librexgpu-xenos.so (the Vulkan GPU plugin) from the
# pinned v0.10.0 source with this project's patches and installs them into an extracted linux-amd64 SDK folder, with
# the patched public headers and the patched ReXApp source the game's own build compiles. Build on Ubuntu 20.04: the
# Release libraries must not need a glibc newer than MAX_GLIBC. tools/build.sh and tools/package-linux.sh run this
# themselves, and the CMake build checks the SDK with --check; see docs/linux.md.
#
#   tools/build-patched-rexruntime.sh SDK_DIR [WORK_DIR]    build and install (network needed the first time)
#   tools/build-patched-rexruntime.sh --check SDK_DIR [--config CONFIG] [--staged DIR]   verify only
#   tools/build-patched-rexruntime.sh --list                print the patches applied here, in order, and the fingerprint
#   tools/build-patched-rexruntime.sh --stress SDK_DIR BUILD_DIR   run the allocator stress test (see below)
#
# --check verifies the patched libraries in SDK_DIR and, with --staged, that DIR holds the SDK's runtime and GPU plugin
# of CONFIG (Debug, RelWithDebInfo or Release; default Release). The Debug libraries stay stock, so for Debug only the
# staged copies are checked. WORK_DIR holds the fetched source and its build (default: out/rexglue-sdk-src in this
# repository); the build tree is kept between runs and the patched files whose content did not change keep their
# time, so a patch change rebuilds only what it touches. Needs git, clang-20, clang++-20, cmake 3.25+, ninja,
# binutils and the packages tools/setup-linux-build-env.sh installs. SDK_DIR must be writable. --stress runs
# mua_physical_heap_stress from an already configured Release project build in BUILD_DIR, beside the installed
# runtime.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SELF="$ROOT/tools/$(basename "$0")"
PATCH_DIR="$ROOT/patches/rexglue-sdk"
PATCH_PLATFORM=linux
STAMP_NAME=rexruntime-patch.txt
LIBS=(lib/librexruntime.so lib/librexruntimerd.so lib/librexgpu-xenos.so lib/librexgpu-xenosrd.so)
# Patched ReXGlue sources the game's own build compiles from the SDK folder (ReXApp is part of the executable): SDK
# path, then its path in the ReXGlue source.
SOURCE_FILES=("share/rexglue/rex_app.cpp:src/ui/rex_app.cpp")
# How the patched files are built; part of the fingerprint, so changing them invalidates the installed files.
CC=clang-20
CXX=clang++-20
BUILD_FLAGS=-march=x86-64-v2
LINK_RPATH='$ORIGIN:$ORIGIN/../lib'
BUILD_TARGETS=(rexruntime rexgpu-xenos)
BUILD_CONFIGS=(RelWithDebInfo Release)
MAX_GLIBC=2.31
# The runtime loads the GPU plugin through these.
PLUGIN_ENTRY_POINTS=(rex_gpu_abi_version rex_gpu_create)

# shellcheck source=../patches/rexglue-sdk/baseline.env
. "$PATCH_DIR/baseline.env"

fail() { echo "ERROR: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
text_sha() { sha256sum | cut -d' ' -f1; }
join_by() { local IFS="$1"; shift; echo "$*"; }

# The patches in the order they are applied: NNNN-name.patch applies everywhere, NNNN-name.windows.patch only on
# Windows and NNNN-name.linux.patch only here. Sorted by name; the numbers are unique, so the order is unambiguous.
PATCHES=()
collect_patches() {
  local file name number
  local -A numbers=()
  local -a names=()
  shopt -s nullglob
  for file in "$PATCH_DIR"/*.patch; do
    name="$(basename "$file")"
    [[ "$name" =~ ^([0-9]{4})-[a-z0-9]+(-[a-z0-9]+)*(\.(windows|linux))?\.patch$ ]] ||
      fail "patches/rexglue-sdk/$name is not named NNNN-name[.windows|.linux].patch"
    number="${BASH_REMATCH[1]}"
    [ -z "${numbers[$number]:-}" ] ||
      fail "patches/rexglue-sdk/$name and ${numbers[$number]} share the number $number"
    numbers[$number]="$name"
    case "$name" in
      *.windows.patch) ;;
      *) names+=("$name") ;;
    esac
  done
  shopt -u nullglob
  [ "${#names[@]}" -gt 0 ] || fail "no patches in patches/rexglue-sdk"
  mapfile -t PATCHES < <(printf '%s\n' "${names[@]}" | LC_ALL=C sort)
}
collect_patches

compiler() { "$CXX" --version | sed -n 1p; }
patch_names() { join_by , "${PATCHES[@]}"; }
patch_set_sha() {  # every patch's name and hash, in order
  local name
  for name in "${PATCHES[@]}"; do echo "$name $(sha "$PATCH_DIR/$name")"; done | text_sha
}
inputs_sha() {  # everything the installed files are built from
  {
    echo "sdk_version=$SDK_VERSION"
    echo "sdk_tag=$SDK_TAG"
    echo "sdk_commit=$SDK_COMMIT"
    echo "platform=$PATCH_PLATFORM"
    echo "compiler=$(compiler)"
    echo "build_flags=$BUILD_FLAGS"
    echo "link_rpath=$LINK_RPATH"
    echo "build_targets=$(join_by , "${BUILD_TARGETS[@]}")"
    echo "build_configs=$(join_by , "${BUILD_CONFIGS[@]}")"
    echo "max_glibc=$MAX_GLIBC"
    echo "source_files=$(join_by , "${SOURCE_FILES[@]}")"
    local name
    for name in "${PATCHES[@]}"; do echo "patch $name $(sha "$PATCH_DIR/$name")"; done
    echo "build_script=$(sha "$SELF")"
  } | text_sha
}
official_sha() {  # lib
  case "$1" in
    lib/librexruntime.so) echo "$OFFICIAL_LINUX_RUNTIME_SHA256" ;;
    lib/librexruntimerd.so) echo "$OFFICIAL_LINUX_RUNTIME_RD_SHA256" ;;
    lib/librexgpu-xenos.so) echo "$OFFICIAL_LINUX_GPU_SHA256" ;;
    lib/librexgpu-xenosrd.so) echo "$OFFICIAL_LINUX_GPU_RD_SHA256" ;;
    *) fail "no stock hash for $1" ;;
  esac
}
lib_config() {  # lib
  case "$1" in
    *rd.so) echo RelWithDebInfo ;;
    *) echo Release ;;
  esac
}
built_lib() { echo "$SRC/out/linux-amd64/$(lib_config "$1")/$(basename "$1")"; }

highest_version() {  # FILE GLIBC|GLIBCXX: the newest version of that library the file requires, empty if none
  { readelf -V -W "$1" | grep -oE "Name: $2_[0-9]+(\.[0-9]+)*" || true; } | sed "s/^Name: $2_//" | sort -V | tail -1
}
exceeds() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" != "$2" ]; }  # VERSION LIMIT

inputs_change() {  # stamp: which inputs differ from the ones the stamp was written for
  local old current added removed name
  old="$(sed -n 's/^patches=//p' "$1")"
  current="$(patch_names)"
  added=""; removed=""
  for name in ${current//,/ }; do case ",$old," in *",$name,"*) ;; *) added="$added $name" ;; esac; done
  for name in ${old//,/ }; do case ",$current," in *",$name,"*) ;; *) removed="$removed $name" ;; esac; done
  if [ -n "$added" ]; then echo "patches added:$added"
  elif [ -n "$removed" ]; then echo "patches removed or renamed:$removed"
  elif [ "$(sed -n 's/^patch_sha256=//p' "$1")" != "$(patch_set_sha)" ]; then echo "a patch changed"
  elif [ "$(sed -n 's/^compiler=//p' "$1")" != "$(compiler)" ]; then echo "compiler changed: $(compiler)"
  else echo "ReXGlue baseline, build script or build settings changed"; fi
}

check_stamp() {  # SDK_DIR
  local sdk="$1" stamp="$1/$STAMP_NAME" problems=0 lib actual entry file
  command -v "$CXX" >/dev/null || { echo "  $CXX not found" >&2; return 1; }
  [ -f "$stamp" ] || { echo "  $STAMP_NAME is missing" >&2; return 1; }
  [ "$(sed -n 's/^sdk_commit=//p' "$stamp")" = "$SDK_COMMIT" ] ||
    { echo "  libraries were not built from ReXGlue $SDK_TAG ($SDK_COMMIT)" >&2; problems=1; }
  [ "$(sed -n 's/^inputs_sha256=//p' "$stamp")" = "$(inputs_sha)" ] ||
    { echo "  libraries were built from other inputs than the current ones ($(inputs_change "$stamp"))" >&2; problems=1; }
  for lib in "${LIBS[@]}"; do
    grep -q "^[0-9a-f]\{64\}  $lib\$" "$stamp" || { echo "  $lib is not in $STAMP_NAME" >&2; problems=1; continue; }
    [ -f "$sdk/$lib" ] || { echo "  missing: $lib" >&2; problems=1; continue; }
    actual="$(sha "$sdk/$lib")"
    grep -qx "$actual  $lib" "$stamp" || { echo "  $lib does not match the patched build" >&2; problems=1; }
    [ "$actual" != "$(official_sha "$lib")" ] || { echo "  $lib is the stock ReXGlue release file" >&2; problems=1; }
  done
  for entry in "${SOURCE_FILES[@]}"; do
    file="${entry%%:*}"
    grep -q "^[0-9a-f]\{64\}  $file\$" "$stamp" || { echo "  $file is not in $STAMP_NAME" >&2; problems=1; continue; }
    [ -f "$sdk/$file" ] || { echo "  missing: $file" >&2; problems=1; continue; }
    grep -qx "$(sha "$sdk/$file")  $file" "$stamp" || { echo "  $file does not match the patched source" >&2; problems=1; }
  done
  return "$problems"
}

check_staged() {  # SDK_DIR CONFIG DIR: DIR holds the SDK's runtime and GPU plugin of CONFIG
  local suffix name problems=0
  case "$2" in
    Debug) suffix=d ;;
    RelWithDebInfo) suffix=rd ;;
    Release) suffix= ;;
    *) fail "unknown configuration '$2'" ;;
  esac
  for name in "librexruntime$suffix.so" "librexgpu-xenos$suffix.so"; do
    if [ ! -f "$3/$name" ]; then
      echo "  $name is not next to the program in $3" >&2
      problems=1
    elif ! cmp -s "$1/lib/$name" "$3/$name"; then
      echo "  $name next to the program is not the one in $1" >&2
      problems=1
    fi
  done
  return "$problems"
}

require_release_build() {  # BUILD_DIR TEST: the test must load the Release runtime the stamp records
  readelf -d "$1/$2" | grep -qF '[librexruntime.so]' || fail "$1 is not a Release project build"
}

# Copies only when the content differs, so an installed file keeps its time and nothing built from it is redone.
copy_if_different() {  # SOURCE DESTINATION; prints 1 when it copied
  if [ -f "$2" ] && cmp -s "$1" "$2"; then
    echo 0
    return
  fi
  mkdir -p "$(dirname "$2")"
  cp "$1" "$2"
  echo 1
}

if [ "${1:-}" = --list ]; then
  printf '%s\n' "${PATCHES[@]}"
  echo "fingerprint $(inputs_sha)"
  exit 0
fi

if [ "${1:-}" = --check ]; then
  SDK="${2:-}"
  [ -n "$SDK" ] || fail "usage: $0 --check SDK_DIR [--config CONFIG] [--staged DIR]"
  shift 2
  CONFIG=Release
  STAGED=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --config) CONFIG="${2:?--config needs a configuration}"; shift ;;
      --staged) STAGED="${2:?--staged needs a folder}"; shift ;;
      *) fail "unexpected argument $1" ;;
    esac
    shift
  done
  case "$CONFIG" in Debug | RelWithDebInfo | Release) ;; *) fail "unknown configuration '$CONFIG'" ;; esac
  problems=0
  if [ "$CONFIG" != Debug ]; then check_stamp "$SDK" || problems=1; fi
  if [ -n "$STAGED" ]; then check_staged "$SDK" "$CONFIG" "$STAGED" || problems=1; fi
  [ "$problems" = 0 ] ||
    fail "$SDK or the build folder does not hold this branch's ReXGlue runtime and GPU plugin ($CONFIG); run tools/build.sh, or tools/build-patched-rexruntime.sh $SDK"
  [ "$CONFIG" = Debug ] || echo "ReXGlue patches: current (${#PATCHES[@]} patches, fingerprint $(inputs_sha | cut -c1-12))"
  exit 0
fi

if [ "${1:-}" = --stress ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --stress SDK_DIR BUILD_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime and GPU plugin"
  cmake --build "$3" --target mua_physical_heap_stress
  require_release_build "$3" mua_physical_heap_stress
  # Run beside the installed runtime, not the copy the build left in BUILD_DIR, which may be stale.
  stage="$3/physical_heap_stress"
  mkdir -p "$stage"
  cp "$3/mua_physical_heap_stress" "$2/lib/librexruntime.so" "$stage/"
  (cd "$stage" && ./mua_physical_heap_stress | grep -E '^(view|PASS|FAIL)')
  exit 0
fi

SDK="${1:-}"
[ -n "$SDK" ] && [ -d "$SDK/lib/cmake/rexglue" ] ||
  fail "usage: $0 SDK_DIR [WORK_DIR] (SDK_DIR is the extracted linux-amd64 SDK)"
SRC="${2:-$ROOT/out/rexglue-sdk-src}"

for tool in git "$CC" "$CXX" cmake ninja sha256sum readelf nm; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done
# Ubuntu 24.04's clang 20.1.2 ignores an explicit argument for a parameter whose default names an inline constexpr object
# (C++23), which tests/frame_limiter_tests.cpp catches; the release environment's apt.llvm.org clang 20.1.8 does not.
MIN_CLANG=20.1.8
CLANG_VERSION="$(clang++-20 --version 2>/dev/null | sed -nE '1s/.*version ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p')"
[ -n "$CLANG_VERSION" ] && [ "$(printf '%s\n' "$MIN_CLANG" "$CLANG_VERSION" | sort -V | head -1)" = "$MIN_CLANG" ] ||
  fail "clang++-20 ${CLANG_VERSION:-is missing}; this build needs $MIN_CLANG or newer (tools/setup-linux-build-env.sh installs it)"

# Already built from these inputs: nothing to do.
if check_stamp "$SDK" 2>/dev/null; then
  echo "ReXGlue patches: current (${#PATCHES[@]} patches, fingerprint $(inputs_sha | cut -c1-12)); nothing to build"
  exit 0
fi

if [ ! -d "$SRC/.git" ]; then
  echo "== Fetching ReXGlue $SDK_TAG from $SDK_REPO (network) into $SRC"
  mkdir -p "$(dirname "$SRC")"
  git clone --branch "$SDK_TAG" --depth 1 --recurse-submodules --shallow-submodules --jobs 8 "$SDK_REPO" "$SRC"
fi
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
[ "$COMMIT" = "$SDK_COMMIT" ] || fail "$SRC is at $COMMIT; expected $SDK_TAG $SDK_COMMIT (delete the folder to fetch it again)"

patched_files() {  # the files the patches changed or added in the ReXGlue source
  git -C "$SRC" diff --name-only
  git -C "$SRC" ls-files --others --exclude-standard -- src include tests cmake
}

echo "== Patch ($(patch_names))"
# Resetting the source and applying the patches again rewrites every patched file; the ones that come out the same
# get their time back, so the kept build tree under out/ rebuilds only what changed.
declare -A BEFORE=()
while IFS= read -r file; do
  [ -f "$SRC/$file" ] && BEFORE["$file"]="$(sha "$SRC/$file") $(stat -c %Y "$SRC/$file")"
done < <(patched_files)
# Another branch may have left other patches in the tree, in any tracked file. The build tree under out/ stays.
git -C "$SRC" checkout -- .
git -C "$SRC" clean -fdq -- src include tests cmake
for name in "${PATCHES[@]}"; do
  git -C "$SRC" apply --check "$PATCH_DIR/$name" || fail "$name no longer applies to ReXGlue $SDK_TAG"
  git -C "$SRC" apply "$PATCH_DIR/$name"
done
unchanged=0
patched=0
while IFS= read -r file; do
  patched=$((patched + 1))
  [ -n "${BEFORE[$file]:-}" ] && [ -f "$SRC/$file" ] || continue
  if [ "$(sha "$SRC/$file")" = "${BEFORE[$file]%% *}" ]; then
    touch -d "@${BEFORE[$file]##* }" "$SRC/$file"
    unchanged=$((unchanged + 1))
  fi
done < <(patched_files)
echo "ReXGlue source: ${#PATCHES[@]} patches applied; $unchanged of $patched patched files unchanged"

echo "== Build ${BUILD_TARGETS[*]} (${BUILD_CONFIGS[*]})"
# -ffile-prefix-map keeps the build folder out of the libraries' log strings; the RUNPATH is the stock libraries'
# instead of the build folder and the empty entries CMake would add.
FLAGS="$BUILD_FLAGS -ffile-prefix-map=$SRC=."
(
  cd "$SRC"
  cmake --preset linux-amd64 "-DCMAKE_C_COMPILER=$CC" "-DCMAKE_CXX_COMPILER=$CXX" \
    "-DCMAKE_C_FLAGS=$FLAGS" "-DCMAKE_CXX_FLAGS=$FLAGS" -DCMAKE_SKIP_RPATH=ON \
    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-rpath,'$LINK_RPATH'" >/dev/null
  for config in "${BUILD_CONFIGS[@]}"; do
    cmake --build out/build/linux-amd64 --config "$config" --target "${BUILD_TARGETS[@]}" --parallel
  done
)
for lib in "${LIBS[@]}"; do
  BUILT="$(built_lib "$lib")"
  [ -f "$BUILT" ] || fail "ReXGlue build output is missing $BUILT"
  [ "$(sha "$BUILT")" != "$(official_sha "$lib")" ] || fail "$(basename "$lib") came out identical to the stock release; the patches were not built in"
  # The RelWithDebInfo debug info names the source folder; only the Release files are packaged.
  if [ "$(lib_config "$lib")" = Release ]; then
    for path in "$SRC" "$SDK" "$ROOT"; do
      ! grep -qaF "$path" "$BUILT" || fail "$(basename "$lib") embeds a local folder ($path)"
    done
    GLIBC="$(highest_version "$BUILT" GLIBC)"
    GLIBCXX="$(highest_version "$BUILT" GLIBCXX)"
    echo "  $(basename "$lib"): GLIBC_${GLIBC:-none} GLIBCXX_${GLIBCXX:-none}"
    ! exceeds "${GLIBC:-0}" "$MAX_GLIBC" || fail "$(basename "$lib") needs GLIBC_$GLIBC; the baseline is glibc $MAX_GLIBC (build on Ubuntu 20.04)"
  fi
  DYNAMIC="$(readelf -d "$BUILT" 2>/dev/null)"
  grep -qF "[$LINK_RPATH]" <<< "$DYNAMIC" || fail "$(basename "$lib") does not have the RUNPATH $LINK_RPATH"
  case "$lib" in
    lib/librexgpu-xenos*)
      grep -qE '\(NEEDED\).*\[librexruntime(rd)?\.so\]' <<< "$DYNAMIC" || fail "$(basename "$lib") does not link the ReXGlue runtime"
      EXPORTS="$(nm -D --defined-only "$BUILT" | awk '{print $3}')"
      for symbol in "${PLUGIN_ENTRY_POINTS[@]}"; do
        grep -qx "$symbol" <<< "$EXPORTS" || fail "$(basename "$lib") does not export $symbol"
      done
      ;;
  esac
done

rm -f "$SDK/$STAMP_NAME"
installed=0
for lib in "${LIBS[@]}"; do installed=$((installed + $(copy_if_different "$(built_lib "$lib")" "$SDK/$lib"))); done
# tools/rexglue's headers come from the stock release zip and are never reinstalled otherwise; a patch that adds or
# changes a public header needs it mirrored here too.
headers=0
while IFS= read -r -d '' header; do
  headers=$((headers + $(copy_if_different "$header" "$SDK/include/rex/${header#"$SRC/include/rex/"}")))
done < <(find "$SRC/include/rex" -type f -print0)
sources=0
for entry in "${SOURCE_FILES[@]}"; do
  sources=$((sources + $(copy_if_different "$SRC/${entry#*:}" "$SDK/${entry%%:*}")))
done
echo "Installed in $SDK: $installed of ${#LIBS[@]} libraries, $headers headers and $sources sources changed"
{
  echo "sdk_version=$SDK_VERSION"
  echo "sdk_commit=$SDK_COMMIT"
  echo "inputs_sha256=$(inputs_sha)"
  echo "compiler=$(compiler)"
  echo "patch_sha256=$(patch_set_sha)"
  echo "patches=$(patch_names)"
  for lib in "${LIBS[@]}"; do echo "$(sha "$SDK/$lib")  $lib"; done
  for entry in "${SOURCE_FILES[@]}"; do echo "$(sha "$SDK/${entry%%:*}")  ${entry%%:*}"; done
} > "$SDK/$STAMP_NAME"
check_stamp "$SDK" || fail "the installed libraries failed their own check"
echo "Patched ReXGlue runtime and GPU plugin installed:"
cat "$SDK/$STAMP_NAME"

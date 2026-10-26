#!/usr/bin/env bash
# Builds the Linux x86_64 Release and packages dist/MarvelUltimateAlliance-v<version>-x86_64.AppImage: one AppImage with
# the launcher and the three game programs. Local only: nothing is published, tagged or uploaded. Run on Linux or WSL.
#
#   tools/package-linux.sh            incremental Release build, then package
#   tools/package-linux.sh --clean    clean Release build first
#   tools/package-linux.sh --out-dir DIR    write the AppImage, its .sha256 and the symbols to DIR instead of dist (test builds)
#   tools/package-linux.sh --verify-only FILE    check an existing AppImage (extracts it, scans it, checks the runtime)
#
# The programs' debug information goes to <out dir>/MarvelUltimateAlliance-v<version>-x86_64-symbols/, which is not
# packaged.
#
# Needs clang-20, clang++-20, lld-20, cmake 3.25+, ninja, rsync, git, wget or curl, unzip or python3, binutils and the
# GCC 13+ C++ runtime. Release builds are made on Ubuntu 20.04 (tools/setup-linux-build-env.sh prepares one): the AppImage
# needs the build machine's glibc.
# The ReXGlue runtime and Vulkan GPU plugin are built from source with the project's patches
# (tools/build-patched-rexruntime.sh) the first time and whenever the patches change, which also needs network
# access and the packages tools/setup-linux-build-env.sh installs.
# The AppImage runtime (the part that mounts the AppImage before AppRun runs) is built from the pinned AppImage/type2-runtime
# source with the project's patch (tools/build-appimage-runtime.sh): that needs root or sudo, network access the first time,
# and is skipped while the pins and patches are unchanged.
# Needs generated/mua1, generated/mua1-gold and generated/mua2 from tools/codegen.ps1 (Windows), each game's default.xex
# in its game folder, which only the guest-code freshness check reads (tools/check-guest-code.sh), and the TRG launcher
# (third_party/trg-launcher or MUA_TRG_LAUNCHER_DIR). The game files are not packaged.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# On the Linux filesystem: much faster than a Windows drive under /mnt, and case-sensitive.
WORK="${MUA_WORK:-$HOME/mua-linux-build}"
PRESET=linux-amd64-release
MAX_GLIBC=2.31
APP_NAME=marvel_ultimate_alliance
BIN_NAME=marvel_ultimate_alliance_bundle
GAME_PROGRAMS=(marvel_ultimate_alliance_mua1 marvel_ultimate_alliance_mua1_gold marvel_ultimate_alliance_mua2)
GAME_ICONS=(mua1 mua1-gold mua2)
PROGRAMS=("$BIN_NAME" "${GAME_PROGRAMS[@]}")
HEADER_ART=(header-art-mua1.png header-art-mua1-gold.png header-art-mua2.png)

BASELINE="$ROOT/patches/rexglue-sdk/baseline.env"
baseline() { sed -n "s|^$1=||p" "$BASELINE" | tr -d '\r'; }
SDK_VERSION="$(baseline SDK_VERSION)"
SDK_SHA256="$(baseline SDK_LINUX_ZIP_SHA256 | tr 'A-F' 'a-f')"
APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

CLEAN=0
VERIFY_ONLY=""
OUT_DIR="$ROOT/dist"
while [ $# -gt 0 ]; do
  case "$1" in
    --clean) CLEAN=1 ;;
    --out-dir) [ $# -ge 2 ] || { echo "--out-dir needs a folder" >&2; exit 2; }; OUT_DIR="$(realpath -m "$2")"; shift ;;
    --verify-only) [ $# -ge 2 ] || { echo "--verify-only needs a file" >&2; exit 2; }; VERIFY_ONLY="$(realpath "$2")"; shift ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

fail() { echo "ERROR: $*" >&2; exit 1; }

download() {  # url file sha256
  if [ ! -f "$2" ] || ! echo "$3  $2" | sha256sum -c --status -; then
    rm -f "$2"
    if command -v wget >/dev/null; then wget -q -O "$2" "$1"; else curl -fsSL -o "$2" "$1"; fi
  fi
  echo "$3  $2" | sha256sum -c --status - || fail "$2 does not match its pinned SHA-256"
}

# The version is the project VERSION and MUA_VERSION_SUFFIX in CMakeLists.txt, as for the Windows package.
VERSION="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*marvel_ultimate_alliance[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || fail "project VERSION not found in CMakeLists.txt"
VERSION="$VERSION$(sed -nE 's/^[[:space:]]*set\(MUA_VERSION_SUFFIX[[:space:]]+"([^"]*)"\).*/\1/p' "$ROOT/CMakeLists.txt" | tr -d '\r')"
APP_ID="$(sed -nE 's/^[[:space:]]*constexpr char kApplicationId\[\] = "([^"]+)";.*/\1/p' "$ROOT/src/platform.cpp")"
[[ "$APP_ID" =~ ^[A-Za-z0-9_-]+(\.[A-Za-z0-9_-]+)+$ ]] ||
  fail "src/platform.cpp must declare kApplicationId exactly once, as a dotted name on one line (found: '$APP_ID')"
[ -f "$ROOT/packaging/linux/$APP_ID.desktop" ] || fail "packaging/linux/$APP_ID.desktop is missing: the desktop file is named after kApplicationId"
NAME="MarvelUltimateAlliance-v$VERSION-x86_64"

EXPECTED=".DirIcon
AppRun
$APP_NAME.png
$APP_ID.desktop"
for program in "${PROGRAMS[@]}"; do EXPECTED+=$'\n'"usr/bin/$program"; done
for art in "${HEADER_ART[@]}"; do EXPECTED+=$'\n'"usr/bin/assets/launcher/$art"; done
EXPECTED+="
usr/bin/$APP_NAME.toml
usr/bin/librexgpu-xenos.so
usr/bin/librexruntime.so
usr/lib/libgcc_s.so.1
usr/lib/libstdc++.so.6
usr/share/doc/$APP_NAME/ReXGlue-LICENSE.txt
usr/share/doc/$APP_NAME/THIRD-PARTY-LICENSES-LINUX.txt
usr/share/doc/$APP_NAME/THIRD-PARTY-LICENSES.txt
usr/share/doc/$APP_NAME/libstdc++-copyright.txt
usr/share/icons/hicolor/scalable/apps/$APP_NAME.svg"
for size in 16 24 32 48 64 128 256 512; do EXPECTED+=$'\n'"usr/share/icons/hicolor/${size}x$size/apps/$APP_NAME.png"; done
for program in "${PROGRAMS[@]}"; do EXPECTED+=$'\n'"usr/share/icons/hicolor/256x256/apps/$program.png"; done

EXPECTED="$(printf '%s\n' "$EXPECTED" | LC_ALL=C sort)"

# Checks a staged or extracted AppDir; extra_paths are build or home folders that must not appear in any file.
scan_appdir() {  # appdir label [extra_paths...]
  local appdir="$1" label="$2" item rel base path utf16 f glibc missing rpath program problems=0
  shift 2
  problem() { echo "  $*" >&2; problems=$((problems + 1)); }
  while IFS= read -r -d '' item; do
    rel="${item#"$appdir"/}"
    base="$(basename "$item")"
    case "${base,,}" in
      default.xex | game | extracted | game-install-temp | game-install-backup | user_data | cache | logs | content | \
        settings.toml | game_settings.toml | mua1 | mua1-gold | mua1_gold | mua2 | 415607da | 4156082f | \
        '$systemupdate') problem "forbidden name: $rel" ;;
      *.xex | *.iso | *.zip | *.wad | *.pak | *.log | *.dmp | *.pdb | *.debug | *.bak | *.tmp | *.sav | *.invalid | \
        *.xsh | *.xpso | *.pyc | *~ | settings.toml.*) problem "forbidden file type: $rel" ;;
    esac
    case "${base,,}" in
      *renderdoc* | *vklayer* | *.json | implicit_layer.d | explicit_layer.d) problem "RenderDoc or Vulkan layer file: $rel" ;;
    esac
    [ -L "$item" ] && problem "symbolic link: $rel"
    if [ -f "$item" ]; then
      case "$(head -c 4 "$item" | tr -d '\0')" in LIVE | PIRS | "CON " | XEX2) problem "Xbox 360 package or executable: $rel" ;; esac
      for path in "$@"; do
        grep -qaF "$path" "$item" && problem "local path in file: $rel ($path)"
        utf16="$(printf '%s' "$path" | od -An -tx1 | tr -d ' \n' | sed 's/../\\x&\\x00/g')"
        LC_ALL=C grep -qaP "$utf16" "$item" && problem "UTF-16 local path in file: $rel ($path)"
      done
      grep -qaF DEBUG_SAULO "$item" && problem "DEBUG_SAULO in file: $rel"
      [ "$(head -c 2 "$item")" = MZ ] && problem "Windows binary: $rel"
    fi
  done < <(find "$appdir" -mindepth 1 -print0)
  local actual
  actual="$(cd "$appdir" && find . ! -type d | sed 's|^\./||' | LC_ALL=C sort)"
  [ "$actual" = "$EXPECTED" ] || { problem "unexpected AppDir contents:"; diff <(echo "$EXPECTED") <(echo "$actual") >&2 || true; }
  grep -qxF "StartupWMClass=$APP_ID" "$appdir/$APP_ID.desktop" || problem "StartupWMClass in $APP_ID.desktop is not $APP_ID"
  grep -qxF "Exec=$BIN_NAME" "$appdir/$APP_ID.desktop" || problem "Exec in $APP_ID.desktop is not $BIN_NAME"
  for program in "${GAME_PROGRAMS[@]}"; do
    grep -qF "$program" "$appdir/AppRun" || problem "AppRun does not start $program"
  done
  for program in "${PROGRAMS[@]}"; do
    grep -qaF "$APP_ID" "$appdir/usr/bin/$program" || problem "$program does not contain the application id $APP_ID"
    missing="$(LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$appdir/usr/bin/$program" | grep 'not found' || true)"
    [ -z "$missing" ] || problem "$program has unresolved libraries: $missing"
  done
  for f in "${PROGRAMS[@]}" librexruntime.so librexgpu-xenos.so; do
    if readelf -d "$appdir/usr/bin/$f" | grep -i renderdoc >/dev/null; then problem "$f links RenderDoc"; fi
    if readelf -S -W "$appdir/usr/bin/$f" | grep -F ' .debug_' >/dev/null; then problem "$f holds debug sections"; fi
    rpath="$(readelf -d "$appdir/usr/bin/$f" | sed -n 's/.*R\(UN\)\?PATH.*\[\(.*\)\]/\2/p')"
    case "$f" in
      lib*.so) case "$rpath" in '' | '$ORIGIN' | '$ORIGIN:$ORIGIN/../lib') ;; *) problem "$f RPATH/RUNPATH is '$rpath'" ;; esac ;;
      *) [ "$rpath" = '$ORIGIN' ] || problem "$f RPATH/RUNPATH is '$rpath', not \$ORIGIN" ;;
    esac
  done
  [ "$problems" = 0 ] || fail "the $label holds files that must not be released"
  echo "  $label clean"
  for f in "${PROGRAMS[@]/#/usr/bin/}" usr/bin/librexruntime.so usr/bin/librexgpu-xenos.so usr/lib/libstdc++.so.6 \
    usr/lib/libgcc_s.so.1; do
    glibc="$(readelf --dyn-syms -W "$appdir/$f" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)"
    echo "  $f needs $glibc"
    [ "$(printf '%s\n' "${glibc#GLIBC_}" "$MAX_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] ||
      fail "$f needs $glibc, above the glibc $MAX_GLIBC baseline"
  done
}

if [ -n "$VERIFY_ONLY" ]; then
  [ -f "$VERIFY_ONLY" ] || fail "missing $VERIFY_ONLY"
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  bash "$ROOT/tools/build-appimage-runtime.sh" --check "$VERIFY_ONLY"
  cp "$VERIFY_ONLY" "$TMP/verify.AppImage"
  chmod +x "$TMP/verify.AppImage"
  (cd "$TMP" && ./verify.AppImage --appimage-extract >/dev/null)
  scan_appdir "$TMP/squashfs-root" "extracted AppImage" "$ROOT" "$WORK" "$HOME"
  echo "Verified $VERIFY_ONLY"
  exit 0
fi

echo "Marvel Ultimate Alliance PC port $VERSION - Linux AppImage (local only)"
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "WARNING: version $VERSION has a suffix: the launcher's updater only finds packages named for the plain version" \
    "(set MUA_VERSION_SUFFIX to an empty string for a release)." >&2
fi
SOURCE="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if CHANGES="$(git -C "$ROOT" status --porcelain 2>/dev/null)"; then
  [ -z "$CHANGES" ] || SOURCE="$SOURCE plus uncommitted changes"
else
  SOURCE="$SOURCE, working tree state unknown"
fi
echo "Source: $SOURCE"

echo "== Tools"
for tool in clang-20 clang++-20 ld.lld-20 cmake ninja rsync sha256sum objcopy readelf objdump ldd; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done
command -v wget >/dev/null || command -v curl >/dev/null || fail "wget or curl not found"
command -v unzip >/dev/null || command -v python3 >/dev/null || fail "unzip or python3 not found"
clang++-20 --version | sed -n 1p
cmake --version | sed -n 1p
# Ubuntu 24.04's clang 20.1.2 ignores an explicit argument for a parameter whose default names an inline constexpr object
# (C++23), which tests/frame_limiter_tests.cpp catches; the release environment's apt.llvm.org clang 20.1.8 does not.
MIN_CLANG=20.1.8
CLANG_VERSION="$(clang++-20 --version 2>/dev/null | sed -nE '1s/.*version ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p')"
[ -n "$CLANG_VERSION" ] && [ "$(printf '%s\n' "$MIN_CLANG" "$CLANG_VERSION" | sort -V | head -1)" = "$MIN_CLANG" ] ||
  fail "clang++-20 ${CLANG_VERSION:-is missing}; this build needs $MIN_CLANG or newer (tools/setup-linux-build-env.sh installs it)"
HOST_GLIBC="$(getconf GNU_LIBC_VERSION | cut -d' ' -f2)"
echo "glibc $HOST_GLIBC"
[ "$(printf '%s\n' "$MAX_GLIBC" "$HOST_GLIBC" | sort -V | tail -1)" = "$MAX_GLIBC" ] ||
  fail "this machine has glibc $HOST_GLIBC; release builds need Ubuntu 20.04 (glibc $MAX_GLIBC), see tools/setup-linux-build-env.sh"

echo "== Inputs"
TRG="$ROOT/third_party/trg-launcher"
[ -f "$TRG/CMakeLists.txt" ] || TRG="${MUA_TRG_LAUNCHER_DIR:-}"
[ -n "$TRG" ] && [ -f "$TRG/CMakeLists.txt" ] || fail "the TRG launcher is missing: add third_party/trg-launcher or set MUA_TRG_LAUNCHER_DIR"
bash "$ROOT/tools/check-guest-code.sh" "$ROOT" || fail "generated/ is not current (run tools/codegen.ps1 on Windows first)"

echo "== Source -> $WORK"
mkdir -p "$WORK/third_party" "$WORK/tools"
for f in CMakeLists.txt CMakePresets.json mua1.manifest.toml mua1-gold.manifest.toml mua2.manifest.toml \
  tools/build-patched-rexruntime.sh tools/check-guest-code.sh; do
  rsync -a "$ROOT/$f" "$WORK/$f"
done
for d in assets cmake config src tests generated/mua1 generated/mua1-gold generated/mua2 packaging patches/rexglue-sdk \
  patches/trg-launcher tools/dump_image tools/hd_text_preview; do
  mkdir -p "$WORK/$d"
  rsync -a --delete --exclude .git "$ROOT/$d/" "$WORK/$d/"
done
mkdir -p "$WORK/third_party/trg-launcher"
rsync -a --delete --exclude .git "$TRG/" "$WORK/third_party/trg-launcher/"
# A Windows checkout has CRLF sources; the TRG patches are applied with git apply and need matching endings.
find "$WORK/third_party/trg-launcher" "$WORK/patches/trg-launcher" -type f \
  \( -name '*.patch' -o -name '*.h' -o -name '*.cpp' -o -name '*.c' -o -name '*.txt' -o -name '*.cmake' \) \
  -exec sed -i 's/\r$//' {} +

echo "== ReXGlue SDK $SDK_VERSION (linux-amd64)"
SDK="$WORK/tools/rexglue/linux-amd64"
if [ ! -f "$SDK/lib/cmake/rexglue/rexglueConfigVersion.cmake" ]; then
  mkdir -p "$WORK/tools/rexglue"
  ZIP="$WORK/tools/rexglue/rexglue-sdk-$SDK_VERSION-linux-amd64.zip"
  download "https://github.com/rexglue/rexglue-sdk/releases/download/v$SDK_VERSION/rexglue-sdk-$SDK_VERSION-linux-amd64.zip" \
    "$ZIP" "$SDK_SHA256"
  rm -rf "$SDK"
  if command -v unzip >/dev/null; then
    unzip -q -o "$ZIP" -d "$WORK/tools/rexglue"
  else
    python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$WORK/tools/rexglue"
  fi
fi
grep -qF "set(PACKAGE_VERSION \"$SDK_VERSION\")" "$SDK/lib/cmake/rexglue/rexglueConfigVersion.cmake" ||
  fail "tools/rexglue/linux-amd64 is not ReXGlue $SDK_VERSION"

echo "== Patched ReXGlue runtime and GPU plugin"
PATCHED="$ROOT/tools/build-patched-rexruntime.sh"
if ! bash "$PATCHED" --check "$SDK" >/dev/null 2>&1; then
  bash "$PATCHED" "$SDK" "$WORK/rexglue-sdk-src"
fi
bash "$PATCHED" --check "$SDK"

echo "== Build ($PRESET)"
BIN="$WORK/out/build/$PRESET"
[ "$CLEAN" = 1 ] && rm -rf "$BIN"
(cd "$WORK" && cmake --preset "$PRESET" >/dev/null && cmake --build --preset "$PRESET")
for f in "${PROGRAMS[@]}" librexruntime.so librexgpu-xenos.so $APP_NAME.toml "${HEADER_ART[@]/#/assets/launcher/}"; do
  [ -f "$BIN/$f" ] || fail "build output is missing $f"
done
bash "$PATCHED" --check "$SDK" --staged "$BIN" || fail "the build folder does not hold the patched ReXGlue libraries"

echo "== AppDir"
APPDIR="$WORK/AppDir"
DOC="$APPDIR/usr/share/doc/$APP_NAME"
SYMBOLS="$OUT_DIR/$NAME-symbols"
rm -rf "$APPDIR" "$SYMBOLS"
mkdir -p "$APPDIR/usr/bin/assets/launcher" "$APPDIR/usr/lib" "$DOC" "$SYMBOLS"
cp "$BIN/librexruntime.so" "$BIN/librexgpu-xenos.so" "$BIN/$APP_NAME.toml" "$APPDIR/usr/bin/"
for art in "${HEADER_ART[@]}"; do cp "$BIN/assets/launcher/$art" "$APPDIR/usr/bin/assets/launcher/"; done
# The debug sections carry the build machine's folders, so they stay in $SYMBOLS.
for program in "${PROGRAMS[@]}"; do
  objcopy --only-keep-debug "$BIN/$program" "$SYMBOLS/$program.debug"
  objcopy --strip-debug --add-gnu-debuglink="$SYMBOLS/$program.debug" "$BIN/$program" "$APPDIR/usr/bin/$program"
done
# ReXGlue needs GLIBCXX_3.4.32 (GCC 13); AppRun uses this copy only on systems without it.
for lib in libstdc++.so.6 libgcc_s.so.1; do
  src="$(g++ -print-file-name=$lib 2>/dev/null || true)"
  [ -f "$src" ] || src="/usr/lib/x86_64-linux-gnu/$lib"
  cp -L "$src" "$APPDIR/usr/lib/$lib"
done
grep -q GLIBCXX_3.4.32 "$APPDIR/usr/lib/libstdc++.so.6" || fail "the build machine's libstdc++ is older than GCC 13"
cp "$WORK/packaging/licenses/ReXGlue-LICENSE.txt" "$WORK/packaging/licenses/THIRD-PARTY-LICENSES.txt" \
  "$WORK/packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt" "$DOC/"
for component in type2-runtime libfuse squashfuse Zstandard zlib mimalloc musl libgcc; do
  grep -qF "AppImage runtime: $component" "$DOC/THIRD-PARTY-LICENSES-LINUX.txt" ||
    fail "THIRD-PARTY-LICENSES-LINUX.txt has no notice for the AppImage runtime component $component"
done
CXX_COPYRIGHT=/usr/share/doc/libstdc++6/copyright
[ -f "$CXX_COPYRIGHT" ] || fail "missing $CXX_COPYRIGHT (the license of the bundled C++ runtime)"
cp "$CXX_COPYRIGHT" "$DOC/libstdc++-copyright.txt"
cp "$WORK/packaging/linux/AppRun" "$WORK/packaging/linux/$APP_ID.desktop" "$APPDIR/"
# The icon is the root file the .desktop names, the .DirIcon file managers show, and the theme the desktop integrates.
# Each program loads its own 256 px icon as its window icon.
ICONS="$WORK/packaging/icons"
THEME="$APPDIR/usr/share/icons/hicolor"
for size in 16 24 32 48 64 128 256 512; do
  mkdir -p "$THEME/${size}x${size}/apps"
  cp "$ICONS/launcher/app-icon-$size.png" "$THEME/${size}x${size}/apps/$APP_NAME.png"
done
cp "$ICONS/launcher/app-icon-256.png" "$THEME/256x256/apps/$BIN_NAME.png"
for i in "${!GAME_PROGRAMS[@]}"; do
  cp "$ICONS/${GAME_ICONS[$i]}/app-icon-256.png" "$THEME/256x256/apps/${GAME_PROGRAMS[$i]}.png"
done
mkdir -p "$THEME/scalable/apps"
cp "$ICONS/app-icon.svg" "$THEME/scalable/apps/$APP_NAME.svg"
cp "$ICONS/launcher/app-icon-256.png" "$APPDIR/$APP_NAME.png"
cp "$ICONS/launcher/app-icon-256.png" "$APPDIR/.DirIcon"
chmod 755 "$APPDIR/AppRun" "${PROGRAMS[@]/#/$APPDIR/usr/bin/}"
chmod 644 "$APPDIR/"*.desktop "$APPDIR/"*.png "$APPDIR/.DirIcon" "$APPDIR/usr/bin/"*.so \
  "$APPDIR/usr/bin/$APP_NAME.toml" "$APPDIR/usr/bin/assets/launcher/"*.png "$APPDIR/usr/lib/"* "$DOC/"*
find "$THEME" -type f -exec chmod 644 {} +
bash "$PATCHED" --check "$SDK" --staged "$APPDIR/usr/bin" || fail "the AppDir does not hold the patched ReXGlue libraries"

echo "== Release check"
scan_appdir "$APPDIR" AppDir "$ROOT" "$WORK" "$HOME"

echo "== AppImage"
TOOLS="$WORK/tools/appimage"
mkdir -p "$TOOLS"
TOOL="$TOOLS/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage"
RUNTIME="$TOOLS/runtime-mua-x86_64"
download "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage" \
  "$TOOL" "$APPIMAGETOOL_SHA256"
bash "$ROOT/tools/build-appimage-runtime.sh" "$RUNTIME" "$WORK/appimage-runtime"
chmod +x "$TOOL"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/$NAME.AppImage"
rm -f "$OUT"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run --no-appstream --runtime-file "$RUNTIME" "$APPDIR" "$OUT" >/dev/null
chmod 755 "$OUT"
bash "$ROOT/tools/build-appimage-runtime.sh" --check "$OUT"

echo "== Final AppImage check"
VERIFY="$(mktemp -d)"
trap 'rm -rf "$VERIFY"' EXIT
cp "$OUT" "$VERIFY/verify.AppImage"
(cd "$VERIFY" && ./verify.AppImage --appimage-extract >/dev/null)
scan_appdir "$VERIFY/squashfs-root" "extracted AppImage" "$ROOT" "$WORK" "$HOME"
[ "$(sha256sum "$VERIFY/squashfs-root/usr/bin/librexruntime.so" | cut -d' ' -f1)" = "$(sha256sum "$BIN/librexruntime.so" | cut -d' ' -f1)" ] ||
  fail "the AppImage does not hold the built runtime"
(cd "$OUT_DIR" && sha256sum "$NAME.AppImage" > "$NAME.sha256")

echo
echo "===================================================="
echo " APPIMAGE READY (nothing was published)"
echo "===================================================="
echo "  AppImage: $OUT ($(du -h "$OUT" | cut -f1))"
echo "  SHA-256:  $(cut -d' ' -f1 "$OUT_DIR/$NAME.sha256")"
echo "  Symbols:  $SYMBOLS (not in the AppImage)"
echo "  Source:   $SOURCE"
echo "  Contents:"
echo "$EXPECTED" | sed 's/^/    /'

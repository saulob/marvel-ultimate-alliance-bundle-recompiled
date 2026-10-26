#!/usr/bin/env bash
# Prepares a clean Ubuntu 20.04 (glibc 2.31) machine, container or WSL distro for tools/package-linux.sh.
# Run it once as root: bash tools/setup-linux-build-env.sh. It needs network access and changes system packages.
#
# libdecor and PipeWire are not packaged for 20.04. SDL loads both at run time but needs their headers to build the
# backends, so they are built here from pinned sources and installed into /usr/local.
set -euo pipefail

CMAKE_VERSION=3.31.8
CMAKE_SHA256=630615d8e98ac33eba7fbe472626dff5c899c85af3c024585ae109166a6909d0
MESON_VERSION=1.3.2
LIBDECOR_TAG=0.2.2
LIBDECOR_COMMIT=7807ae3480f5c6a37c5e8505d94af1e764aaf704
PIPEWIRE_TAG=0.3.48
PIPEWIRE_COMMIT=6c4d3a51583f823b789b0de2df1e36d6c2f8dff8
LLVM_KEY_FINGERPRINT=6084F3CF814B57C1CF12EFD515CF4D18AF4F7421
TOOLCHAIN_KEY_FINGERPRINT=60C317803A41BA51845E371A1E9377A2BA9EF27F

fail() { echo "ERROR: $*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || fail "run as root"
grep -q '^VERSION_ID="20.04"' /etc/os-release || fail "this is not Ubuntu 20.04"
export DEBIAN_FRONTEND=noninteractive

echo "== Base packages"
sed -i 's/^# deb \(.*focal.* universe\)/deb \1/; s/^# deb \(.*focal-updates.* universe\)/deb \1/' /etc/apt/sources.list
apt-get update
apt-get install -y --no-install-recommends ca-certificates curl gnupg git

echo "== LLVM 20 and GCC 13 runtime repositories"
install_key() {  # name fingerprint source-command
  local ring="/usr/share/keyrings/$1.gpg" tmp
  tmp="$(mktemp)"
  eval "$3" > "$tmp"
  gpg --dearmor --yes -o "$ring" "$tmp"
  rm -f "$tmp"
  gpg --no-default-keyring --keyring "$ring" --list-keys --with-colons | grep -q "^fpr:::::::::$2:" ||
    fail "the $1 signing key does not have fingerprint $2"
}
install_key llvm "$LLVM_KEY_FINGERPRINT" "curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key"
install_key ubuntu-toolchain-r "$TOOLCHAIN_KEY_FINGERPRINT" \
  "curl -fsSL 'https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x$TOOLCHAIN_KEY_FINGERPRINT'"
echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/focal/ llvm-toolchain-focal-20 main" \
  > /etc/apt/sources.list.d/llvm-20.list
echo "deb [signed-by=/usr/share/keyrings/ubuntu-toolchain-r.gpg] https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu focal main" \
  > /etc/apt/sources.list.d/ubuntu-toolchain-r.list
apt-get update

echo "== Build packages"
apt-get install -y --no-install-recommends \
  clang-20 lld-20 llvm-20 gcc-13 g++-13 libstdc++-13-dev libstdc++6 \
  ninja-build rsync unzip python3 python3-pip binutils make pkg-config xz-utils patch file gettext-base \
  libgtk-3-dev libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxrender-dev libxss-dev libvulkan-dev libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev \
  libasound2-dev libpulse-dev libgl1-mesa-dev libudev-dev libdbus-1-dev \
  libx11-6 libx11-xcb1 libxcb1 libwayland-client0

echo "== CMake $CMAKE_VERSION and Meson $MESON_VERSION"
CMAKE_TAR="cmake-$CMAKE_VERSION-linux-x86_64.tar.gz"
curl -fsSL -o "/tmp/$CMAKE_TAR" "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/$CMAKE_TAR"
echo "$CMAKE_SHA256  /tmp/$CMAKE_TAR" | sha256sum -c --status - || fail "$CMAKE_TAR does not match its pinned SHA-256"
mkdir -p /opt/cmake
tar -xzf "/tmp/$CMAKE_TAR" -C /opt/cmake --strip-components=1
ln -sf /opt/cmake/bin/cmake /opt/cmake/bin/ctest /opt/cmake/bin/cpack /usr/local/bin/
rm -f "/tmp/$CMAKE_TAR"
pip3 install --no-cache-dir "meson==$MESON_VERSION"

build_from_git() {  # name url tag commit meson-options...
  local name="$1" url="$2" tag="$3" commit="$4" src
  shift 4
  src="$(mktemp -d)"
  git clone -q --depth 1 --branch "$tag" "$url" "$src/$name"
  [ "$(git -C "$src/$name" rev-parse HEAD)" = "$commit" ] || fail "$name $tag is not commit $commit"
  CC=clang-20 CXX=clang++-20 meson setup "$src/build" "$src/$name" --prefix=/usr/local --libdir=lib --buildtype=release "$@"
  ninja -C "$src/build" install
  rm -rf "$src"
}

echo "== libdecor $LIBDECOR_TAG"
build_from_git libdecor https://gitlab.freedesktop.org/libdecor/libdecor.git "$LIBDECOR_TAG" "$LIBDECOR_COMMIT" \
  -Ddemo=false -Ddbus=disabled -Dgtk=disabled

echo "== PipeWire $PIPEWIRE_TAG"
build_from_git pipewire https://gitlab.freedesktop.org/pipewire/pipewire.git "$PIPEWIRE_TAG" "$PIPEWIRE_COMMIT" \
  -Dtests=disabled -Dexamples=disabled -Dman=disabled -Ddocs=disabled -Dgstreamer=disabled -Dsystemd=disabled \
  -Dsession-managers=[] -Dbluez5=disabled -Dvulkan=disabled -Dlibcamera=disabled -Dffmpeg=disabled \
  -Djack=disabled -Dpipewire-jack=disabled -Dpipewire-alsa=disabled -Dspa-plugins=disabled -Dlibusb=disabled
ldconfig

echo
echo "Ready: build with tools/package-linux.sh (see docs/linux.md)."

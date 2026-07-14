#!/usr/bin/env bash
# Build a PORTABLE Unisic AppImage — one that runs on old-glibc distros (Debian
# bookworm, Ubuntu 22.04+, etc.) with NO toolchain and NO recompiling on the
# target. Normally invoked through `scripts/vm-test.sh appimage`, which runs it
# inside an ubuntu:22.04 container; you can also run it directly on any
# Ubuntu-22.04-ish host.
#
# Why the old base matters: an AppImage bundles Qt and libraries but NOT glibc,
# so it only runs where glibc is >= the BUILD host's. Building on Fedora bakes
# in Fedora's newer glibc and the AppImage then FAILS to start on Debian/older
# Ubuntu — exactly the portability problem AppImages are meant to solve. Ubuntu
# 22.04 ships glibc 2.35, low enough for every current distro.
#
# This mirrors .github/workflows/appimage.yml (the release build); keep the two
# in rough sync. Update information / .zsync (differential updates) is CI-only
# and intentionally omitted here — a local build has nothing to publish against.
#
# Env:
#   UNISIC_SRC          source tree (read-only is fine)   default: repo root
#   UNISIC_CACHE        reusable work dir on a REAL fs (Qt, tools, build dir,
#                       AppDir, output). default: $HOME/.cache/unisic-appimage
#                       MUST NOT be on FAT/exFAT: the Qt install and AppDir rely
#                       on symlinks that those filesystems cannot store.
#   QT_VERSION          Qt to fetch via aqt.              default: 6.8.3 (= CI)
#   UNISIC_BUILD_NUMBER set → release flavor (no Developer pane), same as CI.
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.8.3}"
SRC="${UNISIC_SRC:-$(cd "$(dirname "$0")/.." && pwd)}"
CACHE="${UNISIC_CACHE:-$HOME/.cache/unisic-appimage}"
PREFIX="$CACHE/lsq-prefix"     # LayerShellQt + zxing-cpp local install prefix
QT_ROOT="$CACHE/Qt"
TOOLS="$CACHE/tools"
BUILD="$CACHE/build-appimage"  # persisted → ninja rebuilds incrementally
APPDIR="$CACHE/AppDir"
OUT="$CACHE/dist"
mkdir -p "$CACHE" "$PREFIX" "$TOOLS" "$OUT"

has_config() { find "$PREFIX" -name "$1" 2>/dev/null | grep -q .; }

# --- 1. host build deps (fresh every container run; a no-op on a provisioned
#        host). Mirrors appimage.yml's apt list + aqt's python needs.
#        libfontconfig1-dev/libfreetype-dev are EXTRA vs the CI list: the aqt
#        Qt6Gui DT_NEEDEDs libfontconfig.so.1 + libfreetype.so.6, and the GitHub
#        ubuntu-22.04 runner preinstalls them while a bare ubuntu:22.04 image
#        does not — without them the final link fails (undefined Fc*/FT* refs).
# ----------------------------------------------------------------------------
if ! command -v patchelf >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
        ca-certificates curl file git desktop-file-utils patchelf libfuse2 \
        python3 python3-pip \
        cmake ninja-build g++ pkg-config libpipewire-0.3-dev \
        libfontconfig1-dev libfreetype-dev \
        libgl1-mesa-dev libxkbcommon-dev libxcb-cursor0 libxcb-cursor-dev \
        libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
        libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 \
        libwayland-client0 libwayland-cursor0 libwayland-egl1 \
        libtesseract-dev libleptonica-dev \
        libwayland-dev extra-cmake-modules wayland-protocols
fi

# --- 2. Qt via aqtinstall (cached under $QT_ROOT; the 1.5 GB download only
#        happens the first time). qtwaylandcompositor supplies the lib the qt
#        deploy plugin's (misnamed) "waylandcompositor" deployer links to. ----
QT_DIR="$QT_ROOT/$QT_VERSION/gcc_64"
if [ ! -x "$QT_DIR/bin/qmake" ]; then
    pip3 install --no-cache-dir aqtinstall
    aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
        -m qtwaylandcompositor -O "$QT_ROOT"
fi
export PATH="$QT_DIR/bin:$TOOLS:$PATH"
export QMAKE="$QT_DIR/bin/qmake"

# --- 3. LayerShellQt (best effort → HAVE_LAYERSHELL: on-top notification card,
#        overlay-over-fullscreen capture, preview pin). jammy's ECM is too old
#        for layer-shell-qt 6.3.x, so install ECM 6.10 into the same prefix. --
if ! has_config LayerShellQtConfig.cmake; then
    ( set -e
      cd "$CACHE"
      [ -d extra-cmake-modules ] || git clone --depth 1 --branch v6.10.0 \
          https://invent.kde.org/frameworks/extra-cmake-modules.git
      cmake -S extra-cmake-modules -B build-ecm -G Ninja \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_TESTING=OFF -DBUILD_DOC=OFF \
          -DBUILD_MAN_DOCS=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF
      cmake --install build-ecm
      [ -d layer-shell-qt ] || git clone --depth 1 --branch v6.3.5 \
          https://invent.kde.org/plasma/layer-shell-qt.git
      cmake -S layer-shell-qt -B build-lsq -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_PREFIX_PATH="$PREFIX;$QT_DIR" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
          -DKDE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF
      cmake --build build-lsq --parallel
      cmake --install build-lsq
    ) || echo "WARNING: layer-shell-qt build failed — AppImage will lack HAVE_LAYERSHELL"
fi

# --- 4. zxing-cpp (best effort → HAVE_ZXING: QR/barcode decode in the OCR path)
if ! has_config ZXingConfig.cmake; then
    ( set -e
      cd "$CACHE"
      [ -d zxing-cpp ] || git clone --depth 1 --branch v2.2.1 \
          https://github.com/zxing-cpp/zxing-cpp.git
      cmake -S zxing-cpp -B build-zxing -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
          -DBUILD_EXAMPLES=OFF -DBUILD_BLACKBOX_TESTS=OFF -DBUILD_UNIT_TESTS=OFF
      cmake --build build-zxing --parallel
      cmake --install build-zxing
    ) || echo "WARNING: zxing-cpp build failed — AppImage will lack HAVE_ZXING"
fi

# --- 5. configure / build / install Unisic into the AppDir -------------------
has_config LayerShellQtConfig.cmake || echo "NOTE: building without HAVE_LAYERSHELL"
has_config ZXingConfig.cmake       || echo "NOTE: building without HAVE_ZXING"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_PREFIX_PATH="$QT_DIR;$PREFIX" \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" --parallel
rm -rf "$APPDIR"; mkdir -p "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --strip

# --- 6. linuxdeploy → AppImage -----------------------------------------------
version_line="$(grep -m1 '^set(UNISIC_VERSION_STRING' "$SRC/CMakeLists.txt" || true)"
if [ -n "$version_line" ]; then
    VERSION="$(sed -E 's/.*"([^"]+)".*/\1/' <<<"$version_line")"
else
    version_line="$(grep -m1 '^project(unisic VERSION' "$SRC/CMakeLists.txt")"
    VERSION="${version_line#*VERSION }"; VERSION="${VERSION%% *}"
fi
export VERSION

[ -x "$TOOLS/linuxdeploy-x86_64.AppImage" ] || curl -fL -o "$TOOLS/linuxdeploy-x86_64.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
[ -x "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" ] || curl -fL -o "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x "$TOOLS"/linuxdeploy*.AppImage

# Containers have no FUSE — run the tool AppImages by self-extracting.
export APPIMAGE_EXTRACT_AND_RUN=1
export QML_SOURCES_PATHS="$SRC/qml"
# Deploy the Qt Wayland CLIENT stack, else the AppImage falls back to XWayland.
export EXTRA_QT_MODULES="waylandcompositor"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
# Never bundle the build host's libwayland — an old libwayland-client against a
# modern compositor breaks at symbol lookup. Every 2020+ distro ships its own.
export LINUXDEPLOY_EXCLUDED_LIBRARIES="libwayland-client*;libwayland-cursor*;libwayland-egl*"
export LD_LIBRARY_PATH="$QT_DIR/lib:$PREFIX/lib:$PREFIX/lib/x86_64-linux-gnu:$PREFIX/lib64:${LD_LIBRARY_PATH:-}"

LSQ_LIB="$(find "$PREFIX" -name 'libLayerShellQtInterface.so.6.*' -print -quit 2>/dev/null || true)"
EXTRA_LIB_ARGS=()
[ -n "$LSQ_LIB" ] && EXTRA_LIB_ARGS+=(--library "$LSQ_LIB")

cd "$CACHE"
rm -f ./*.AppImage   # drop any AppImage from a previous run in the work dir
"$TOOLS/linuxdeploy-x86_64.AppImage" \
    --appdir "$APPDIR" --plugin qt \
    "${EXTRA_LIB_ARGS[@]}" \
    --exclude-library "libwayland-client*" \
    --exclude-library "libwayland-cursor*" \
    --exclude-library "libwayland-egl*" \
    --output appimage

test -f "$APPDIR/usr/plugins/platforms/libqwayland-generic.so" \
    || { echo "ERROR: Qt wayland platform plugin was not deployed" >&2; exit 1; }
if find "$APPDIR/usr" -name 'libwayland-*' -print | grep -q .; then
    echo "ERROR: host libwayland was bundled into the AppDir" >&2; exit 1
fi

mkdir -p "$OUT"
mv -f ./*.AppImage "$OUT"/
echo "Built: $OUT/$(ls -t "$OUT"/*.AppImage | head -1 | xargs -n1 basename)"

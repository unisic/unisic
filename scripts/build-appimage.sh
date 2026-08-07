#!/usr/bin/env bash
# Build a PORTABLE Unisic AppImage - one that runs with NO toolchain and NO
# recompiling on the target. Normally invoked through `scripts/vm-test.sh
# appimage`, which runs it inside an ubuntu:24.04 container; you can also run it
# directly on any Ubuntu-24.04-ish host.
#
# Why the base image matters: an AppImage bundles Qt and libraries but NOT
# glibc, so it only runs where glibc is >= the BUILD host's. Building on Fedora
# bakes in Fedora's newer glibc and the AppImage then FAILS to start on Debian
# or older Ubuntu. Ubuntu 24.04 ships glibc 2.39, which covers every currently
# supported distro but is above Debian bookworm (2.36) and Ubuntu 22.04 (2.35);
# those two want the Flatpak or a native package. The base moved off jammy with
# .github/workflows/appimage.yml, which is also where noble bought back two
# dependencies (zxing-cpp, tesseract language data) as plain apt packages.
#
# This mirrors .github/workflows/appimage.yml (the release build); keep the two
# in rough sync. Update information / .zsync (differential updates) is CI-only
# and intentionally omitted here - a local build has nothing to publish against.
#
# Unisic has no optional dependencies. Every source build below is REQUIRED: if
# one fails, the script stops instead of quietly producing an AppImage whose UI
# offers a feature the binary does not contain. That is what this script used to
# do for three gates at once (HAVE_LIBINPUT, HAVE_KGUIADDONS and
# HAVE_KWIN_SCREENCAST were all silently off here while the header claimed the
# file mirrored CI).
#
# Env:
#   UNISIC_SRC          source tree (read-only is fine)   default: repo root
#   UNISIC_CACHE        reusable work dir on a REAL fs (Qt, tools, build dir,
#                       AppDir, output). default: $HOME/.cache/unisic-appimage
#                       MUST NOT be on FAT/exFAT: the Qt install and AppDir rely
#                       on symlinks that those filesystems cannot store.
#   QT_VERSION          Qt to fetch via aqt.              default: 6.8.3 (= CI)
#   UNISIC_BUILD_NUMBER set -> release flavor (no Developer pane), same as CI.
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.8.3}"
SRC="${UNISIC_SRC:-$(cd "$(dirname "$0")/.." && pwd)}"
CACHE="${UNISIC_CACHE:-$HOME/.cache/unisic-appimage}"
PREFIX="$CACHE/lsq-prefix"     # local install prefix for the KDE source builds
QT_ROOT="$CACHE/Qt"
TOOLS="$CACHE/tools"
BUILD="$CACHE/build-appimage"  # persisted -> ninja rebuilds incrementally
APPDIR="$CACHE/AppDir"
OUT="$CACHE/dist"
mkdir -p "$CACHE" "$PREFIX" "$TOOLS" "$OUT"

has_config() { find "$PREFIX" -name "$1" 2>/dev/null | grep -q .; }

# A gate that did not end up in $PREFIX is a stop, not a note. See the header.
require_config() {   # <ConfigFile.cmake> <gate> <what breaks>
    has_config "$1" && return 0
    printf 'ERROR: %s was not installed into %s\n' "$1" "$PREFIX" >&2
    printf '       %s would be compiled out, i.e. %s\n' "$2" "$3" >&2
    printf '       Delete %s and re-run to rebuild it from scratch.\n' "$PREFIX" >&2
    exit 1
}

# --- 1. host build deps (fresh every container run; a no-op on a provisioned
#        host). Mirrors appimage.yml's apt list + aqt's python needs.
#        libfontconfig1-dev/libfreetype-dev are EXTRA vs the CI list: the aqt
#        Qt6Gui DT_NEEDEDs libfontconfig.so.1 + libfreetype.so.6, and the GitHub
#        ubuntu-24.04 runner preinstalls them while a bare ubuntu:24.04 image
#        does not - without them the final link fails (undefined Fc*/FT* refs).
#        extra-cmake-modules is deliberately NOT taken from apt (noble ships
#        5.115, the KDE builds below need 6.10), and libfuse2 is libfuse2t64
#        since noble's 64-bit time_t transition.
# ----------------------------------------------------------------------------
if ! command -v patchelf >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
        ca-certificates curl file git desktop-file-utils patchelf libfuse2t64 \
        python3 python3-pip \
        cmake ninja-build g++ pkg-config libpipewire-0.3-dev \
        libfontconfig1-dev libfreetype-dev \
        libx11-dev libxext-dev libxfixes-dev libxcb1-dev \
        libgl1-mesa-dev libxkbcommon-dev libxcb-cursor0 libxcb-cursor-dev \
        libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-render-util0 \
        libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0 \
        libwayland-client0 libwayland-cursor0 libwayland-egl1 \
        libtesseract-dev libleptonica-dev libzxing-dev \
        tesseract-ocr-eng tesseract-ocr-pol tesseract-ocr-osd \
        libinput-dev libudev-dev \
        libwayland-dev wayland-protocols
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

# --- 3. extra-cmake-modules: the KDE build system the three source builds
#        below need, and newer than anything noble packages. ------------------
if ! has_config ECMConfig.cmake; then
    cd "$CACHE"
    [ -d extra-cmake-modules ] || git clone --depth 1 --branch v6.10.0 \
        https://invent.kde.org/frameworks/extra-cmake-modules.git
    cmake -S extra-cmake-modules -B build-ecm -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_TESTING=OFF -DBUILD_DOC=OFF \
        -DBUILD_MAN_DOCS=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF
    cmake --install build-ecm
fi

# --- 4. plasma-wayland-protocols -> HAVE_KWIN_SCREENCAST (KWin-native
#        recording, no portal share dialog). XML plus a cmake config, nothing
#        to compile. Built from source rather than apt-installed because noble
#        ships 1.10.0: above the kit's >= 1.7 floor, below the >= 1.15.0 that
#        kguiaddons v6.10.0 refuses to configure without. Installed BEFORE both
#        of the libraries that want it. ------------------------------------
if ! has_config PlasmaWaylandProtocolsConfig.cmake; then
    cd "$CACHE"
    [ -d plasma-wayland-protocols ] || git clone --depth 1 --branch v1.16.0 \
        https://invent.kde.org/libraries/plasma-wayland-protocols.git
    cmake -S plasma-wayland-protocols -B build-pwp -G Ninja \
        -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DKDE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_LIBDIR=lib
    cmake --install build-pwp
fi

# --- 5. LayerShellQt -> HAVE_LAYERSHELL: on-top notification card,
#        overlay-over-fullscreen capture, preview pin. Noble's
#        liblayershellqtinterface-dev is the Qt5 build (5.27), so this one is
#        built against the aqt Qt. --------------------------------------------
if ! has_config LayerShellQtConfig.cmake; then
    cd "$CACHE"
    [ -d layer-shell-qt ] || git clone --depth 1 --branch v6.3.5 \
        https://invent.kde.org/plasma/layer-shell-qt.git
    cmake -S layer-shell-qt -B build-lsq -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$PREFIX;$QT_DIR" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DKDE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF
    cmake --build build-lsq --parallel
    cmake --install build-lsq
fi

# --- 6. KF6GuiAddons -> HAVE_KGUIADDONS: KSystemClipboard, which is what puts
#        a screenshot into Plasma's clipboard HISTORY rather than only the
#        current slot (issue #51). Noble has no KF6 at all. --------------------
if ! has_config KF6GuiAddonsConfig.cmake; then
    cd "$CACHE"
    [ -d kguiaddons ] || git clone --depth 1 --branch v6.10.0 \
        https://invent.kde.org/frameworks/kguiaddons.git
    cmake -S kguiaddons -B build-kga -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$PREFIX;$QT_DIR" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DKDE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF
    cmake --build build-kga --parallel
    cmake --install build-kga
fi

# --- 7. configure / build / install Unisic into the AppDir -------------------
# A cached $PREFIX from an older run can be missing one of these even though
# every step above was skipped, so they are checked rather than assumed. The
# CMake configure would stop on its own too; this says which of the local
# builds is the one to redo.
require_config PlasmaWaylandProtocolsConfig.cmake HAVE_KWIN_SCREENCAST \
    "KDE users would get the portal share dialog instead of native recording"
require_config LayerShellQtConfig.cmake HAVE_LAYERSHELL \
    "no styled capture card, no overlay above fullscreen windows, no pinned preview"
require_config KF6GuiAddonsConfig.cmake HAVE_KGUIADDONS \
    "screenshots would never enter Plasma's clipboard history"
cd "$CACHE"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_PREFIX_PATH="$QT_DIR;$PREFIX" \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD" --parallel
rm -rf "$APPDIR"; mkdir -p "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --strip

# The gate manifest CMake writes at configure time. Same ten names the CI jobs
# check through .github/actions/assert-gates; here the configure above has
# already hard-failed on anything missing, so this is a printout, not a second
# opinion.
if [ -r "$BUILD/unisic-features.txt" ]; then
    echo "Build gates:"; sed 's/^/  /' "$BUILD/unisic-features.txt"
else
    echo "ERROR: $BUILD/unisic-features.txt was not written - configure did not complete" >&2
    exit 1
fi

# OCR language data (eng + pol + osd, matching the Flatpak and the release
# AppImage). The libraries were always bundled and the data never was, so OCR
# came up empty on any host without a matching langpack. TESSDATA_PREFIX is
# deliberately NOT exported anywhere: it is exclusive, and setting it would
# hide the user's own language packs; OcrEngine probes $APPDIR/usr/share/tessdata
# alongside the host paths instead.
mkdir -p "$APPDIR/usr/share/tessdata"
for lang in eng pol osd; do
    data="$(find /usr/share/tesseract-ocr -name "$lang.traineddata" -print -quit 2>/dev/null || true)"
    if [ -z "$data" ]; then
        echo "ERROR: $lang.traineddata is not installed on this host - the bundle would ship OCR that reads nothing" >&2
        exit 1
    fi
    cp "$data" "$APPDIR/usr/share/tessdata/"
done

# --- 8. linuxdeploy -> AppImage ----------------------------------------------
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

# Containers have no FUSE - run the tool AppImages by self-extracting.
export APPIMAGE_EXTRACT_AND_RUN=1
export QML_SOURCES_PATHS="$SRC/qml"
# Deploy the Qt Wayland CLIENT stack, else the AppImage falls back to XWayland.
export EXTRA_QT_MODULES="waylandcompositor"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
# Never bundle the build host's libwayland - an old libwayland-client against a
# modern compositor breaks at symbol lookup. Every 2020+ distro ships its own.
export LINUXDEPLOY_EXCLUDED_LIBRARIES="libwayland-client*;libwayland-cursor*;libwayland-egl*"
export LD_LIBRARY_PATH="$QT_DIR/lib:$PREFIX/lib:$PREFIX/lib/x86_64-linux-gnu:$PREFIX/lib64:${LD_LIBRARY_PATH:-}"

# Pass the versioned .so of each non-system library explicitly. linuxdeploy
# resolves them as NEEDED entries anyway, but for these three that resolution
# runs through the LD_LIBRARY_PATH line above and nothing else - if it ever
# misses, the result is not a missing feature but an AppImage that dies at load
# time.
EXTRA_LIB_ARGS=()
for pattern in 'libLayerShellQtInterface.so.6*' 'libZXing.so.*' 'libKF6GuiAddons.so.6*'; do
    lib="$(find "$PREFIX" /usr/lib/x86_64-linux-gnu -name "$pattern" -print -quit 2>/dev/null || true)"
    if [ -z "$lib" ]; then
        echo "ERROR: no $pattern found to bundle - the AppImage would not start" >&2
        exit 1
    fi
    EXTRA_LIB_ARGS+=(--library "$lib")
done

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
test -s "$APPDIR/usr/share/tessdata/eng.traineddata" \
    || { echo "ERROR: tessdata went missing from the AppDir" >&2; exit 1; }

mkdir -p "$OUT"
mv -f ./*.AppImage "$OUT"/
echo "Built: $OUT/$(ls -t "$OUT"/*.AppImage | head -1 | xargs -n1 basename)"

#!/usr/bin/env bash
# Build & test Unisic on a VM (GNOME Wayland or any other desktop).
#
# Package flow (easiest — installable artifact, no toolchain on the VM):
#   ./scripts/vm-test.sh deploy user@gnome-vm   # build matching .rpm/.deb, scp,
#                                               # install, flash record border
#   ./scripts/vm-test.sh rpm                    # just build dist/*.rpm
#   ./scripts/vm-test.sh deb                    # just build dist/*.deb
#   ./scripts/vm-test.sh appimage               # portable dist/*.AppImage (built
#                                               # in an ubuntu:22.04 container so
#                                               # it runs on old-glibc distros)
#
# Source flow (dev build on the VM: F8 smoke test + Developer pane):
#   ./scripts/vm-test.sh setup user@gnome-vm    # push sources + deps + build
#   ./scripts/vm-test.sh test  user@gnome-vm    # push + build + flash border
#   ./scripts/vm-test.sh remote user@gnome-vm run
#
# Packages are RELEASE builds (same flavor as CI: UNISIC_BUILD_NUMBER set, so
# no Developer pane) — the record border, --gif, --region etc. all work there.
# The `appimage` command produces the same portable AppImage CI does, locally,
# by driving scripts/build-appimage.sh inside an ubuntu:22.04 container (needs
# podman or docker); build once on the host, run on any VM without a toolchain.
set -euo pipefail

VM_DIR="${UNISIC_VM_DIR:-unisic-src}"   # source-flow repo dir on the VM (under ~)
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${UNISIC_BIN:-$REPO_ROOT/build/unisic}"
PKG_BUILD_DIR="$REPO_ROOT/build-pkg"
DIST_DIR="$REPO_ROOT/dist"
# cpack must package on a REAL filesystem: rpmbuild's brp step hardlinks
# duplicate files, which fails on a FAT/exFAT-mounted repo (this one).
CPACK_WORK="${UNISIC_PKG_WORK:-${XDG_CACHE_HOME:-$HOME/.cache}/unisic-pkg}"

usage() {
    cat <<'EOF'
Usage: scripts/vm-test.sh <command> [args]

Package commands:
  rpm                Build dist/*.rpm (CPack; release flavor)
  deb                Build dist/*.deb (CPack; release flavor)
  appimage           Build a portable dist/*.AppImage inside an ubuntu:22.04
                     container (podman/docker) so it runs on old-glibc distros;
                     Qt/tools/build cache in ~/.cache → later runs are fast
  deploy HOST        Detect the VM's package manager over ssh, build the
                     matching package, scp + install it, then flash the record
                     border on the VM screen and print the checklist

Source commands (run on the machine that should build/test — host or VM):
  deps            Install Fedora build+packaging dependencies (sudo dnf)
  build           Configure (cmake -G Ninja, Release, DEV flavor) into ./build
  run             Launch ./build/unisic inside the current graphical session
  gif             Trigger region-GIF recording (tests the record border live)
  border [secs]   Flash the standalone XWayland record-border helper (default 8 s)
  smoke           How to run the built-in F8 smoke test
  checklist       Manual GNOME Wayland test checklist

Remote source commands (HOST = ssh target, e.g. user@gnome-vm):
  push  HOST            rsync the repo to HOST:~/$UNISIC_VM_DIR
  setup HOST            push + install deps + build on the VM
  test  HOST            push + build + flash the record border in the VM session
  remote HOST <cmd...>  Run any source command above on the VM with session env

Env: UNISIC_VM_DIR — source-flow dir on the VM (default: unisic-src)
     UNISIC_BIN    — binary for run/gif/border (default: ./build/unisic)
EOF
}

# Over ssh there is no WAYLAND_DISPLAY/DISPLAY/DBus address — find the logged-in
# session's sockets so GUI commands land on the VM's screen.
ensure_session_env() {
    export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
        local s
        for s in "$XDG_RUNTIME_DIR"/wayland-*; do
            [[ -S "$s" ]] || continue
            export WAYLAND_DISPLAY="$(basename "$s")"
            break
        done
    fi
    if [[ -z "${DISPLAY:-}" && -d /tmp/.X11-unix ]]; then
        local x
        for x in /tmp/.X11-unix/X*; do
            [[ -S "$x" ]] || continue
            export DISPLAY=":${x##*X}"
            break
        done
    fi
    export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"
    if [[ -z "${WAYLAND_DISPLAY:-}" && -z "${DISPLAY:-}" ]]; then
        echo "No Wayland/X session found — log into the VM's desktop first." >&2
        exit 1
    fi
}

need_bin() {
    [[ -x "$BIN" ]] || { echo "Missing $BIN — run: scripts/vm-test.sh build" >&2; exit 1; }
}

# Release-flavor configure shared by rpm/deb: UNISIC_BUILD_NUMBER set makes
# UNISIC_BUILD != "dev", i.e. the normal stable app, same as CI packages.
configure_pkg() {
    UNISIC_BUILD_NUMBER="${UNISIC_BUILD_NUMBER:-vm}" \
        cmake -B "$PKG_BUILD_DIR" -S "$REPO_ROOT" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$PKG_BUILD_DIR" --parallel
}

newest() { ls -t "$@" 2>/dev/null | head -1; }

# The border flash for machines that only have the PACKAGE installed (no repo,
# no this script): plain sh, finds the session sockets, runs the helper 8 s.
remote_border_snippet() {
    cat <<'EOS'
set -e
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
if [ -z "${WAYLAND_DISPLAY:-}" ]; then
  for s in "$XDG_RUNTIME_DIR"/wayland-*; do
    [ -S "$s" ] && WAYLAND_DISPLAY="$(basename "$s")" && export WAYLAND_DISPLAY && break
  done
fi
if [ -z "${DISPLAY:-}" ] && [ -d /tmp/.X11-unix ]; then
  for x in /tmp/.X11-unix/X*; do
    [ -S "$x" ] && DISPLAY=":${x##*X}" && export DISPLAY && break
  done
fi
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"
echo "Record-border frame for 8 s on the VM screen — check: on top, click-through."
sleep 8 | unisic --record-border-helper "" 0 0 0 0 0 0 0.30 0.30 0.40 0.40 "#C8ACD6"
echo "Helper exited on stdin EOF — OK"
EOS
}

cmd="${1:-}"; shift || true
case "$cmd" in
deps)
    sudo dnf install -y cmake ninja-build gcc-c++ \
        qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-linguist \
        pipewire-devel wl-clipboard rsync \
        rpm-build dpkg
    # ffmpeg: plain Fedora ships ffmpeg-free; RPM Fusion systems have ffmpeg.
    sudo dnf install -y ffmpeg-free || sudo dnf install -y ffmpeg
    ;;
rpm)
    command -v rpmbuild >/dev/null || { echo "rpmbuild missing — run: scripts/vm-test.sh deps" >&2; exit 1; }
    configure_pkg
    cpack --config "$PKG_BUILD_DIR/CPackConfig.cmake" -G RPM -B "$CPACK_WORK"
    mkdir -p "$DIST_DIR"
    cp -f "$(newest "$CPACK_WORK"/*.rpm)" "$DIST_DIR"/
    echo "Built: $(newest "$DIST_DIR"/*.rpm)"
    ;;
deb)
    command -v dpkg-deb >/dev/null || { echo "dpkg-deb missing — run: scripts/vm-test.sh deps" >&2; exit 1; }
    configure_pkg
    # dpkg-shlibdeps resolves library deps against a Debian package database —
    # on a non-Debian build host there is none, so rely on the explicit
    # Depends list from CMakeLists (enough for VM testing).
    shlibdeps=ON
    [[ -e /etc/debian_version ]] || {
        shlibdeps=OFF
        echo "Non-Debian host: building the .deb with SHLIBDEPS=OFF (explicit Depends only)."
    }
    cpack --config "$PKG_BUILD_DIR/CPackConfig.cmake" -G DEB -B "$CPACK_WORK" \
        -D CPACK_DEBIAN_PACKAGE_SHLIBDEPS=$shlibdeps
    mkdir -p "$DIST_DIR"
    cp -f "$(newest "$CPACK_WORK"/*.deb)" "$DIST_DIR"/
    echo "Built: $(newest "$DIST_DIR"/*.deb)"
    ;;
appimage)
    # Portable AppImage, built in an ubuntu:22.04 container so its glibc floor
    # (2.35) is low enough to run on Debian/older Ubuntu — building on this
    # Fedora host would bake in a glibc too new for the VM. The heavy artifacts
    # (Qt via aqt, layer-shell-qt/zxing, the build tree) live in $cache on a
    # REAL fs, never on the exFAT repo (no symlinks there), and persist across
    # runs so only changed sources recompile.
    engine="$(command -v podman || command -v docker || true)"
    [ -n "$engine" ] || {
        echo "Need podman or docker for a portable AppImage (old-glibc base)." >&2
        echo "Install one, e.g.: sudo dnf install -y podman" >&2
        exit 1
    }
    cache="${UNISIC_APPIMAGE_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/unisic-appimage}"
    mkdir -p "$cache" "$DIST_DIR"
    echo "Building portable AppImage in ubuntu:22.04 via $(basename "$engine")…"
    echo "  (first run downloads Qt ~1.5 GB into $cache; later runs are incremental)"
    # label=disable, not :Z — the repo is on exFAT, which cannot store the
    # SELinux xattr a :Z relabel writes (the relabel fails / access is denied).
    "$engine" run --rm \
        --security-opt label=disable \
        -v "$REPO_ROOT":/src:ro \
        -v "$cache":/cache \
        -e UNISIC_SRC=/src -e UNISIC_CACHE=/cache \
        -e QT_VERSION="${QT_VERSION:-6.8.3}" \
        -e UNISIC_BUILD_NUMBER="${UNISIC_BUILD_NUMBER:-vm}" \
        -w /cache \
        docker.io/library/ubuntu:22.04 \
        bash /src/scripts/build-appimage.sh
    art="$(newest "$cache"/dist/*.AppImage)"
    [ -n "$art" ] || { echo "No AppImage produced — see the container output above." >&2; exit 1; }
    cp -f "$art" "$DIST_DIR"/
    out="$DIST_DIR/$(basename "$art")"
    chmod +x "$out" 2>/dev/null || true
    echo "Built: $out"
    echo "Copy to any VM and run (no toolchain needed):"
    echo "  chmod +x $(basename "$out") && ./$(basename "$out")"
    ;;
deploy)
    host="${1:?usage: vm-test.sh deploy HOST}"
    mgr="$(ssh "$host" 'command -v dnf || command -v zypper || command -v apt-get || true')"
    mgr="$(basename "${mgr:-}")"
    case "$mgr" in
    dnf|zypper)
        "$0" rpm
        pkg="$(newest "$DIST_DIR"/*.rpm)"
        scp "$pkg" "$host:"
        if [[ "$mgr" == dnf ]]; then
            ssh -t "$host" "sudo dnf install -y ./$(basename "$pkg")"
        else
            ssh -t "$host" "sudo zypper --non-interactive install --allow-unsigned-rpm ./$(basename "$pkg")"
        fi
        ;;
    apt-get)
        "$0" deb
        pkg="$(newest "$DIST_DIR"/*.deb)"
        scp "$pkg" "$host:"
        ssh -t "$host" "sudo apt-get install -y ./$(basename "$pkg")"
        ;;
    *)
        echo "No dnf/zypper/apt-get on $host (Arch VM: build packaging/arch/PKGBUILD on the VM)." >&2
        exit 1
        ;;
    esac
    echo "Installed on $host — flashing the record border there…"
    remote_border_snippet | ssh "$host" sh -s
    echo
    "$0" checklist
    echo "Launch on the VM: ssh $host, then run: unisic   (or via the app menu)"
    ;;
build)
    cmake -B "$REPO_ROOT/build" -S "$REPO_ROOT" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$REPO_ROOT/build" --parallel
    echo "Built: $REPO_ROOT/build/unisic (dev flavor: F8 smoke test, Developer pane)"
    ;;
run)
    need_bin; ensure_session_env
    exec "$BIN"
    ;;
gif)
    # With an instance already running this forwards over the single-instance
    # socket; otherwise it boots the app and opens the region overlay.
    need_bin; ensure_session_env
    exec "$BIN" --gif
    ;;
border)
    # Standalone record-border helper: the exact surface a region recording
    # shows on GNOME (XWayland override-redirect, click-through). Zeroed
    # screen-matching args -> helper falls back to the sole/primary monitor;
    # frame covers 30%..70% of it. stdin EOF after N seconds ends it.
    need_bin; ensure_session_env
    secs="${1:-8}"
    echo "Record-border frame for ${secs}s — check: visible, stays ABOVE windows you click, clicks pass through."
    sleep "$secs" | "$BIN" --record-border-helper "" 0 0 0 0 0 0 0.30 0.30 0.40 0.40 "#C8ACD6"
    echo "Helper exited on stdin EOF — OK"
    ;;
smoke)
    cat <<'EOF'
Built-in smoke test (dev build only, i.e. the source flow's ./build/unisic):
  1. scripts/vm-test.sh run
  2. Focus the Unisic window, press F8 (or Settings -> Developer -> "Run full
     smoke test"). Expect in the log:
       record border (xwayland helper): PASS      <- the GNOME path
  3. Settings -> Developer also has a "Record border (4 s)" button.
Packages installed via deploy are RELEASE builds — no F8; use "border"/"gif".
EOF
    ;;
checklist)
    cat <<'EOF'
GNOME Wayland manual checklist:
  [ ] border flash                       -> frame + pulsing "REC 0:xx" badge visible
  [ ] click/raise other windows          -> frame STAYS on top, clicks go through it
  [ ] region GIF (unisic --gif)          -> select a region; while recording the
                                            frame surrounds it; stop from the app
  [ ] open the recorded GIF              -> NO frame pixels inside the output
  [ ] Settings -> capabilities           -> "Recording border" available
Notes: GNOME Overview temporarily hides the frame (override-redirect windows) —
it returns after leaving the Overview. Sessions without XWayland have no frame.
EOF
    ;;
push)
    host="${1:?usage: vm-test.sh push HOST}"
    rsync -a --delete --exclude=/build --exclude=/build-pkg --exclude=/dist \
        --exclude=/.git --exclude=/.claude \
        "$REPO_ROOT"/ "$host:$VM_DIR/"
    echo "Pushed to $host:$VM_DIR"
    ;;
setup)
    host="${1:?usage: vm-test.sh setup HOST}"
    "$0" push "$host"
    ssh -t "$host" "cd '$VM_DIR' && ./scripts/vm-test.sh deps && ./scripts/vm-test.sh build"
    echo "Done. Next: $0 test $host   (or: $0 remote $host run)"
    ;;
test)
    host="${1:?usage: vm-test.sh test HOST}"
    "$0" push "$host"
    ssh -t "$host" "cd '$VM_DIR' && ./scripts/vm-test.sh build && ./scripts/vm-test.sh border 8"
    echo
    "$0" checklist
    ;;
remote)
    host="${1:?usage: vm-test.sh remote HOST <cmd...>}"; shift
    [[ $# -ge 1 ]] || { echo "usage: vm-test.sh remote HOST <cmd...>" >&2; exit 1; }
    ssh -t "$host" "cd '$VM_DIR' && ./scripts/vm-test.sh $*"
    ;;
""|-h|--help|help)
    usage
    ;;
*)
    echo "Unknown command: $cmd" >&2
    usage >&2
    exit 1
    ;;
esac

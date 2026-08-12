#!/usr/bin/env bash
# Build, install, run and lint the Unisic flatpak.
#
# The manifest is the same file the Flathub submission repository holds, so it
# builds from a git TAG. --local rewrites that one source to the working tree
# for testing an unreleased change; the copy it writes is never the one to
# submit.
#
# Give the first build time. Unisic has no optional dependencies, so every one
# the runtime does not carry is compiled from source here (x264, ffmpeg,
# leptonica, tesseract, zxing-cpp, wl-clipboard, libssh2 + a static curl,
# layer-shell-qt and the libevdev/mtdev/libinput chain) before flatpak-builder
# even reaches the app. Later runs reuse the cache under the state dir and
# rebuild the app module alone.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
app_id="app.unisic.Unisic"
manifest="$here/$app_id.yml"
builddir="${UNISIC_FLATPAK_BUILDDIR:-$root/build-flatpak}"
statedir="$builddir/.flatpak-builder"

do_local=0
do_run=0
do_lint=0
do_build=1

for arg in "$@"; do
    case "$arg" in
        --local) do_local=1 ;;
        --run) do_run=1 ;;
        --lint) do_lint=1; do_build=0 ;;
        --build) do_build=1 ;;
        -h|--help)
            sed -n '2,14p' "$0"
            printf '\nUsage: %s [--local] [--run] [--lint]\n' "$(basename "$0")"
            exit 0
            ;;
        *) printf 'unknown option: %s\n' "$arg" >&2; exit 2 ;;
    esac
done

need() {
    command -v "$1" >/dev/null 2>&1 || {
        printf 'missing: %s\n' "$1" >&2
        printf 'install it first (Fedora: sudo dnf install %s)\n' "${2:-$1}" >&2
        exit 1
    }
}

runtime_version() {
    # Single source of truth: whatever the manifest says.
    sed -n 's/^runtime-version: *"\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$manifest" | head -1
}

# --user throughout: flathub is commonly configured in BOTH the system and the
# user installation, and an ambiguous remote name makes flatpak stop and ask -
# which hangs an unattended build. The user installation also needs no root.
ensure_runtime() {
    local ver; ver="$(runtime_version)"
    for ref in "org.kde.Platform//$ver" "org.kde.Sdk//$ver"; do
        if ! flatpak info "$ref" >/dev/null 2>&1; then
            printf 'installing %s (this is a large download)\n' "$ref"
            flatpak install --user -y --noninteractive flathub "$ref"
        fi
    done
}

# --local: same manifest with the unisic module built from this checkout.
prepare_manifest() {
    if [ "$do_local" -eq 0 ]; then
        printf '%s' "$manifest"
        return
    fi
    need python3
    local out="$builddir/$app_id.local.yml"
    mkdir -p "$builddir"
    python3 - "$manifest" "$out" "$root" <<'PY'
import re, sys
src, dst, root = sys.argv[1:4]
text = open(src, encoding="utf-8").read()
# Replace ONLY the last source block (the unisic module's git source).
pat = re.compile(
    r"( {4}sources:\n)"
    r"( {6}- type: git\n"
    r" {8}url: https://github\.com/unisic/unisic\.git\n"
    r"(?: {8}.*\n| {8,}.*\n)*)\Z",
    re.M)
if not pat.search(text):
    raise SystemExit("could not find the unisic git source block to rewrite")
text = pat.sub(lambda m: m.group(1) + f"      - type: dir\n        path: {root}\n", text)
open(dst, "w", encoding="utf-8").write(text)
print(f"wrote {dst}", file=sys.stderr)
PY
    printf '%s' "$out"
}

if [ "$do_lint" -eq 1 ]; then
    need flatpak
    if ! flatpak info org.flatpak.Builder >/dev/null 2>&1; then
        printf 'installing org.flatpak.Builder (carries the linter Flathub runs)\n'
        flatpak install --user -y --noninteractive flathub org.flatpak.Builder
    fi
    printf '== manifest lint ==\n'
    flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest "$manifest"
    if [ -d "$builddir/repo" ]; then
        printf '== repo lint ==\n'
        flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo "$builddir/repo"
    else
        printf 'no built repo at %s yet - run without --lint first for the repo check\n' "$builddir/repo"
    fi
    exit 0
fi

if [ "$do_build" -eq 1 ]; then
    need flatpak
    need flatpak-builder
    ensure_runtime
    used_manifest="$(prepare_manifest)"
    printf '== building %s ==\n' "$used_manifest"
    # --user: same reason as ensure_runtime, plus the install target for --run.
    flatpak-builder --force-clean --user --state-dir="$statedir" \
        --repo="$builddir/repo" \
        $([ "$do_run" -eq 1 ] && printf -- '--install') \
        "$builddir/build" "$used_manifest"
fi

if [ "$do_run" -eq 1 ]; then
    printf '== running %s ==\n' "$app_id"
    # A dev/native Unisic may own the single-instance socket; the flatpak has
    # its own $XDG_RUNTIME_DIR view, so both can run side by side.
    flatpak run "$app_id"
fi

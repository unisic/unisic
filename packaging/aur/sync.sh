#!/usr/bin/env bash
# Point the two AUR recipes at a release, regenerate .SRCINFO, optionally
# build-test them and push to the AUR.
#
#   ./sync.sh                     # retarget both recipes at the latest release
#   ./sync.sh --version 0.7.6     # ... at a specific one
#   ./sync.sh --check             # also build both in an Arch container
#   ./sync.sh --check bin         # ... only unisic-bin (seconds, no compile)
#   ./sync.sh --push              # also push to ssh://aur@aur.archlinux.org
#
# .SRCINFO is what the AUR web interface actually reads, and a stale one is
# the classic way to publish a package nobody can find or install. It is
# never hand-written here: makepkg --printsrcinfo generates it, in an Arch
# container when this box is not Arch (the maintainer's is Fedora).
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo='unisic/unisic'
version=''
do_check=0
check_scope='all'
do_push=0
container_image='docker.io/library/archlinux:base-devel'

while [ $# -gt 0 ]; do
    case "$1" in
        --version) version="${2:?--version needs a value}"; shift 2 ;;
        --check)
            do_check=1; shift
            # Optional scope word, so `--check` alone still means everything.
            case "${1:-}" in bin|all) check_scope="$1"; shift ;; esac ;;
        --push) do_push=1; shift ;;
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

say() { printf '\033[1;35m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m==> error:\033[0m %s\n' "$*" >&2; exit 1; }

runtime=''
for c in podman docker; do command -v "$c" >/dev/null 2>&1 && { runtime="$c"; break; }; done

# ---------------------------------------------------------------- release ---
if [ -z "$version" ]; then
    say "Asking GitHub for the latest release"
    version="$(curl -fsSL "https://api.github.com/repos/${repo}/releases/latest" \
               | sed -n 's/.*"tag_name" *: *"v\{0,1\}\([^"]*\)".*/\1/p' | head -n1)"
    [ -n "$version" ] || die "Could not read the latest release tag."
fi
say "Target version: ${version}"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fetch_sha() {
    # $1 = url. Downloads to a temp file and prints its sha256. The AUR needs
    # a real checksum: SKIP is only legitimate for VCS sources.
    local url="$1" out="${tmp}/$(basename "$1")"
    curl -fsSL -o "$out" "$url" || die "Download failed: ${url}"
    sha256sum "$out" | cut -d' ' -f1
}

# The release asset, not the tag archive: GitHub's archives carry no submodule
# content, and from 0.8 the build needs external/unisic-kit. The release
# workflow attaches the complete tarball under this name.
src_url="https://github.com/${repo}/releases/download/v${version}/unisic-${version}.tar.gz"
say "Hashing the source tarball"
src_sha="$(fetch_sha "$src_url")"

# The release asset carries the upstream pkgrel in its file name; it moves
# independently of the AUR pkgrel, so read it rather than assuming 1.
say "Locating the released pacman package"
asset="$(curl -fsSL "https://api.github.com/repos/${repo}/releases/tags/v${version}" \
         | sed -n 's/.*"browser_download_url" *: *"\([^"]*\.pkg\.tar\.zst\)".*/\1/p' \
         | grep -v -- '-debug-' | head -n1)"
[ -n "$asset" ] || die "Release v${version} has no .pkg.tar.zst asset."
asset_name="$(basename "$asset")"
upstream_pkgrel="$(printf '%s' "$asset_name" | sed -n "s/^unisic-${version}-\([0-9]\{1,\}\)-x86_64\.pkg\.tar\.zst$/\1/p")"
[ -n "$upstream_pkgrel" ] || die "Could not parse a pkgrel out of ${asset_name}."
say "Hashing ${asset_name}"
bin_sha="$(fetch_sha "$asset")"

# ------------------------------------------------------------- dependency ---
# The released package records the dependency set it was actually built with.
# If a release adds a library (the X11 recording support pulled in libx11 &
# friends) and nobody updated these recipes, the AUR package installs and then
# fails to start - so a MISSING entry is fatal.
#
# The comparison is deliberately one-way. Extra entries here are how namcap
# findings land: libinput and hicolor-icon-theme are real dependencies the
# upstream package still under-declares, and failing on those would just train
# the maintainer to skip the check.
say "Comparing dependencies against the released package"
pkginfo="$(tar --zstd -xOf "${tmp}/${asset_name}" .PKGINFO 2>/dev/null || true)"
released_deps="$(printf '%s' "$pkginfo" | sed -n 's/^depend = //p' | sort -u)"
recipe_deps="$(sed -n '/^depends=(/,/)/p' "${here}/unisic-bin/PKGBUILD" \
               | tr -d "()'" | sed 's/^depends=//' | tr ' ' '\n' | sed '/^$/d' | sort -u)"
missing="$(comm -13 <(printf '%s\n' "$recipe_deps") <(printf '%s\n' "$released_deps"))"
extra="$(comm -23 <(printf '%s\n' "$recipe_deps") <(printf '%s\n' "$released_deps"))"
if [ -n "$extra" ]; then
    printf '\033[1;33m==> only in the AUR recipes\033[0m (fine - namcap additions):\n'
    printf '      %s\n' $extra
fi
if [ -n "$missing" ]; then
    printf '\033[1;31m==> the release needs packages the recipes do not list:\033[0m\n'
    printf '      %s\n' $missing
    die "Add them to depends=() in BOTH PKGBUILDs, then re-run."
fi

# ----------------------------------------------------------------- rewrite ---
bump() {
    local file="$1" sha="$2"
    sed -i \
        -e "s/^pkgver=.*/pkgver=${version}/" \
        -e "s/^pkgrel=.*/pkgrel=1/" \
        -e "s/^_pkgrel=.*/_pkgrel=${upstream_pkgrel}/" \
        -e "s/^sha256sums=.*/sha256sums=('${sha}')/" \
        "$file"
}
bump "${here}/unisic/PKGBUILD" "$src_sha"
bump "${here}/unisic-bin/PKGBUILD" "$bin_sha"
say "Recipes retargeted at ${version} (upstream pkgrel ${upstream_pkgrel})"

# ---------------------------------------------------------------- .SRCINFO ---
srcinfo() {
    local dir="$1"
    if command -v makepkg >/dev/null 2>&1; then
        (cd "$dir" && makepkg --printsrcinfo > .SRCINFO)
    elif [ -n "$runtime" ]; then
        # Staged through a temp dir on purpose: the checkout may live on a
        # filesystem the container cannot chown (this repo sits on exFAT,
        # where `chown -R` inside the mount fails outright), and makepkg
        # refuses to run as root.
        local stage="${tmp}/srcinfo-$(basename "$dir")"
        mkdir -p "$stage"
        cp "${dir}/PKGBUILD" "$stage/"
        # The trailing chown is not cosmetic: rootless podman maps the
        # container's `b` to a subuid, so files it created come back owned by
        # a uid this user cannot delete, and the EXIT trap fails on them.
        # Container-root maps to the invoking user, so handing them back works.
        "$runtime" run --rm -v "${stage}:/pkg:z" -w /pkg "$container_image" \
            bash -c 'useradd -m b && chown -R b /pkg \
                     && runuser -u b -- makepkg --printsrcinfo > /pkg/.SRCINFO; \
                     chown -R 0:0 /pkg'
        cp "${stage}/.SRCINFO" "${dir}/.SRCINFO"
    else
        die "Need makepkg, podman or docker to generate .SRCINFO."
    fi
}
say "Generating .SRCINFO"
srcinfo "${here}/unisic"
srcinfo "${here}/unisic-bin"

# ------------------------------------------------------------------- check ---
if [ "$do_check" -eq 1 ]; then
    # `bin` exists for CI: the release pipeline's own arch job already compiled
    # and installed this exact tree, so rebuilding it a second time through the
    # source recipe buys ~10 minutes of nothing. The repack has no such twin.
    targets='unisic-bin unisic'
    [ "$check_scope" = bin ] && targets='unisic-bin'
    for pkg in $targets; do
        say "Build-testing ${pkg}"
        stage="${tmp}/check-${pkg}"
        mkdir -p "$stage"
        cp "${here}/${pkg}/PKGBUILD" "$stage/"
        if command -v makepkg >/dev/null 2>&1; then
            # Already on Arch (or inside an Arch CI container): build here.
            # makepkg refuses to run as root, and -s needs passwordless sudo,
            # both of which are the caller's problem to have arranged.
            [ "$(id -u)" -ne 0 ] \
                || die "--check cannot run as root: makepkg refuses. Use an unprivileged user with passwordless sudo."
            ( cd "$stage" && makepkg -s --noconfirm --cleanbuild ) \
                || die "${pkg} failed to build."
            command -v namcap >/dev/null 2>&1 && namcap "$stage"/*.pkg.tar.zst || true
        elif [ -n "$runtime" ]; then
            "$runtime" run --rm -v "${stage}:/pkg:z" -w /pkg "$container_image" bash -c '
                set -e
                # The chown back to container-root (= the invoking user under
                # rootless podman) has to happen even when makepkg fails, or
                # the EXIT trap cannot delete the staging dir afterwards.
                trap "chown -R 0:0 /pkg" EXIT
                pacman -Sy --noconfirm --needed archlinux-keyring >/dev/null
                pacman -Syu --noconfirm --needed base-devel namcap git >/dev/null
                useradd -m b && chown -R b /pkg
                # makepkg -s installs the dependencies through sudo; without a
                # rule it just reports "Could not resolve all dependencies".
                printf "b ALL=(ALL) NOPASSWD: ALL\n" > /etc/sudoers.d/b
                runuser -u b -- makepkg -s --noconfirm --cleanbuild
                namcap ./*.pkg.tar.zst || true
            ' || die "${pkg} failed to build."
        else
            die "--check needs makepkg, podman or docker."
        fi
    done
fi

# -------------------------------------------------------------------- push ---
if [ "$do_push" -eq 1 ]; then
    for pkg in unisic unisic-bin; do
        say "Pushing ${pkg} to the AUR"
        work="${tmp}/aur-${pkg}"
        git clone "ssh://aur@aur.archlinux.org/${pkg}.git" "$work" 2>/dev/null \
            || die "Could not clone ${pkg}.git - is your SSH key registered on your AUR account?"
        cp "${here}/${pkg}/PKGBUILD" "${here}/${pkg}/.SRCINFO" "$work/"
        git -C "$work" add PKGBUILD .SRCINFO
        if git -C "$work" diff --cached --quiet; then
            say "${pkg}: already up to date"
            continue
        fi
        git -C "$work" commit -m "Update to ${version}"
        git -C "$work" push origin HEAD:master
    done
fi

say "Done. Review the diff, then run with --push (or push the clones yourself)."

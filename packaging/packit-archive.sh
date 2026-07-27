#!/usr/bin/env bash
# Source archive for the Packit/COPR builds (.packit.yaml `create-archive`).
#
# Packit's built-in archive step is `git archive`, which writes a gitlink for a
# submodule instead of its content. CMakeLists.txt does
# add_subdirectory(external/unisic-kit), so a COPR build from such an archive
# dies at configure time with "does not contain a CMakeLists.txt file". Every
# release from 0.8 on carries that submodule, hence this action.
#
# Same trick as the release workflow's Arch source tarball: archive the
# superproject and the kit separately, concatenate the UNCOMPRESSED tars, then
# gzip the result. Packit reads the LAST line of stdout as the archive path, so
# nothing else may be printed there.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# Packit exports the version it resolved from the tag; the CMakeLists value is
# the fallback for a local `packit srpm` run outside a release.
version="${PACKIT_PROJECT_VERSION:-}"
if [ -z "$version" ]; then
    version="$(sed -n 's/^set(UNISIC_VERSION_STRING "\([^"]*\)").*/\1/p' CMakeLists.txt)"
fi
[ -n "$version" ] || { printf 'Cannot determine the version\n' >&2; exit 1; }

# unisic.spec's Source0 basename is unisic-<version>.tar.gz, and %autosetup
# expects the tree inside to be prefixed unisic-<version>/.
archive="${PACKIT_PROJECT_ARCHIVE:-unisic-${version}.tar.gz}"
prefix="unisic-${version}"

# Packit's clone has the gitlink but not the kit's objects.
git submodule update --init --recursive >&2

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

git -c safe.directory="$root" \
    archive --format=tar --prefix="${prefix}/" -o "${tmp}/src.tar" HEAD
git -C external/unisic-kit -c safe.directory="${root}/external/unisic-kit" \
    archive --format=tar --prefix="${prefix}/external/unisic-kit/" -o "${tmp}/kit.tar" HEAD
tar --concatenate --file="${tmp}/src.tar" "${tmp}/kit.tar"
gzip -c "${tmp}/src.tar" > "$archive"

printf '%s\n' "$archive"

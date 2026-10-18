#!/usr/bin/env bash
# Stage a portable Windows build: unisic.exe with every DLL, Qt plugin and QML
# module it loads, plus the OCR language data. Run from an MSYS2 UCRT64 shell
# after `cmake --build <build-dir>`:
#
#   packaging/windows/deploy.sh <build-dir> <out-dir> [<zip-dir>]
#
# and packs <out-dir> into <zip-dir>/Unisic-<version>-windows-x86_64.zip plus
# its .sha256 (<zip-dir> defaults to the parent of <out-dir>).
#
# Layout of <out-dir>, the same shape as the AppImage's usr/ so OcrEngine finds
# the language data at <exe dir>/../share/tessdata without a Windows branch:
#   bin/    unisic.exe, DLLs, Qt plugins, qml/, qt.conf
#   share/tessdata/{eng,pol,osd}.traineddata
#
# windeployqt only knows Qt. Everything else (zxing, tesseract, leptonica and
# their image codecs, the gcc runtime) is collected by walking ldd until no
# new /ucrt64 DLL turns up, because plugins pull in libraries the exe does not.
set -euo pipefail

build_dir="${1:?usage: deploy.sh <build-dir> <out-dir> [<zip-dir>]}"
out_dir="${2:?usage: deploy.sh <build-dir> <out-dir> [<zip-dir>]}"
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
prefix="${MINGW_PREFIX:-/ucrt64}"

[ -f "$build_dir/unisic.exe" ] || { echo "error: $build_dir/unisic.exe not built" >&2; exit 1; }

rm -rf "$out_dir"
mkdir -p "$out_dir/bin" "$out_dir/share/tessdata"
cp "$build_dir/unisic.exe" "$out_dir/bin/"

# VideoPreview.qml, the only QtMultimedia importer, lives in the kit: scan both
# QML roots, as the AppImage does.
windeployqt6 \
    --qmldir "$repo_dir/qml" \
    --qmldir "$repo_dir/external/unisic-kit/qml" \
    --no-system-d3d-compiler \
    --no-opengl-sw \
    "$out_dir/bin/unisic.exe"

# Pin Qt to the bundle. Without this an MSYS2 Qt still carries its build
# prefix and, on a machine that has MSYS2 installed, would mix plugins from
# C:/msys64 with the bundled libraries.
cat > "$out_dir/bin/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = .
QmlImports = qml
Translations = translations
EOF

declare -A seen=()
while :; do
    added=0
    while IFS= read -r dll; do
        name="$(basename "$dll")"
        [ -n "${seen[$name]:-}" ] && continue
        seen[$name]=1
        if [ ! -f "$out_dir/bin/$name" ]; then
            cp "$dll" "$out_dir/bin/"
            added=1
        fi
    done < <(find "$out_dir/bin" \( -iname '*.exe' -o -iname '*.dll' \) -print0 \
                 | xargs -0 -r ldd 2>/dev/null \
                 | awk -v p="$prefix/" 'index($3, p) == 1 { print $3 }' | sort -u)
    [ "$added" -eq 1 ] || break
done

# OCR language data. Missing data is a hard error: a bundle without it ships
# an OCR button that reads nothing.
for lang in eng pol osd; do
    data="$prefix/share/tessdata/$lang.traineddata"
    [ -s "$data" ] || { echo "error: $data missing (pacman -S ${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}-tesseract-data-$lang)" >&2; exit 1; }
    cp "$data" "$out_dir/share/tessdata/"
done

# Runtime-loaded pieces the linker cannot vouch for.
for f in bin/platforms/qwindows.dll bin/qml/QtQuick/Controls bin/tls share/tessdata/eng.traineddata; do
    [ -e "$out_dir/$f" ] || { echo "error: $f went missing from the bundle" >&2; exit 1; }
done

echo "staged $(find "$out_dir" -type f | wc -l) files, $(du -sh "$out_dir" | cut -f1), in $out_dir"

# The release asset the Windows self-update installs from, with bin/ and share/
# at the zip root so an update unpacks to updates/<version>/bin/unisic.exe.
# Packed by System32\tar.exe (bsdtar), the same tool UpdateChecker unpacks it
# with, so the CI round trip below is the one every update takes. The .sha256
# beside it is for a manual download; the app checks the digest GitHub
# publishes for the asset itself.
version="$(grep -m1 '^set(UNISIC_VERSION_STRING' "$repo_dir/CMakeLists.txt" | sed -E 's/.*"([^"]+)".*/\1/')"
[ -n "$version" ] || { echo "error: no UNISIC_VERSION_STRING in CMakeLists.txt" >&2; exit 1; }
zip_dir="${3:-$(dirname "$out_dir")}"
mkdir -p "$zip_dir"
zip_dir="$(cd "$zip_dir" && pwd)"
zip_name="Unisic-$version-windows-x86_64.zip"
rm -f "$zip_dir/$zip_name" "$zip_dir/$zip_name.sha256"
"$(cygpath -S -u)/tar.exe" -a -c -f "$(cygpath -w "$zip_dir/$zip_name")" \
    -C "$(cygpath -w "$out_dir")" bin share
printf '%s  %s\n' "$(sha256sum < "$zip_dir/$zip_name" | cut -d' ' -f1)" "$zip_name" \
    > "$zip_dir/$zip_name.sha256"
echo "packed $zip_dir/$zip_name ($(du -h "$zip_dir/$zip_name" | cut -f1))"

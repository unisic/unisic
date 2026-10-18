#!/usr/bin/env python3
# Regenerate src/platform/win/unisic.ico from resources/icons/unisic.svg, the
# same artwork the Linux packages install. Run from an MSYS2 UCRT64 shell (it
# needs rsvg-convert, pacman -S mingw-w64-ucrt-x86_64-librsvg) after the SVG
# changes, and commit the result: the build only reads the .ico, so neither
# tool is a build dependency.
#
#   python3 packaging/windows/make-icon.py
#
# Every size is stored as a PNG entry, which Windows reads since Vista. The
# sizes are the ones Explorer, the taskbar, the Start menu and the installer
# ask for at 100 % to 200 % scaling.
import os
import struct
import subprocess
import sys

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)

repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
svg = os.path.join(repo, "resources", "icons", "unisic.svg")
out = os.path.join(repo, "src", "platform", "win", "unisic.ico")

images = []
for size in SIZES:
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), svg],
                         check=True, stdout=subprocess.PIPE).stdout
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"rsvg-convert gave no PNG for {size} px")
    images.append((size, png))

# ICONDIR, then one ICONDIRENTRY per image, then the image data in order. A
# width or height of 256 is written as 0.
header = struct.pack("<HHH", 0, 1, len(images))
offset = len(header) + 16 * len(images)
entries = b""
for size, png in images:
    dim = 0 if size == 256 else size
    entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset)
    offset += len(png)

with open(out, "wb") as f:
    f.write(header + entries + b"".join(png for _, png in images))
print(f"wrote {out}: {len(images)} sizes, {offset} bytes")

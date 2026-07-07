# Unisic

ShareX-like screenshot & screen-recording tool for Linux **Wayland** (KDE Plasma first, portal-portable), with a custom SwiftUI-inspired UI. C++20 / Qt 6 / QML.

## Features

- **Capture**: full screen (all monitors), interactive region, active window — via `org.kde.KWin.ScreenShot2` (silent, native-res) with automatic fallback to `org.freedesktop.portal.Screenshot`.
- **Annotate during region selection** (ShareX-style): arrows, rectangles, ellipses, pen, text directly on the frozen live overlay before the capture is finalized; arrow keys nudge the selection, Enter/double-click captures, Esc cancels.
- **Full editor** auto-opened after capture: pen, line, arrow, rect, ellipse, text, blur, pixelate, highlight, numbered step markers, crop, undo/redo (`Ctrl+Z`/`Ctrl+Shift+Z`), save (`Ctrl+S`), copy (`Ctrl+C`), upload (`Ctrl+U`).
- **Modular upload destinations** (`~/.config/unisic/destinations.json`, `.sxcu`-like schema with `$text$` / `$json:path$` / `$regex:…$` URL extraction). Built-ins: catbox.moe, 0x0.st. FTP/SFTP via `type: "curl"`. Uploaded link is auto-copied to the clipboard; history keeps it for re-copy.
- **GIF recording**: portal ScreenCast → PipeWire → ffmpeg (lossless intermediate, then two-pass palettegen/paletteuse). Region or full screen, FPS / max duration / quality options. Optional MP4 output.
- Tray icon with quick actions, global hotkeys via KGlobalAccel (defaults: `Meta+Shift+1/2/3`, `Meta+Shift+G`), capture history with thumbnails.

## Build (Fedora)

```sh
sudo dnf install -y cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
    pipewire-devel ffmpeg wl-clipboard

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

`pipewire-devel` is optional at build time (recording is disabled without it). `ffmpeg` and `wl-clipboard` are runtime dependencies (encoding; robust Wayland clipboard).

## Notes

- On first run Unisic drops `org.unisic.Unisic.desktop` into `~/.local/share/applications` — required by KWin to authorize the silent `ScreenShot2` interface. Without it captures still work through the portal (with a one-time consent dialog).
- Mandatory palette: `#17153B` / `#2E236C` / `#433D8B` / accent `#C8ACD6`.

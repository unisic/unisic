<div align="center">

<img src="resources/icons/unisic.svg" width="160" height="160" alt="Unisic" />

# Unisic

**The screenshot & screen-recorder Linux Wayland deserves.**

Silent capture · Native KWin · Annotate · Record · Upload · Zero telemetry · GPLv3

[![Download latest release](https://img.shields.io/badge/Download_Latest_Release-C8ACD6?style=for-the-badge&logo=linux&logoColor=17153B)](https://github.com/unisic/unisic/releases/latest)

<p>
  </a>
  <img alt="Linux Wayland" src="https://img.shields.io/badge/Linux-Wayland-000?style=for-the-badge&color=2E236C">
 </a>
  <img alt="License" src="https://img.shields.io/badge/license-GPLv3-000?style=for-the-badge&color=433D8B">
</p>

<sub>
  <a href="#install">Install</a> ·
  <a href="#features">Features</a> ·
  <a href="#build-from-source">Build</a> ·
  <a href="#configuration">Configure</a>
</sub>

<br />
<br />

<!-- Drop UI screenshots here, e.g.:
<img src="docs/screenshots/editor.jpg" width="49%" alt="Unisic post-capture editor" />
<img src="docs/screenshots/overlay.jpg" width="49%" alt="Unisic region overlay with annotation" />
-->

</div>

Screenshot and screen-recording tool for **Linux Wayland**, built KDE Plasma–first but portable to any desktop through xdg-desktop-portal. Fully custom, SwiftUI-inspired Qt Quick UI — no Kirigami or Breeze widgets. **C++20 / Qt 6.5 / QML.**

Unisic captures screenshots and screen recordings, lets you annotate them before and after capture, and pushes the result wherever you want — clipboard, disk, or an upload destination — all on legitimate Wayland APIs (portals, KWin ScreenShot2, PipeWire, KGlobalAccel).

## Install

Grab the latest build from the [**Releases**](https://github.com/unisic/unisic/releases/latest) page, or [build from source](#build-from-source). Requires a Wayland session with `xdg-desktop-portal` (and a backend such as `xdg-desktop-portal-kde`); recording additionally needs PipeWire and `ffmpeg`. Silent native KWin capture is a KDE Plasma bonus — everything else works through portals on any Wayland desktop.

## Features

### Capture

Three capture modes, all wired to the tray, global hotkeys, and CLI flags:

- **Full screen** — every monitor grabbed as a single stitched virtual-desktop image.
- **Region** — interactive selection on a frozen, per-monitor overlay, with a live dimension readout and on-canvas annotation before the shot is finalized.
- **Active window** — the focused window (KWin), or a window you pick from the portal dialog on the fallback path.

Capture runs silently at native resolution through KWin's `org.kde.KWin.ScreenShot2` DBus API when available, and automatically falls back to `org.freedesktop.portal.Screenshot` when it is not (e.g. non-KDE desktops, or before the app is installed). A configurable pre-capture delay (default 200 ms) and optional cursor inclusion (off by default) apply to every mode.

### Annotate on the overlay (before capture)

While selecting a region you can draw directly on the frozen screenshot with **pen, arrow, rectangle, ellipse, and text**; annotations are burnt into the final crop. The toolbar appears once you have dragged a selection, with undo, colour swatches, and a colour picker.

- Drag to select · arrow keys nudge the selection by 1 px (10 px with **Shift**)
- **Ctrl+A** select the whole screen · **Ctrl+Z** undo
- **Enter** or **double-click** to capture · **Esc** to cancel

### Post-capture editor

The editor opens automatically after every capture (on by default; can be turned off). Twelve tools: **pen, line, arrow, rectangle, ellipse, text, highlight, blur, pixelate, smart eraser, numbered step markers, and crop**. Any tool can be hidden from the toolbar in settings. Everything is composited in image-pixel space, so the export is exactly what you see.

- **Ctrl+Z** undo · **Ctrl+Shift+Z** / **Ctrl+Y** redo
- **Ctrl+S** save · **Ctrl+C** copy · **Ctrl+U** upload · **Esc** close

### Recording

Recording runs through the ScreenCast portal → PipeWire → ffmpeg, encoding a lossless x264rgb intermediate first for clean output. Region recording streams the full monitor and crops in ffmpeg (portals can't select a sub-region). Recordings have no audio.

- **GIF** — region or full screen (two-pass palettegen/paletteuse). Options: FPS (default 15), max duration (default 30 s), quality (Fast · Balanced · Best).
- **Video** — full screen, region, or a single window → **MP4** (H.264) or **WebM** (VP9). Options: FPS (default 30), max duration (unlimited by default), CRF quality (default 20).

Recording requires PipeWire; without `pipewire-devel` at build time the app still builds, but recording is disabled at runtime.

### Upload & history

Modular upload destinations live in `~/.config/unisic/destinations.json` (a simple JSON custom-uploader schema). Result and deletion URLs are extracted from the response with `$text$`, `$json:path$`, or `$regex:pattern$`.

- **HTTP** — multipart POST/PUT with custom form fields and headers.
- **`type: "curl"`** — FTP / FTPS / SFTP via the `curl` CLI (`-T`, optional `-u` auth).
- Ships two built-ins: **catbox.moe** (active by default) and **0x0.st**.

The uploaded link is auto-copied to the clipboard (optionally opened in the browser). Every capture is recorded in a persistent history with thumbnails (image / GIF / video), kept at `~/.local/share/unisic/`. Upload-after-capture is off by default; save, clipboard, editor, and history each fire independently.

### Tray, hotkeys & themes

- **Tray menu**: capture region / full screen / window, record video (region / window), record GIF (region), stop recording, open, quit. Left-click opens the main window.
- **Global hotkeys** via KGlobalAccel over DBus, editable in Unisic's settings or KDE's Shortcuts KCM. Defaults:

  | Action | Shortcut |
  | --- | --- |
  | Capture full screen | `Meta+Shift+1` |
  | Capture region | `Meta+Shift+2` |
  | Capture active window | `Meta+Shift+3` |
  | Record GIF (region) | `Meta+Shift+G` |
  | Record video (region) | `Meta+Shift+R` |

- **9 themes**: `system` (follows KDE's light/dark scheme and accent colour, default), plus the `unisic` brand palette, `dark`, `light`, `catppuccin-mocha`, `catppuccin-latte`, `dracula`, `nord`, and `gruvbox`. Icons use freedesktop names — bundled monochrome SVGs recoloured to the theme, or the system icon theme under `system`.

## Build from source

Needs **Qt 6.5+**, CMake, and Ninja. Install the dependencies for your distro, then run the [common build](#build) below.

### Arch Linux

Build and install the package straight from the shipped PKGBUILD:

```sh
sudo pacman -S --needed base-devel qt6-base qt6-declarative qt6-svg qt6-wayland \
    pipewire ffmpeg wl-clipboard xdg-desktop-portal cmake ninja pkgconf

# From a source tarball / checkout:
cd packaging/arch && makepkg -si
```

Or install the dependencies and use the common build below.

### Debian / Ubuntu

Requires Qt 6.5+, so use a release that ships it (Debian trixie / Ubuntu 24.10+; older releases pin Qt 6.4 and won't configure).

```sh
sudo apt install cmake ninja-build g++ pkg-config \
    qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland \
    libpipewire-0.3-dev ffmpeg wl-clipboard xdg-desktop-portal
```

### Fedora

```sh
sudo dnf install -y cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qtwayland \
    pipewire-devel ffmpeg wl-clipboard xdg-desktop-portal
```

### Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

- The PipeWire dev package (`pipewire`/`libpipewire-0.3-dev`/`pipewire-devel`) is **optional** at build time — the build still succeeds, but recording is disabled at runtime.
- Runtime deps: `ffmpeg` (encoding), `wl-clipboard` (robust Wayland clipboard), `xdg-desktop-portal` (+ a backend such as `xdg-desktop-portal-kde`), and the Qt Wayland platform plugin (`qt6-wayland`).

## Run

```sh
unisic --fullscreen        # capture full screen
unisic --region            # interactive region capture
unisic --window            # active window
unisic --gif               # start a region GIF recording
unisic --export-settings <file>   # write all settings + destinations to JSON
unisic --import-settings <file>   # load them back
```

With no arguments Unisic starts in the background with a tray icon and its main window.

## Configuration

- Settings, destinations, and history are stored under `~/.config/unisic/` and `~/.local/share/unisic/`.
- Save filenames come from a template with tokens `%date%`, `%time%`, `%datetime%`, `%unix%`, `%rand%` (default `Unisic_%date%_%time%`); image format is PNG, JPG, or WebP.
- Full settings can be exported/imported as JSON (all effective values plus upload destinations and theme).

## Notes

- On first run Unisic installs `org.unisic.Unisic.desktop` into `~/.local/share/applications` (declaring `X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2`) and rebuilds the KDE service cache — this is what authorizes the silent KWin capture path. Without it, captures still work through the portal, with the desktop's own consent dialog.
- Brand palette: primary `#17153B`, secondary `#2E236C`, tertiary `#433D8B`, accent `#C8ACD6`.
- Licensed under the GNU GPL v3.

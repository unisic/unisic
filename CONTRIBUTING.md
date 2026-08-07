# Contributing to Unisic

Issues and pull requests welcome. Bug reports are the most valuable thing you can send - include your desktop, compositor, GPU and logs so an exotic-compositor edge case can be reproduced.

## Project layout

[AGENTS.md](AGENTS.md) is the full contributor guide (architecture, subsystem map, conventions, correctness landmines). In short:

- `src/` - C++20 / Qt 6 core: `capture/`, `record/`, `editor/`, `overlay/`, `upload/`, `update/`, `hotkeys/`, `theme/`.
- `qml/` - hand-built Qt Quick UI (no Kirigami/Breeze; QQuickStyle forced to Basic).
- `resources/` - icons, `.desktop`, AppStream metadata.
- `packaging/` - Arch PKGBUILD and OBS specs; Debian/RPM come from CPack in `CMakeLists.txt`.
- `.github/workflows/` - CI and the release pipeline.

## Building

Needs **Qt 6.5+**, CMake, Ninja and the full dependency set below. Copy the block for your distro whole: every package in it is required, and a missing one stops `cmake -B build` at configure time with the package name in the error rather than producing a Unisic with a feature quietly absent.

**Fedora**

```sh
sudo dnf install -y cmake ninja-build gcc-c++ pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qtwayland \
    qt6-qtwayland-devel qt6-qtbase-private-devel qt6-qttools-devel \
    plasma-wayland-protocols-devel \
    pipewire-devel tesseract-devel leptonica-devel zxing-cpp-devel \
    layer-shell-qt-devel wayland-devel kf6-kguiaddons-devel \
    libinput-devel systemd-devel \
    libX11-devel libXext-devel libXfixes-devel libxcb-devel \
    ffmpeg curl zip wl-clipboard xdg-desktop-portal \
    tesseract-langpack-eng tesseract-langpack-pol tesseract-osd
```

**Debian / Ubuntu** (trixie / 24.10+ for Qt 6.5+)

```sh
sudo apt install cmake ninja-build g++ pkg-config \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-wayland \
    qt6-wayland-dev qt6-base-private-dev qt6-tools-dev qt6-l10n-tools \
    plasma-wayland-protocols \
    libpipewire-0.3-dev libtesseract-dev libleptonica-dev libzxing-dev \
    liblayershellqtinterface-dev libwayland-dev libkf6guiaddons-dev \
    libinput-dev libudev-dev \
    libx11-dev libxext-dev libxfixes-dev libxcb1-dev \
    ffmpeg curl zip wl-clipboard xdg-desktop-portal \
    tesseract-ocr-eng tesseract-ocr-pol tesseract-ocr-osd
```

**Arch**

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf \
    qt6-base qt6-declarative qt6-svg qt6-wayland qt6-tools \
    plasma-wayland-protocols \
    pipewire tesseract leptonica zxing-cpp \
    layer-shell-qt wayland kguiaddons libinput \
    libx11 libxext libxfixes libxcb \
    ffmpeg curl zip wl-clipboard xdg-desktop-portal \
    tesseract-data-eng tesseract-data-pol tesseract-data-osd
```

**Build & run**

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

**None of those are optional.** Unisic has no optional dependencies at all, and no feature that compiles out: leave one package out and the build stops at configure time naming it, rather than handing you a binary whose interface still offers what it cannot do. What each buys, so the list is not just a wall of names: **PipeWire** is the recording backend on Wayland; **Tesseract + Leptonica** are OCR and **zxing-cpp** is the QR/barcode read inside it (the `eng`/`pol`/`osd` traineddata packages are what OCR actually reads, so they belong in the list too); **LayerShellQt + wayland** are the styled capture card, which on Plasma has no other route; **KF6GuiAddons** (`KSystemClipboard`) is what puts a copied screenshot into KDE Plasma's Klipper clipboard **history** instead of only the current clipboard slot; **libinput + libudev** are the pressed-key badge and the click ripple; **Qt LinguistTools** (`qt6-qttools-devel` / `qt6-tools-dev` + `qt6-l10n-tools` / `qt6-tools`) bakes the seven translation catalogs into the binary; **Qt WaylandClient + the Qt6 GUI private headers + plasma-wayland-protocols** are KWin-native recording, which is what keeps a clip on Plasma from opening the portal share dialog every time. The X11 packages are two separate defines over overlapping libraries: `libX11` + `libXext` + `libXfixes` are `HAVE_X11` (XShm screen recording on an X11 session), `libX11` + `libxcb` are `HAVE_X11_HOTKEYS` (`XGrabKey` global hotkeys there). They stay separate probes so the error can name the right package, not because either may be off.

## Development approach

Unisic is developed with agentic AI assistance following [AGENTS.md](AGENTS.md). Every generated change is read line by line and reviewed by a maintainer before it lands - the tooling speeds things up, but nothing merges unread.

## Pull requests

- Branch off `main`; keep it to one logical change per PR.
- State what you tested and on which compositor.
- Keep the shipped binary lean - no new heavy dependencies (Kirigami, Boost, KDE Frameworks...) without discussion. A dependency that does land is mandatory from the start and goes into every packaging recipe in the same PR; see the dependency policy in [AGENTS.md](AGENTS.md).
- Match the surrounding code style; run a build before opening the PR.

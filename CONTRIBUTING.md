# Contributing to Unisic

Issues and pull requests welcome. Bug reports are the most valuable thing you can send — include your desktop, compositor, GPU and logs so an exotic-compositor edge case can be reproduced.

## Project layout

[AGENTS.md](AGENTS.md) is the full contributor guide (architecture, subsystem map, conventions, correctness landmines). In short:

- `src/` — C++20 / Qt 6 core: `capture/`, `record/`, `editor/`, `overlay/`, `upload/`, `update/`, `hotkeys/`, `theme/`.
- `qml/` — hand-built Qt Quick UI (no Kirigami/Breeze; QQuickStyle forced to Basic).
- `resources/` — icons, `.desktop`, AppStream metadata.
- `packaging/` — Arch PKGBUILD and OBS specs; Debian/RPM come from CPack in `CMakeLists.txt`.
- `.github/workflows/` — CI and the release pipeline.

## Building

Needs **Qt 6.5+**, CMake and Ninja.

**Fedora**

```sh
sudo dnf install -y cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel qt6-qtwayland \
    pipewire-devel ffmpeg wl-clipboard xdg-desktop-portal
```

**Debian / Ubuntu** (trixie / 24.10+ for Qt 6.5+)

```sh
sudo apt install cmake ninja-build g++ pkg-config \
    qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland \
    libpipewire-0.3-dev ffmpeg wl-clipboard xdg-desktop-portal
```

**Arch**

```sh
sudo pacman -S --needed base-devel qt6-base qt6-declarative qt6-svg qt6-wayland \
    pipewire ffmpeg wl-clipboard xdg-desktop-portal cmake ninja pkgconf
```

**Build & run**

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

PipeWire, Tesseract and onnxruntime dev packages are optional — without them the app builds with recording / OCR / U-2-Net background removal disabled (the dependency-free heuristic cutout still works). For AI background removal install `onnxruntime-devel` (Fedora) or `onnxruntime` (Arch) and rebuild; the ~4.5 MB U-2-Net model is fetched on first use.

## Development approach

Unisic is developed with agentic AI assistance following [AGENTS.md](AGENTS.md). Every generated change is read line by line and reviewed by a maintainer before it lands — the tooling speeds things up, but nothing merges unread.

## Pull requests

- Branch off `main`; keep it to one logical change per PR.
- State what you tested and on which compositor.
- Keep the shipped binary lean — no new heavy dependencies (Kirigami, Boost, KDE Frameworks…) without discussion; see the dependency policy in [AGENTS.md](AGENTS.md).
- Match the surrounding code style; run a build before opening the PR.

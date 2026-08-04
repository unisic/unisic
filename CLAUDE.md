# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

It is an **index, not the manual**. Every subsystem note that used to live here now lives in `docs/dev/` - open the matching file **before** changing that area. Those notes are hard-won fixes, not background reading: working from this index alone will re-break things that were already solved.

| Working on | Read first |
|---|---|
| `src/**` subsystems (capture, editor/canvas, overlay, record, upload, history, notify, diag, theme) | `docs/dev/architecture.md` |
| `qml/**`, kit components, accessibility, layout rules | `docs/dev/ui-kit.md` |
| Recording, global hotkeys, clipboard, after-capture pipeline, file routing, CLI, developer mode | `docs/dev/pipelines.md` |
| `scripts/install.sh`, `packaging/**`, release workflows, `UpdateChecker` | `docs/dev/packaging.md` |
| Any user-facing string | `docs/dev/i18n.md` |
| Repo map, prime directives, correctness landmines, verification + done checklist, commit rules | `AGENTS.md` |

## Build & Run

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

Requires `qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel pipewire-devel` (Fedora) plus runtime `ffmpeg` and `wl-clipboard`. `pipewire-devel` is optional - without it the build succeeds but recording is disabled (`HAVE_PIPEWIRE` guard). Same pattern for OCR: `tesseract-devel leptonica-devel` + a langpack like `tesseract-langpack-pol` enable it (`HAVE_TESSERACT`); `zxing-cpp-devel` additionally enables QR/barcode decoding inside the OCR path (`HAVE_ZXING` - a code in the region copies its payload instead of OCR-ing its pixels). Two more of the same shape live in the kit and gate the X11 session paths: `libX11-devel libXext-devel libXfixes-devel` (`HAVE_X11`, XShm recording) and `libX11-devel libxcb-devel` (`HAVE_X11_HOTKEYS`, `XGrabKey`). The capture popup positions itself by filling the screen and masking input to the card (no layer-shell dependency). `ctest --test-dir build` runs the QtTest targets in `tests/` (pure-logic units - version compare, shortcut format, annotation canvas, history filter); everything compositor-bound is covered by the in-app smoke test instead.

The shared design system is a git submodule: `external/unisic-kit` (QML module `Unisic.Kit`). Shared code belongs THERE, never copied back into this tree; clone/build with `--recurse-submodules`.

## Non-negotiables

Each line is the short form of a rule whose full reasoning is in the linked file. When a change touches one, read the reasoning before deciding it does not apply.

- **Mandatory UI palette**: Primary `#17153B` (main window/panel backgrounds), Secondary `#2E236C` and Tertiary `#433D8B` (secondary elements, hover/active), Accent `#C8ACD6` (action buttons, attention). All colors come from the `Theme.qml` singleton's tokens - never a hardcoded hex.
- **Wayland-legit capture paths only on a Wayland session**: xdg-desktop-portal Screenshot/ScreenCast, KWin `org.kde.KWin.ScreenShot2` as a KDE-specific enhancement, PipeWire as the video backend, KGlobalAccel over D-Bus for global shortcuts, `QDBusInterface`/`QDBusConnection` for D-Bus. Never route a Wayland session through an X11 path, screen-scraping, or a hack around the security model.
- **X11 is a supported second target (since 0.8), not a hack**: on an `xcb` session recording grabs frames with `X11ShmGrabber` (XShm + XFixes, in the kit, `HAVE_X11` **and** `HAVE_PIPEWIRE` - it feeds the same sampler/encoder), and global hotkeys use `X11Hotkeys` (`XGrabKey`, `HAVE_X11_HOTKEYS`) where KGlobalAccel is absent. Screenshots/overlay/editor/OCR/history/upload were always session-agnostic. Recording a single **window** stays Wayland-only (no picker without the portal). Best effort: Wayland is the daily-driven target, X11 is verified by a pass over the features (README "X11 support"; `AGENTS.md` §1, §3).
- **Every user-facing string** is `qsTr()`/`tr()` AND translated in all seven `i18n/unisic_{en,pl,es,it,fr,ru,de}.ts` files, with no `unfinished` markers left behind (`docs/dev/i18n.md`).
- **Every new user-facing feature/path is wired into BOTH** the F8 smoke test (`AppContext::runSmokeTest`) and its own per-action button in the Settings Developer pane (`docs/dev/pipelines.md`).
- **Every user-facing change is documented** in `resources/CHANGELOG.md`, bilingual EN/PL, under the current beta heading (never a new heading mid-beta).
- **No em dashes** anywhere in text written here - UI strings, comments, docs, commits. Plain `-` only.
- **The installed `.desktop` `Exec` must be an absolute path**, or KWin refuses `ScreenShot2` on every packaged install and silently falls back to the portal (`docs/dev/architecture.md`).
- **Never a QSettings group named "general"/"General"** - it collides with INI's magic General section and resets settings to defaults every launch (`docs/dev/pipelines.md`).
- **Never compile an Imgur Client-ID in** - it identifies the application, so one shipped ID caps every user together and makes whoever registered it answerable for strangers' uploads (`docs/dev/architecture.md`).
- **No Kirigami/Breeze**: the UI is a fully custom design system, QQuickStyle forced to "Basic" (`docs/dev/ui-kit.md`).
- **Removed on purpose, do not reintroduce without asking**: smart pick + `ObjectDetector`, U-2-Net background removal and the whole `HAVE_ONNX` path (dropped in 0.7.1b), and the guided tour. The tour is closed for good, not parked: a tour worth having points at the real UI from outside it, and Wayland gives no client its own window position or anything else's, so it can never be more than a slideshow inside our own window - `UWelcome` already covers that ground. Do not propose it, and do not re-add `UTour` strings to `i18n/**`.

## What Unisic Is

Unisic is a screenshot and screen-recording tool for **Linux Wayland**, prioritizing KDE Plasma/KWin but portable via xdg-desktop-portal, and running on an **X11 session** as a best-effort second target (see Non-negotiables). Tech stack: **C++17+ with Qt 6, Qt Quick/QML UI**.

Core features (in rough build-priority order):

1. **Capture**: full screen (multi-monitor), interactive region (live selection with dimension readout), specific window.
2. **On-overlay annotation** during region selection (arrows, shapes, text) *before* the capture is finalized.
3. **Post-capture editor** auto-opened after every capture: arrows, lines, rectangles, ellipses, freehand, text, blur/pixelate, crop, numbered step markers, highlight, undo/redo.
4. **Modular upload destinations** (custom HTTP/FTP/SFTP/API + public services), auto-copy URL to clipboard, upload history.
5. **GIF screen recording** (distinct from normal video recording): record region/full screen, convert to .gif via ffmpeg; options for FPS, area, max duration.

Lower priority: tray icon + quick menu, configurable global hotkeys per capture mode, capture history with thumbnails.

## Planning Workflow

`.claude/workflows/unisic-plan-research.js` (invocable as the `unisic-plan-research` workflow/skill) runs a three-phase multi-agent plan: 7 parallel research agents (portals, KWin/hotkeys, PipeWire, overlay selection, capture-tool parity, upload stack, GIF encoding) → 2 independent architecture designs (pragmatic vs KDE-native lens) → a synthesis into one final architecture + milestone plan. Its `REQ` constant is the canonical requirements statement for the project.

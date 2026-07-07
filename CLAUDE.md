# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

Requires `qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel pipewire-devel` (Fedora) plus runtime `ffmpeg` and `wl-clipboard`. `pipewire-devel` is optional — without it the build succeeds but recording is disabled (`HAVE_PIPEWIRE` guard). Same pattern for OCR: `tesseract-devel leptonica-devel` + a langpack like `tesseract-langpack-pol` enable it (`HAVE_TESSERACT`). The capture popup positions itself by filling the screen and masking input to the card (no layer-shell dependency). No test suite yet.

## Architecture

- `src/AppContext.*` — facade exposed to QML as context property `App`; owns all subsystems and the after-capture pipeline (editor/save/clipboard/upload/history), tray icon, hotkey dispatch.
- `src/capture/` — backends: `KWinScreenShot2` (silent, needs the installed .desktop file's `X-KDE-DBUS-Restricted-Interfaces`; auto-falls back on auth failure) and `PortalScreenshot`; `PortalRequest` implements the portal Request/Response handle pattern; `ScreenCastSession` drives the ScreenCast portal for recording; `CaptureManager` picks the backend.
- `src/editor/AnnotationCanvas` — the core drawing surface (QQuickPaintedItem): all tools, selection, undo/redo, and final compositing in **image-pixel space** (DPR forced to 1.0). Used by both the overlay and the editor; export is exactly what's rendered.
- `src/overlay/OverlayController` — freezes each screen (one capture per monitor), spawns one fullscreen `OverlayWindow.qml` per screen, returns annotated crop (screenshots) or physical-pixel region (GIF).
- `src/record/` — `PipeWireGrabber` (libpipewire thread, keeps latest SHM frame) → `GifRecorder` samples at fixed FPS into ffmpeg stdin (lossless x264rgb temp), then two-pass palettegen/paletteuse GIF conversion.
- `src/upload/UploadManager` — destinations from `~/.config/unisic/destinations.json` (.sxcu-like; `$text$`/`$json:path$`/`$regex:$` extraction); `type: "curl"` shells out to curl for FTP/SFTP.
- `src/theme/ThemeController` — module QML singleton (`QML_SINGLETON`) owning the selected theme (persisted `ui/theme`) and bridging the live system palette (`qApp->palette()` + `QStyleHints::colorScheme()`) for the "system" theme. `ThemeController::instance()` shares the one engine-created object with `IconImageProvider`. **Do NOT `qmlRegisterSingletonInstance` into the `Unisic` URI** — that clobbers the module's other C++ QML_ELEMENT types (e.g. `AnnotationCanvas` becomes "not a type").
- `src/theme/IconImageProvider` — `image://icon/<name>?color=%23RRGGBB&sz=NN&v=<rev>`. System theme → `QIcon::fromTheme` (Breeze fallback theme set in main), else the bundled monochrome SVG in `resources/icons/sym/` recolored via `CompositionMode_SourceIn`. Pixmap provider ⇒ `UIcon` must stay `asynchronous: false` (runs on GUI thread; `QIcon::fromTheme`/`qApp->palette()` aren't thread-safe).
- `qml/` — fully custom SwiftUI-like design system (**no Kirigami/Breeze**; QQuickStyle forced to "Basic"): `Theme.qml` singleton (9 palettes computed from tokens; keep property names stable — 19 files depend on them) + `ToolCatalog.qml` singleton (single source of the annotation tool set; both toolbars build from `visibleFor(ctx, hiddenTools)`) + `components/` (UButton, USwitch, UIcon, `UComboBox` on a `Popup` parented to `Overlay.overlay` so dropdowns escape Flickable clipping, …). Icons are `iconName` (freedesktop names) via `UIcon`, not emoji. Windows are created from C++ via QQmlComponent with per-window QQmlContext (`editorSession`, `overlayController` context properties).

Recording: one `GifRecorder` parameterized by `Output{Gif,Mp4,WebM}` + `SourceType{Screen,Region,Window}`; Window source passes `types=WINDOW` to `ScreenCastSession` (portal window picker). GIF (region/screen) and video (screen/region/window → MP4/WebM) are separate sidebar pages/`AppContext` entry points. Global hotkeys: `GlobalHotkeys::defineAction` registers with KGlobalAccel flag `IsDefault(0x8)` only (verified on Plasma 6: binds active on fresh install AND preserves KCM edits; a separate `SetPresent(0x2)` call *clears* the binding on that daemon); `setShortcut` (`SetPresent`) is pushed only on a user edit/import.

Deliberate choices: keep working via portals on non-KDE; clipboard mirrored through `wl-copy` when present (Wayland reliability); recording region = full-monitor stream cropped in ffmpeg (portals can't select regions).

After-capture pipeline: every enabled action (copy/save/upload/editor) fires independently and immediately in `AppContext::finishCapture` — the editor never blocks the others. Filenames come from `AppContext::makeFileName()` (template tokens `%date% %time% %datetime% %unix% %rand%`, format/quality from settings). Settings export/import (`AppContext::exportSettings/importSettings`) serializes all `Settings` Q_PROPERTYs via the metaobject (not raw QSettings keys — those omit defaults) plus destinations. CLI: `unisic --fullscreen|--region|--window|--gif|--export-settings <f>|--import-settings <f>`.

## What Unisic Is

Unisic is a planned ShareX-like screenshot and screen-recording tool for **Linux Wayland**, prioritizing KDE Plasma/KWin but portable via xdg-desktop-portal. Tech stack: **C++17+ with Qt 6, Qt Quick/QML UI**.

Required features (in rough build-priority order):

1. **Capture**: full screen (multi-monitor), interactive region (live selection with dimension readout), specific window.
2. **On-overlay annotation** during region selection (arrows, shapes, text) *before* the capture is finalized — ShareX-style.
3. **Post-capture editor** auto-opened after every capture: arrows, lines, rectangles, ellipses, freehand, text, blur/pixelate, crop, numbered step markers, highlight, undo/redo.
4. **Modular upload destinations** (custom HTTP/FTP/SFTP/API + public services), auto-copy URL to clipboard, upload history.
5. **GIF screen recording** (distinct from normal video recording): record region/full screen, convert to .gif via ffmpeg; options for FPS, area, max duration.

Lower priority: tray icon + quick menu, configurable global hotkeys per capture mode, capture history with thumbnails.

## Hard Constraints

- **Wayland-legit capture paths only**: xdg-desktop-portal Screenshot/ScreenCast, KWin `org.kde.KWin.ScreenShot2` DBus API as a KDE-specific enhancement, PipeWire as the video backend, KGlobalAccel over DBus for global shortcuts, `QDBusInterface`/`QDBusConnection` for DBus. No X11-only capture hacks.
- **Mandatory UI palette**: Primary `#17153B` (main window/panel backgrounds), Secondary `#2E236C` and Tertiary `#433D8B` (secondary elements, hover/active), Accent `#C8ACD6` (action buttons, attention).

## Planning Workflow

`.claude/workflows/unisic-plan-research.js` (invocable as the `unisic-plan-research` workflow/skill) runs a three-phase multi-agent plan: 7 parallel research agents (portals, KWin/hotkeys, PipeWire, overlay selection, ShareX parity, upload stack, GIF encoding) → 2 independent architecture designs (pragmatic vs KDE-native lens) → a synthesis into one final architecture + milestone plan. Its `REQ` constant is the canonical requirements statement for the project.

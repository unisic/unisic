# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

Requires `qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel pipewire-devel` (Fedora) plus runtime `ffmpeg` and `wl-clipboard`. `pipewire-devel` is optional — without it the build succeeds but recording is disabled (`HAVE_PIPEWIRE` guard). Same pattern for OCR: `tesseract-devel leptonica-devel` + a langpack like `tesseract-langpack-pol` enable it (`HAVE_TESSERACT`); `zxing-cpp-devel` additionally enables QR/barcode decoding inside the OCR path (`HAVE_ZXING` — a code in the region copies its payload instead of OCR-ing its pixels). The capture popup positions itself by filling the screen and masking input to the card (no layer-shell dependency). No test suite yet.

## Architecture

- `src/AppContext.*` — facade exposed to QML as context property `App`; owns all subsystems and the after-capture pipeline (editor/save/clipboard/upload/history), tray icon, hotkey dispatch.
- `src/capture/` — backends: `KWinScreenShot2` (silent, needs the installed .desktop file's `X-KDE-DBUS-Restricted-Interfaces`; auto-falls back on auth failure) and `PortalScreenshot`; `PortalRequest` implements the portal Request/Response handle pattern; `ScreenCastSession` drives the ScreenCast portal for recording; `CaptureManager` picks the backend.
- `src/editor/AnnotationCanvas` — the core drawing surface (QQuickPaintedItem): all tools, selection, undo/redo, and final compositing in **image-pixel space** (DPR forced to 1.0). Used by both the overlay and the editor; export is exactly what's rendered.
- `src/overlay/OverlayController` — freezes each screen (one capture per monitor), spawns one fullscreen `OverlayWindow.qml` per screen, returns annotated crop (screenshots) or physical-pixel region (GIF). **Smart pick** (`capture/smartPick`, default ON): with the plain selection tool, `AnnotationCanvas` runs `ObjectDetector::detect` async and a CLICK (no drag) selects the detected object rect under the cursor (hover highlights it + size/level badge); the scroll wheel cycles the nesting chain inner element → containers → whole screen (always the outermost candidate). Detection: Sobel at ≤1024 px + per-side border-coverage filter (kills non-rectangular blobs) + full-res edge snapping (±scale px accurate). Dragging still draws a manual rect. The old overlay "Pick object" chip is superseded (ToolCatalog `overlay: false`; the segmentation code remains for the tool enum).
- `src/record/` — `PipeWireGrabber` (libpipewire thread, keeps latest SHM frame) → `GifRecorder` samples at fixed FPS into ffmpeg stdin (lossless x264rgb temp), then two-pass palettegen/paletteuse GIF conversion.
- `src/upload/UploadManager` — destinations from `~/.config/unisic/destinations.json` (.sxcu-like; `$text$`/`$json:path$`/`$regex:$` extraction); `type: "curl"` shells out to curl for FTP/SFTP.
- `src/theme/ThemeController` — module QML singleton (`QML_SINGLETON`) owning the selected theme (persisted `ui/theme`) and bridging the live system palette (`qApp->palette()` + `QStyleHints::colorScheme()`) for the "system" theme. `ThemeController::instance()` shares the one engine-created object with `IconImageProvider`. **Do NOT `qmlRegisterSingletonInstance` into the `Unisic` URI** — that clobbers the module's other C++ QML_ELEMENT types (e.g. `AnnotationCanvas` becomes "not a type").
- `src/theme/IconImageProvider` — `image://icon/<name>?color=%23RRGGBB&sz=NN&v=<rev>`. System theme → `QIcon::fromTheme` (Breeze fallback theme set in main), else the bundled monochrome SVG in `resources/icons/sym/` recolored via `CompositionMode_SourceIn`. Pixmap provider ⇒ `UIcon` must stay `asynchronous: false` (runs on GUI thread; `QIcon::fromTheme`/`qApp->palette()` aren't thread-safe).
- `qml/` — fully custom SwiftUI-like design system (**no Kirigami/Breeze**; QQuickStyle forced to "Basic"): `Theme.qml` singleton (9 palettes computed from tokens; keep property names stable — 19 files depend on them) + `ToolCatalog.qml` singleton (single source of the annotation tool set; both toolbars build from `visibleFor(ctx, hiddenTools)`) + `components/` (UButton, USwitch, UIcon, `UComboBox` on a `Popup` parented to `Overlay.overlay` so dropdowns escape Flickable clipping, …). Icons are `iconName` (freedesktop names) via `UIcon`, not emoji. Windows are created from C++ via QQmlComponent with per-window QQmlContext (`editorSession`, `overlayController` context properties).

Recording: one `GifRecorder` parameterized by `Output{Gif,Mp4,WebM}` + `SourceType{Screen,Region,Window}`; Window source passes `types=WINDOW` to `ScreenCastSession` (portal window picker). GIF (region/screen) and video (screen/region/window → MP4/WebM) are separate sidebar pages/`AppContext` entry points. Global hotkeys: `GlobalHotkeys::defineAction` registers with KGlobalAccel flag `IsDefault(0x8)` only (preserves KCM edits; a separate `SetPresent(0x2)` call *clears* the binding on that daemon). **The IsDefault reply lies**: when the daemon has an active binding stored as "none" it echoes the requested keys while filling only the default column — the action stays dead. So `AppContext::defineHotkeys` runs `hotkeyBindStatus` on EVERY launch: real `shortcutKeys` queries; any action reported unbound while the app stores a key gets re-asserted via `setShortcut` (`SetPresent|NoAutoloading`, 0x6); a differing bound key is synced into the UI. A deliberate KCM unbind made while the app runs still sticks (the change signal empties the stored string). User edits/imports also push `setShortcut`.

Deliberate choices: keep working via portals on non-KDE; clipboard mirrored through `wl-copy` when present (Wayland reliability); recording region = full-monitor stream cropped in ffmpeg (portals can't select regions).

After-capture pipeline: every enabled action (copy/save/upload/editor) fires independently and immediately in `AppContext::finishCapture` — the editor never blocks the others. Two extra post-capture affordances live here too: (a) **quick-copy grace window** — when `copyToClipboard` is off but `quickCopyAfterCapture` is on, `armQuickCopy` temporarily binds a `quick-copy` KGlobalAccel action to Ctrl+C for 2s (NoAutoloading, so it's cleared unconditionally at startup in case of a crash mid-window) and `disarmQuickCopy` releases it; KGlobalAccel-only. (b) **floating preview** — clicking a capture-notification thumbnail calls `CaptureNotification::preview()` → `AppContext::openPreview` which dumps the full image to a temp PNG and opens `PreviewWindow.qml` with a `PreviewController`. With layer-shell the surface is FULLSCREEN with the wl input region masked to the visible card (`setInputRect`, same pattern as the capture popup) so the card drags as a plain QML item — the only smooth option, layer surfaces can't be system-moved and per-event margin repositioning diverges; pin toggles overlay/top layer. Fallback: normal frameless window + `startSystemMove` + stays-on-top hint. Opacity slider fades the card item (NOT `Window.opacity` — qtwayland silently ignores it). Filenames come from `AppContext::makeFileName()` (template tokens `%date% %time% %datetime% %unix% %rand%`, format/quality from settings). Settings export/import (`AppContext::exportSettings/importSettings`) serializes all `Settings` Q_PROPERTYs via the metaobject (not raw QSettings keys — those omit defaults) plus destinations. Settings keys: NEVER put a key in a QSettings group named "general"/"General" — that name collides with INI's magic General section (serializes `[%General]`, parses back as `"General/..."`, case-sensitive reads miss → resets to defaults every launch). General-tab settings are top-level bare keys (plain `[General]` section) for exactly this reason. CLI: `unisic --fullscreen|--region|--window|--gif|--export-settings <f>|--import-settings <f>`.

Developer mode: a `UNISIC_DEV_BUILD` CMake flag (auto ON for local builds — i.e. `UNISIC_BUILD == "dev"` — OFF for CI releases) exposes a **Developer tab** in Settings (`App.devBuild`). It shows detected compositor capabilities (`App.capNativeNotification/capCustomNotification/capRecordBorder`), a full smoke test bound to **F8** (`AppContext::runSmokeTest`), and a **per-action button** for each path. **REQUIREMENT for every new user-facing feature/path: wire it into BOTH** (a) the sequential smoke test in `AppContext::runSmokeTest` (as a pass/fail/skip step) **and** (b) a dedicated per-action button in the SettingsPage Developer pane (reuse the public `Q_INVOKABLE`, or add a small `devTest*()` for non-capture paths). This keeps the dev harness an exhaustive, hand-checkable inventory of what the app can do. In a release build, capability-gated options are shown greyed-out with an explanation rather than hidden.

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

# AGENTS.md

Contributor guide for **any** AI coding agent working on Unisic — Cursor, Aider, Zed, Codex, Continue, Cline, Windsurf, GitHub Copilot Agent, Claude Code, and humans reading over their shoulder. This is the canonical, tool-agnostic contract. If your tool also reads `CLAUDE.md`, that file carries the same rules plus Claude-specific per-subsystem deep notes — read both; where they overlap they agree, and if they ever disagree, **this file and the actual code win.**

> **Read this whole file before your first edit.** Unisic is small on purpose. Most of the hard problems here are *invisible* — Wayland capture authorization, D-Bus signal ownership, QSettings persistence quirks, Qt object lifetimes. A change that "looks obviously correct" has repeatedly been the wrong one. The gotchas below were each paid for in hours of debugging; treat them as landmines, not trivia.

---

## 1. What Unisic is (and is not)

Unisic is a **ShareX-like screenshot + screen-recording tool for Linux Wayland**, prioritizing KDE Plasma/KWin but portable via `xdg-desktop-portal`. Stack: **C++20, Qt 6 (6.5+), Qt Quick / QML**, fully custom UI. GPLv3. Zero telemetry.

Core workflow it owns end-to-end: press hotkey → annotate *on the selection overlay before the shot is taken* → post-capture editor (arrows, shapes, text, blur/pixelate, crop, numbered steps, object cutout) → route the result to clipboard / disk / a custom upload destination with the link auto-copied → or record the same region as GIF/MP4/WebM.

**It is NOT:** a general image editor, a cloud service, a cross-platform app, an X11 tool, or a kitchen-sink utility. Every feature request is measured against "does a screenshot/record/share workflow genuinely need this?" The answer is usually no.

### Non-negotiable product constraints

- **Wayland-legit capture paths ONLY.** `xdg-desktop-portal` Screenshot/ScreenCast, KWin `org.kde.KWin.ScreenShot2` D-Bus (KDE enhancement), `wlr-screencopy` via `grim` (wlroots), `org.gnome.Shell.Screenshot` (niri/GNOME direct), PipeWire for video, KGlobalAccel / portal GlobalShortcuts for hotkeys. **No X11-only capture hacks. No screen-scraping. No compositor-specific hacks that bypass the security model.**
- **Mandatory UI palette** — do not introduce off-palette colors:
  - Primary `#17153B` (window/panel backgrounds)
  - Secondary `#2E236C`, Tertiary `#433D8B` (secondary elements, hover/active)
  - Accent `#C8ACD6` (action buttons, attention)
  - All UI colors flow from `qml/Theme.qml` tokens — never hardcode a hex in a component.
- **Works without KDE.** KDE gets the fully silent native path; everything must degrade gracefully to portals on GNOME/wlroots/niri. Never assume KWin, `kglobalacceld`, or Breeze is present.
- **Zero telemetry, no network calls except user-configured uploads.** No analytics, no auto-update phone-home, no crash reporters that transmit.

---

## 2. Prime directives

These are the reasons this file exists. Every change is judged against them, in order:

1. **Lightweight.** Small binary, small dependency set, fast startup, low idle RAM/CPU. Unisic lives in the tray all day; it must be invisible when not in use.
2. **Correct.** No regressions in capture/hotkey/settings persistence — the load-bearing, hard-to-test subsystems. When in doubt, verify on a real Wayland session (see §11).
3. **No leaks.** Qt makes ownership easy to get wrong. Every `new` needs an owner; every temp file, D-Bus handle, and PipeWire/ffmpeg resource needs a teardown path.
4. **No feature creep.** Adding code is a cost. The best PR is often a smaller diff, or a deletion.

If a change trades any of these away, it needs an explicit, written justification in the PR — not a silent assumption.

---

## 3. Build, run, and dependencies

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

- **Toolchain:** CMake ≥ 3.21, a C++20 compiler, Ninja.
- **Required:** `qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel` (+ `Widgets DBus Network Concurrent QuickControls2` Qt modules). Runtime: `ffmpeg`, `wl-clipboard`, `xdg-desktop-portal` (+ a portal backend), `qt6-qtwayland`.
- **Optional, compile-time guarded** — the build *succeeds without them* and prints a warning; the feature is disabled at runtime:
  - `pipewire-devel` → `HAVE_PIPEWIRE` → GIF/screen recording. Without it, recording is off.
  - `tesseract-devel leptonica-devel` + a langpack (e.g. `tesseract-langpack-eng`) → `HAVE_TESSERACT` → OCR ("copy text from capture"). Gates `App.ocrAvailable` in QML.
- **Runtime helpers shelled out, not linked:** `ffmpeg` (GIF/video encode), `curl` (FTP/SFTP uploads), `grim` (wlroots/niri capture), `wl-copy` (clipboard mirror), `kbuildsycoca6` (KDE service-cache rebuild). Treat all as optional-at-runtime: detect with `QStandardPaths::findExecutable`, degrade gracefully, never crash if absent.

**Dependency policy (this is a lightweight app):**
- **Do not add a new library** — Qt module, system `.so`, or bundled source — without a strong justification and maintainer sign-off. Prefer shelling out to an already-required helper, or a small self-contained implementation, over a new link-time dependency.
- **Do not pull in Kirigami, Breeze, KDE Frameworks, Boost, or any heavy framework.** The UI is deliberately hand-built on Qt Quick Basic style. `QQuickStyle::setStyle("Basic")` is set in `main.cpp` for exactly this reason.
- New optional features that need a heavy dep must follow the `HAVE_PIPEWIRE`/`HAVE_TESSERACT` compile-time-guard pattern so the default build stays lean.
- Keep `CPACK_STRIP_FILES` working; don't add anything that bloats the shipped binary or the Debian/RPM/Arch/AppImage runtime dep lists in `CMakeLists.txt` / `packaging/` without updating them.

---

## 4. Repository map

```
src/
  main.cpp              Entry point: QApplication, single-instance socket, signal handlers,
                        QQuickStyle=Basic, KWin .desktop authz setup, CLI dispatch, batch modes.
  AppContext.{h,cpp}    THE facade exposed to QML as context property `App`. Owns every
                        subsystem + the after-capture pipeline (editor/save/clipboard/upload/
                        history), tray icon, hotkey dispatch, filename templating. Largest file.
  Settings.{h,cpp}      All persisted settings as Q_PROPERTYs. Metaobject-driven export/import.
  ConfigPath.h          UnisicConfig::filePath() — the ONE config file path.

  capture/              KWinScreenShot2 (silent KDE), PortalScreenshot, GnomeScreenshot (niri/
                        GNOME), GrimScreenshot (wlroots), PortalRequest (portal handle pattern),
                        ScreenCastSession (ScreenCast portal for recording), CaptureManager
                        (backend selection + per-desktop fallback chain).
  editor/              AnnotationCanvas (the core QQuickPaintedItem drawing surface — all tools,
                        selection, undo/redo, compositing in IMAGE-PIXEL space, DPR forced 1.0;
                        used by BOTH overlay and editor), EditorSession.
  overlay/             OverlayController (freezes each screen, one fullscreen OverlayWindow per
                        monitor), ObjectDetector (object-cutout region detection).
  record/              PipeWireGrabber (libpipewire thread, keeps latest SHM frame), GifRecorder
                        (samples to ffmpeg; also drives MP4/WebM).
  upload/              UploadManager (.sxcu-like destinations.json; $text$/$json:$/$regex:$;
                        type:"curl" shells to curl for FTP/SFTP).
  history/             HistoryStore (capture history + thumbnails).
  hotkeys/             GlobalHotkeys (KGlobalAccel/DBus), PortalGlobalShortcuts (non-KDE).
  theme/               ThemeController (module QML singleton; system-palette bridge),
                        IconImageProvider (image://icon/... recolored SVGs / QIcon::fromTheme).
  notify/              CaptureNotification.
  ocr/                 OcrEngine (HAVE_TESSERACT only).

qml/
  Theme.qml            SINGLETON. 9 palettes computed from tokens. Property names are load-
                        bearing — ~19 files depend on them. Keep names stable.
  ToolCatalog.qml      SINGLETON. Single source of the annotation tool set; both toolbars
                        build from visibleFor(ctx, hiddenTools).
  Main.qml, OverlayWindow.qml, EditorWindow.qml, NotificationPopup.qml
  pages/               CapturePage, RecordPage, GifPage, HistoryPage, DestinationsPage, SettingsPage
  components/          UButton, UIcon(Button), UCard, USwitch, USlider, UTextField, UComboBox,
                        USpinBox, UShortcutRecorder, SidebarItem, ToolChip, ColorDot, MiddleScroll

resources/icons/sym/   Bundled monochrome SVGs recolored by IconImageProvider.
packaging/             Arch PKGBUILD (+ Debian/RPM via CPack in CMakeLists).
.github/               release.yml workflow, issue templates.
```

The whole `src/` tree is ~8.9k lines. It is meant to stay comprehensible in an afternoon. If a file balloons, that is a smell — prefer extracting a focused helper over piling onto `AppContext.cpp`.

---

## 5. Lightweight discipline

Concrete rules, not vibes:

- **Startup path is sacred.** `main.cpp` runs a tight sequence; a CLI capture (`unisic --region`) forwards to the running instance over a local socket and must return in tens of milliseconds. Do not add blocking work, network calls, disk scans, or synchronous D-Bus round-trips to the startup or CLI-dispatch path. Heavy setup goes behind `QTimer::singleShot` / lazy init / a worker.
- **Batch modes (`--export-settings`/`--import-settings`) run headless and must never boot the tray, QML engine, or hotkeys.** Keep them that way.
- **Idle cost near zero.** No polling timers that run when nothing is happening. Prefer signal/slot and D-Bus signals over polling. If you must poll, justify the interval.
- **Lazy-construct expensive subsystems.** Don't spin up PipeWire, the QML editor, or capture backends until first use. Tear them down when done.
- **No speculative generality.** No plugin frameworks, config abstraction layers, or "we might need it later" indirection. YAGNI is the default answer.
- **Prefer deletion.** Dead code, unused settings, half-finished features, and commented-out blocks are liabilities in a lightweight tool. Remove them.
- **Watch the shipped size.** Bundled resources (icons, QML) are compiled into the binary via `qt_add_resources` / `qt_add_qml_module`. Don't embed large assets. SVG icons only, kept minimal.

---

## 6. Memory-safety and leak discipline (Qt)

Qt's ownership model is the #1 source of leaks and use-after-free here. Rules:

- **Every `QObject` gets a parent, or an explicit owner.** Parented objects die with the parent — that is the primary lifetime mechanism. A parentless `new QObject` that nobody stores is a leak.
- **Never `delete` a QObject that has pending signals/events queued to it. Use `deleteLater()`.** See the single-instance socket handling in `main.cpp` (`QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater)`) — follow that pattern.
- **Disconnect or scope lambdas that capture raw pointers.** A lambda connected to a long-lived object that captures a soon-to-die pointer is a dangling-capture crash. Give the connection a context object (the 3-arg `connect`) so it auto-disconnects when that object dies.
- **Temp files must be cleaned up on every exit path**, including signals. `main.cpp` installs SIGINT/SIGTERM/SIGHUP self-pipe handlers *specifically so destructors run* (QSettings flush, temp-file cleanup, tray teardown). If you create temp files (GIF/video intermediates, capture scratch), ensure they are removed on success, failure, AND signal-triggered quit. Prefer `QTemporaryFile`/`QTemporaryDir` with RAII scope.
- **Child processes (`ffmpeg`, `curl`, `grim`) must not outlive the app and must not leak.** Set `SOCK_CLOEXEC`-style hygiene (see the self-pipe: CLOEXEC so children don't inherit it). Kill/await child processes on teardown; don't leave zombie ffmpeg encoders after a cancelled recording.
- **PipeWire / D-Bus resources are manual.** `PipeWireGrabber` runs its own thread and holds SHM frames — join the thread and release buffers on stop. D-Bus `PortalRequest` handles follow the Request/Response pattern; close/clean the handle, don't leak the object path subscription.
- **Threads: `Qt::Concurrent` / worker threads must be joined or their futures awaited before teardown.** Don't touch GUI objects from a worker thread (see the `IconImageProvider` note in §9 — `QIcon::fromTheme`/`qApp->palette()` are not thread-safe).
- **QImage/QPixmap are cheap-copy (implicitly shared) but big when detached.** In `AnnotationCanvas` and the record path, avoid gratuitous deep copies of full-resolution frames in hot loops. Reuse buffers (see the `memcpy` mask trick in §8).
- **When you add a subsystem, add its teardown in the same PR.** Construction and destruction are one change, not two.

**Before committing anything nontrivial, mentally trace: who owns this object, and when/where does it die?** If you can't answer, it's probably a leak.

---

## 7. Correctness landmines (hard-won — do not relearn these)

Each of these cost real debugging hours and is now load-bearing. Changing the surrounding code without understanding them reintroduces the bug.

### Settings / persistence

- **NEVER use a QSettings group named `general`/`General` (any case).** It collides with INI's magic `General` section: writes serialize as `[%General]`, a *fresh* process parses that back as group `"General"`, and QSettings reads are case-sensitive so the read misses and returns defaults **every launch**. This was the root cause of "General-tab settings reset on every restart." General-tab settings are **top-level bare keys** (plain `[General]` section) for exactly this reason. Qt's own docs warn: "Do not use a group called 'General'."
- **Verify persistence from a FRESH process, never the writing process.** QSettings reads served from the in-process `QConfFile` cache will *lie* — a round-trip can "work" in-process while being broken on disk. Launch a second process and dump `allKeys()` to check.
- **QSettings only flushes on `sync()`/destructor.** Abnormal exit loses everything since launch. That's why there's a debounce-sync (~800 ms after writes) + `aboutToQuit` + the self-pipe signal handlers. Don't remove them.
- **Check `QSettings::status()`.** A corrupt/unwritable config (classic trigger: `~/.config/unisic` owned by root after one `sudo`-launched run) silently returns *all defaults*. Surface it, don't paper over it.
- **One config file:** `~/.config/unisic/unisic.conf` (lowercase) via `UnisicConfig::filePath()` in `src/ConfigPath.h`, shared by `Settings` and `ThemeController`. Don't reintroduce a second path or a QSettings-org-derived path.
- **Settings export/import serializes Q_PROPERTYs via the metaobject**, not raw QSettings keys (raw keys omit defaults). When you add a setting, add it as a `Q_PROPERTY` so it flows through export/import automatically.
- **Native file dialogs = C++ `QFileDialog`** (Q_INVOKABLE on `AppContext`), NOT the QML `QtQuick.Dialogs` `FileDialog` — the QML one renders as the ugly Basic-styled fallback under the forced Basic style.

### Hotkeys (KGlobalAccel / D-Bus)

- **`GlobalHotkeys::setShortcut` on a user edit must use flag `0x2|0x4` (SetPresent|NoAutoloading), not `0x2` alone.** With autoloading left on, the daemon *ignores* new keys for an action that already has a stored binding and returns the OLD keys. Startup `defineAction` uses `IsDefault (0x8)` only — it sets the default column and does NOT clobber a user's active binding on restart. (Adding `SetPresent (0x2)` at startup *clears* the binding on some daemons.)
- **`setShortcut` returns the keys ACTUALLY in effect** — compare reply vs requested to detect a silent conflict (another owner holds the combo). But don't report "conflict" when the daemon simply isn't present (non-KDE).
- **KGlobalAccel is gated on `XDG_CURRENT_DESKTOP` containing KDE.** D-Bus-activating `kglobalacceld` on GNOME/sway "works" but never fires — fake availability. Non-KDE uses `PortalGlobalShortcuts`; the bind *response* is the truth, not interface presence.

### Capture (per-compositor)

- **KWin `ScreenShot2` requires the installed `.desktop` file to declare `X-KDE-DBUS-Restricted-Interfaces` AND for `/proc/<pid>/exe` to match its quoted `Exec`.** A stale/unquoted Exec path silently breaks authorization. `main.cpp::ensureDesktopFile()` handles this — including a `kbuildsycoca6` rebuild so it works in the same session — and deliberately **skips it for AppImage runs** (transient FUSE mount path goes stale every run). Don't "simplify" this away.
- **niri: any multi-monitor setup deterministically fails both the GNOME-direct and portal Screenshot paths** (niri's `ensure!(outputs.len() == 1)`), and niri implements no window/area/interactive screenshot over D-Bus. The working path is `grim` (`wlr-screencopy`). `CaptureManager::workspaceFallback` encodes the per-desktop chain — don't flatten it.
- **`allowInteractive` must stay `true` for single-capture** (fresh-install safety net) **and `false` for the overlay freeze.**

### Overlay / editor (Qt Quick)

- **`AnnotationCanvas` composites in image-pixel space with DPR forced to 1.0.** Export is exactly what's rendered. Don't reintroduce DPR scaling into the compositing math.
- **`QImage::convertToFormat(Format_Alpha8)` from `Format_Grayscale8` yields ALL-OPAQUE** (it routes through RGB). Build Alpha8 by per-scanline `memcpy` of the gray bytes — see `grayToAlpha` in `AnnotationCanvas.cpp`. Using `convertToFormat` silently no-ops mask cutouts and their previews.
- **Children of a `Flickable` live in the moving `contentItem`** — measuring pointer displacement there feeds the scroll back into itself (runaway/decay). Capture positions in SCENE coords (`mapToItem(null, ...)`) at event time. See `qml/components/MiddleScroll.qml`.
- **Qt Quick double-click fires: press, release, press, dblclick** — the second press lands synchronously *before* `mouseDoubleClickEvent`, before any queued watcher can run. Guard press-handler side effects that might be the first half of a confirm gesture.
- **`QQuickPaintedItem` texture = item size × DPR;** past ~8–16k device px it exceeds GPU texture limits (blank canvas). Zoom is capped by item dimension (~6000 logical px/side, DPR-aware, re-clamped on `imageChanged` because undo-crop can grow the image).

### Process / single-instance

- **Single-instance socket is keyed on UID alone** (`org.unisic.Unisic.<uid>`), deliberately not on any session/display env var — those disagree across autostart vs click vs keybind-spawn and would split into duplicate instances (double hotkey dispatch, racing QSettings writers). A CLI capture flag forwards to the running instance and must trigger the action there, not just raise the window.
- **A running release AppImage owns the socket** (`/tmp/org.unisic.Unisic.<uid>`), so a dev `./build/unisic` will *forward and exit in ~60 ms* — your code never runs. See §11.

### QML singleton trap

- **Do NOT `qmlRegisterSingletonInstance` `ThemeController` into the `Unisic` URI.** It clobbers the module's other `QML_ELEMENT` C++ types (e.g. `AnnotationCanvas` becomes "not a type"). `ThemeController` is a module QML singleton; `IconImageProvider` shares the one engine-created instance via `ThemeController::instance()`.

---

## 8. UI / QML conventions

- **All colors come from `qml/Theme.qml` tokens.** Never hardcode a hex in a component. The mandatory palette (§1) is enforced through Theme. `Theme.qml` property names are consumed by ~19 files — **keep names stable**; renaming a token is a breaking change across the UI.
- **`ToolCatalog.qml` is the single source of the annotation tool set.** Add/remove/reorder tools there; both toolbars build from `visibleFor(ctx, hiddenTools)`. Don't hardcode tool lists in a page.
- **Icons are freedesktop `iconName`s via `UIcon`**, not emoji, not inline SVG in components. `IconImageProvider` serves `image://icon/<name>?color=%23RRGGBB&sz=NN&v=<rev>`. `UIcon` must stay `asynchronous: false` — the provider runs on the GUI thread and `QIcon::fromTheme`/`qApp->palette()` are not thread-safe.
- **Reuse the `components/` primitives** (`UButton`, `USwitch`, `UComboBox`, …) — don't reinvent a styled control inline. `UComboBox`'s popup is parented to `Overlay.overlay` so dropdowns escape `Flickable` clipping; match that pattern for any new popup.
- **No Kirigami, no Breeze QML, no Qt Quick Controls default styling.** Basic style is forced globally.
- **Theme awareness:** the "system" theme bridges the live palette via `ThemeController`. Don't hardcode light/dark assumptions.

---

## 9. Style, conventions, and scope

- **Match the surrounding code.** Comment density, naming, and idiom in this repo lean toward *explaining the non-obvious "why"* — the D-Bus flag, the Wayland quirk, the case-collision. Terse where obvious, a paragraph where a future reader would otherwise reintroduce a bug. Mirror that.
- **C++20, Qt idioms:** `QStringLiteral`/`QLatin1String` for literals in hot paths, signal/slot over polling, RAII for resources, `const` correctness. Follow the existing files.
- **Keep the diff scoped.** Fix the thing asked; don't opportunistically reformat, rename, or "modernize" unrelated code — it obscures the real change and risks the landmines above. A separate cleanup PR is fine.
- **Don't grow `AppContext`.** It's already the largest file and the central facade. New behavior usually belongs in a focused subsystem class that `AppContext` wires up, not another 200 lines in `finishCapture`.
- **After-capture actions fire independently and immediately** in `AppContext::finishCapture` — copy/save/upload/editor each run on their own; the editor never blocks the others. Preserve that independence.
- **Version string is single-sourced** from `project(... VERSION x.y.z)` in `CMakeLists.txt` via `UNISIC_VERSION`. Don't hardcode a version elsewhere.

---

## 10. Verifying a change (do NOT skip this)

This app is GUI + Wayland + D-Bus + external processes. Unit tests barely touch the load-bearing parts (there is no test suite yet). **The real verification is exercising the affected flow on a live Wayland session and observing behavior** — not "it compiles."

Before you claim a change works:

1. **Build clean:** `cmake --build build` with no new warnings.
2. **Kill every running instance first — including AppImages.** A running release AppImage (process `AppRun` / `Unisic-*.AppImage`, *not* `unisic`) owns the single-instance socket; your dev build will forward to it and exit in ~60 ms, so your fix never runs and it looks "still broken." Detect: `ps aux | grep -iE 'AppRun|AppImage'` and `ss -xlp | grep unisic`. `pkill -x unisic` does NOT kill an AppImage — use `pkill -f AppImage`. Also remove a stale socket after a crash.
3. **Exercise the actual flow**, e.g.:
   - Capture change → run each of `unisic --fullscreen`, `--region`, `--window`, `--gif` on the target compositor.
   - Settings change → change it in the UI, fully quit, relaunch a **fresh process**, confirm it persisted (fresh process — see §7).
   - Hotkey change → set it in-app, confirm it actually fires (KDE) or falls back cleanly (non-KDE).
   - Recording change → record, confirm the output plays and no `ffmpeg` process is left behind.
4. **Watch for leaks and stragglers:** no orphaned `ffmpeg`/`curl`/`grim`, no runaway CPU at idle, no growing RSS after repeated captures.
5. **If you touched capture/hotkeys and can only test one compositor, say so explicitly** in the PR — the fallback chains differ per desktop and an untested branch is a likely regression.

Never report "done" for an untested runtime change. If you couldn't run it, state that plainly and describe what still needs verification.

---

## 11. Commits and PRs

- **Conventional Commits:** `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`, etc. Subject in imperative mood, ≤ ~72 chars. See `git log` for the house style (e.g. `fix: migrate legacy settings from "general" group to top-level keys...`).
- **Explain the "why," especially for a landmine fix.** A one-line "fix settings reset" is useless to the next person; name the mechanism (the `[%General]` case-collision) so the fix isn't undone.
- **Branch off `main`; don't commit or push unless the human asks.** Never force-push shared branches.
- **One logical change per PR.** Keep it reviewable.
- **PR description checklist** (see §12) — state what you tested and on which compositor.
- The GitHub release pipeline is `.github/workflows/release.yml`; packaging metadata lives in `CMakeLists.txt` (CPack) and `packaging/arch/PKGBUILD`. If a change affects runtime deps or installed files, update all relevant ones.

---

## 12. Definition of done — checklist

Before opening a PR, confirm:

- [ ] Builds clean (`Release`, Ninja), no new compiler/QML warnings.
- [ ] **No new dependency** (Qt module, `.so`, bundled lib) without justification + sign-off; optional heavy deps are compile-time guarded.
- [ ] **No leak:** every new `QObject` has an owner; temp files/processes/threads/D-Bus handles are torn down on all exit paths (incl. signals).
- [ ] **No landmine reintroduced** (§7): no `general` QSettings group; DPR-1.0 compositing intact; `setShortcut` flags correct; single-instance/socket semantics intact; no `ThemeController` register into `Unisic` URI.
- [ ] Persistence changes verified from a **fresh process**.
- [ ] Colors from `Theme.qml`; tools from `ToolCatalog.qml`; icons via `UIcon`.
- [ ] **Exercised the real flow on a live Wayland session**; compositors tested are named in the PR. No orphaned helper processes, no idle CPU/RAM growth.
- [ ] Diff is scoped — no drive-by reformatting or unrelated renames.
- [ ] Startup / CLI-dispatch / batch-mode paths unchanged in cost (no new blocking work).
- [ ] Feature actually belongs in Unisic (§1) — not creep.

---

## 13. Quick "do NOT" list

- ❌ Add X11 capture paths, screen-scraping, or security-model bypasses.
- ❌ Introduce a QSettings group named `general`/`General`.
- ❌ Verify persistence by reading from the writing process.
- ❌ `qmlRegisterSingletonInstance(ThemeController)` into the `Unisic` URI.
- ❌ Use `convertToFormat(Format_Alpha8)` on grayscale mask data.
- ❌ Measure Flickable-child pointer motion in content coords.
- ❌ Add Kirigami / Breeze / KDE Frameworks / Boost / any heavy framework.
- ❌ Hardcode colors, tool lists, version strings, or config paths.
- ❌ Block the startup / CLI-forward / batch-mode paths.
- ❌ `new` a QObject with no owner, or `delete` one with pending events.
- ❌ Leave `ffmpeg`/`curl`/`grim` children or worker threads running after teardown.
- ❌ Assume KWin, `kglobalacceld`, Breeze, `grim`, or `curl` is present — detect and degrade.
- ❌ Report "done" on a runtime change you didn't actually run.
- ❌ Add features the screenshot/record/share workflow doesn't need.

---

*When this file and the code disagree, the code is authoritative — but fix the drift: update this file in the same PR.*

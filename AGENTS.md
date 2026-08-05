# AGENTS.md

Contributor guide for **any** AI coding agent working on Unisic - Cursor, Aider, Zed, Codex, Continue, Cline, Windsurf, GitHub Copilot Agent, Claude Code, and humans reading over their shoulder. This is the canonical, tool-agnostic contract.

**Read `CLAUDE.md` beside it, whatever tool you use.** It is not Claude-only trivia: it is the short list of non-negotiables plus an **index into `docs/dev/`**, where the per-subsystem deep notes live (`architecture.md` for `src/**`, `ui-kit.md` for `qml/**` and the kit, `pipelines.md` for recording/hotkeys/clipboard/CLI/developer mode, `packaging.md` for `install.sh` and releases, `i18n.md` for strings). Those notes are hard-won fixes, not background reading - open the matching file **before** changing that area, or you will re-break something already solved. This file and `CLAUDE.md` agree; where they ever disagree, **this file and the actual code win** - and fix the drift in the same PR.

> **Read this whole file before your first edit.** Unisic is small on purpose. Most of the hard problems here are *invisible* - Wayland capture authorization, D-Bus signal ownership, QSettings persistence quirks, Qt object lifetimes. A change that "looks obviously correct" has repeatedly been the wrong one. The gotchas below were each paid for in hours of debugging; treat them as landmines, not trivia.

---

## 1. What Unisic is (and is not)

Unisic is a **screenshot + screen-recording tool for Linux Wayland**, prioritizing KDE Plasma/KWin but portable via `xdg-desktop-portal`, and running on an **X11 session** as a best-effort second target since 0.8 (see below). Stack: **C++20, Qt 6 (6.5+), Qt Quick / QML**, fully custom UI. GPLv3. Zero telemetry.

Core workflow it owns end-to-end: press hotkey → annotate *on the selection overlay before the shot is taken* → post-capture editor (arrows, shapes, text, blur/pixelate, crop, numbered steps, smart eraser) → route the result to clipboard / disk / a custom upload destination with the link auto-copied → or record the same region as GIF/MP4/WebM.

**It is NOT:** a general image editor, a cloud service, a cross-platform app, an X11-first tool, or a kitchen-sink utility. Every feature request is measured against "does a screenshot/record/share workflow genuinely need this?" The answer is usually no.

### Non-negotiable product constraints

- **Wayland-legit capture paths ONLY on a Wayland session.** `xdg-desktop-portal` Screenshot/ScreenCast, KWin `org.kde.KWin.ScreenShot2` D-Bus (KDE enhancement), `wlr-screencopy` via `grim` (wlroots), `org.gnome.Shell.Screenshot` (niri/GNOME direct), PipeWire for video, KGlobalAccel / portal GlobalShortcuts for hotkeys. **No X11 path on a Wayland session. No screen-scraping. No compositor-specific hacks that bypass the security model.**
- **X11 sessions ARE supported, as a deliberate second target (since 0.8) - not as a hack.** Everything session-agnostic (screenshots through the portal, overlay, editor, OCR, history, upload) always worked there; two paths were added for `xcb` sessions only:
  - **Recording** uses `X11ShmGrabber` (XShm + XFixes, in the kit) instead of the ScreenCast portal, so it also works on desktops with no portal backend (Cinnamon, MATE, Xfce on Xorg). Cursor halo, click ripples and the keystroke badge come along; the record-region frame and the styled notification card are XWayland/X11 override-redirect windows (the same helper the GNOME path uses).
  - **Global hotkeys** use `X11Hotkeys` (`XGrabKey`) where KGlobalAccel is absent; the backend id is `"x11"`.
  - **Recording a single WINDOW stays Wayland-only** - the window picker is the portal's. `capRecordWindow()` disables the Window source, and the UI says why. Do not fake a picker with X11 window enumeration.
  - Both are compile-time gated in the kit (`HAVE_X11`, `HAVE_X11_HOTKEYS` - see §3) and runtime-gated on `QGuiApplication::platformName() == "xcb"`. **Additive only:** an X11 branch must never change what a Wayland session does. `CaptureManager::isX11Session()` exists for the reverse case (a stale `WAYLAND_DISPLAY` must not route an X11 session's capture into a dead or foreign compositor).
  - **X11 is best effort.** Development and daily use are Wayland; X11 was verified by one pass over the features, on one desktop. Say so when you touch it, and don't trade a Wayland behavior for an X11 one.
- **Mandatory UI palette** - do not introduce off-palette colors:
  - Primary `#17153B` (window/panel backgrounds)
  - Secondary `#2E236C`, Tertiary `#433D8B` (secondary elements, hover/active)
  - Accent `#C8ACD6` (action buttons, attention)
  - All UI colors flow from the `Theme.qml` singleton's tokens (in the kit, see §4) - never hardcode a hex in a component.
- **Works without KDE.** KDE gets the fully silent native path; everything must degrade gracefully to portals on GNOME/wlroots/niri. Never assume KWin, `kglobalacceld`, or Breeze is present.
- **Zero telemetry, no network calls except user-configured uploads.** No analytics, no auto-update phone-home, no crash reporters that transmit.

---

## 2. Prime directives

These are the reasons this file exists. Every change is judged against them, in order:

1. **Lightweight.** Small binary, small dependency set, fast startup, low idle RAM/CPU. Unisic lives in the tray all day; it must be invisible when not in use.
2. **Correct.** No regressions in capture/hotkey/settings persistence - the load-bearing, hard-to-test subsystems. When in doubt, verify on a real Wayland session (see §11).
3. **No leaks.** Qt makes ownership easy to get wrong. Every `new` needs an owner; every temp file, D-Bus handle, and PipeWire/ffmpeg resource needs a teardown path.
4. **No feature creep.** Adding code is a cost. The best PR is often a smaller diff, or a deletion.

If a change trades any of these away, it needs an explicit, written justification in the PR - not a silent assumption.

---

## 3. Build, run, and dependencies

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/unisic
```

- **Toolchain:** CMake ≥ 3.21, a C++20 compiler, Ninja.
- **Required:** `qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel` (+ `Widgets DBus Network Concurrent QuickControls2` Qt modules). Runtime: `ffmpeg`, `wl-clipboard`, `xdg-desktop-portal` (+ a portal backend), `qt6-qtwayland`.
- **Optional, compile-time guarded** - the build *succeeds without them* and prints a warning; the feature is disabled at runtime:
  - `pipewire-devel` → `HAVE_PIPEWIRE` → GIF/screen recording. Without it, recording is off.
  - `tesseract-devel leptonica-devel` + a langpack (e.g. `tesseract-langpack-eng`) → `HAVE_TESSERACT` → OCR ("copy text from capture"). Gates `App.ocrAvailable` in QML.
  - `zxing-cpp-devel` → `HAVE_ZXING` → QR/barcode payload instead of OCR pixels. Nested inside the Tesseract gate: no OCR, no decoding.
  - `layer-shell-qt-devel wayland-devel` → `HAVE_LAYERSHELL` → the styled capture card. On Plasma this is the ONLY route to it (the XWayland helper is refused while `org.kde.KWin` is on the bus), so a build without it always falls back to the native notification.
  - `libinput-devel systemd-devel` → `HAVE_LIBINPUT` → click capture AND the pressed-key badge. Needs BOTH pkg-config modules (`libinput`, `libudev`); on Fedora `libudev.pc` ships in `systemd-devel`. Without it `InputPermission::probe()` returns `NotBuilt` and both Settings rows are dead. No recipe carried it until 0.8.3, so every package before that shipped the feature compiled out - see `docs/dev/packaging.md` for the full list of files to change together.
  - `kf6-kguiaddons-devel` → `HAVE_KGUIADDONS` → Klipper clipboard history.
  - `qt6-qtwayland-devel qt6-qtbase-private-devel plasma-wayland-protocols-devel` (all three) → `HAVE_KWIN_SCREENCAST` in unisic-kit → KWin-native recording with no portal share dialog.
  - `libX11-devel libXext-devel libXfixes-devel` (pkg-config `x11 xext xfixes`) → `HAVE_X11` in unisic-kit → `X11ShmGrabber`, the frame source on an X11 session. **The app-side use is `#if defined(HAVE_PIPEWIRE) && defined(HAVE_X11)`** - the sampler/encoder it feeds is itself compiled under `HAVE_PIPEWIRE`, so a PipeWire-less build has no X11 recording either.
  - `libX11-devel libxcb-devel` (pkg-config `x11 xcb`) → `HAVE_X11_HOTKEYS` in unisic-kit → `X11Hotkeys` (`XGrabKey` + the xcb `KeyPress` native event filter). A **separate** gate from `HAVE_X11` on purpose: a build can end up with X11 hotkeys and no X11 recording, or the reverse. Without it, non-KDE X11 falls back to the portal / compositor binds.
  - Packaging carries these: `libx11 libxext libxfixes libxcb` are in `packaging/arch/PKGBUILD` (both `depends` and `makedepends`) and the equivalent lists elsewhere. A missing X11 lib there ships a binary that will not start - that is what stalled the 0.8 AUR publish.
- **After installing an optional dep into an EXISTING build tree, delete `build/`.** Re-running `cmake -B build` adds the new sources to the target but the AUTOMOC custom command does not depend on `AutogenInfo.json`, so ninja never re-runs it: `mocs_compilation.cpp` keeps the old list and the link dies with `undefined reference to vtable for <NewClass>` / `staticMetaObject` / its signals. Deleting just the stale `<build>/<target>_autogen/timestamp` (e.g. `build/external/unisic-kit/unisic-kit_autogen/timestamp`) forces AUTOMOC to re-run and is enough if a full rebuild is too expensive. A clean configure has never had the problem.
- **Runtime helpers shelled out, not linked:** `ffmpeg` (GIF/video encode), `curl` (FTP/SFTP uploads), `grim` (wlroots/niri capture), `wl-copy` (clipboard mirror), `kbuildsycoca6` (KDE service-cache rebuild). Treat all as optional-at-runtime: detect with `QStandardPaths::findExecutable`, degrade gracefully, never crash if absent.

**Dependency policy (this is a lightweight app):**
- **Do not add a new library** - Qt module, system `.so`, or bundled source - without a strong justification and maintainer sign-off. Prefer shelling out to an already-required helper, or a small self-contained implementation, over a new link-time dependency.
- **Do not pull in Kirigami, Breeze, KDE Frameworks, Boost, or any heavy framework.** The UI is deliberately hand-built on Qt Quick Basic style. `QQuickStyle::setStyle("Basic")` is set in `main.cpp` for exactly this reason.
- New optional features that need a heavy dep must follow the `HAVE_PIPEWIRE`/`HAVE_TESSERACT` compile-time-guard pattern so the default build stays lean.
- Keep `CPACK_STRIP_FILES` working; don't add anything that bloats the shipped binary or the Debian/RPM/Arch/AppImage runtime dep lists in `CMakeLists.txt` / `packaging/` without updating them.

**End-user install path (`scripts/install.sh`).** Separate from building from source: a self-contained bash installer aimed at Linux newcomers, **TUI-only (no CLI)** by design. Run it (`bash <(curl -fsSL …/scripts/install.sh)`, or `curl … | bash` - the menu reads `/dev/tty` so a pipe still works) and it opens a **btop-style bordered arrow-key menu** in the terminal's **alternate-screen buffer**: the whole run - menu AND the install's own output - happens in that window, and leaving it restores the terminal to the command line, so only a final thank-you remains. It is one morphing menu whose main screen offers **Install or update / Settings / Remove / Quit** under a green install-status line (`installed_status()` - installed version + auto-update state, or "not installed yet"); each of the first three opens a submenu - Install (recommended install, portable install, "install a specific version" → version picker), Settings (auto-updates timer toggle + pre-release toggle, both flip in place), Remove (uninstall, uninstall+purge). Full-redrawn in place; `q`/Esc backs out one level (submenu → main, version picker → Install); `_draw` renders a rounded box; `die()`/`_cleanup` leave the alt screen before printing so errors survive. The single non-interactive entry is the private `--self-update <appimage|tarball|native|flatpak> <prefix> [pre]` - the systemd-user auto-update timer invokes the portable channels, and Unisic's in-app "Install now" (native `/usr` package installs, which can't self-update in place; `UpdateChecker::installViaScript` → `AppContext`/`UUpdatePrompt`) invokes `native` inside a terminal it spawns so the `sudo` password prompt is visible. There are **no user-facing flags**. It auto-detects the distro and installs the matching release asset - `.deb` (apt), `.fedora.x86_64.rpm` (**Fedora only** - QML links Qt PRIVATE symbols so the rpm is Qt-minor-locked; openSUSE gets the OBS zypper repo, never the rpm), `.pkg.tar.zst` (pacman) - while **atomic/immutable** desktops (Silverblue/Bazzite/…, `/run/ostree-booted`) and no-native-package distros get the self-updating AppImage or portable tarball in `~/.local` (no password). A **`flatpak` channel** sits beside them (`install_flatpak`, menu entry shown only when the `flatpak` command exists): the sandboxed app can never update itself, so this script does it from the host. `flathub_has_app()` probes `flathub.org/api/v2/appstream/app.unisic.Unisic` - once the listing is live that remote is the channel (`flatpak update`, and a bundle install is moved over with `--reinstall`), before that it installs the `.flatpak` release asset by bundle. `flatpak_scope()` decides `--user` vs `--system` (a system one goes through `priv`, and `setup_autoupdate` refuses the timer for it - the password prompt has nowhere to go). `auto` never installs a Flatpak on its own, but it does UPDATE an existing one instead of dropping a second copy of Unisic next to it, unless a native package is installed. Native packages self-register their OBS/COPR update repo, so re-running == updating; the menu also uninstalls (± delete settings), installs older versions, and toggles the auto-update timer + pre-releases. It detects the session and warns X11 users after install that **screen recording is Wayland-only** (screenshots and everything else work; recording is the PipeWire ScreenCast portal in `src/record`, no X11 path). Keep it in sync when release-asset names, update repos, or supported distros/channels change. Test with `bash -n scripts/install.sh` and the private `--self-update <tarball> <tmpdir>` path (a real download+unpack, no root); the menu itself is driven via a pseudo-terminal. Not covered by `ctest`.

**Flatpak / Flathub (`packaging/flatpak/`).** `app.unisic.Unisic.yml` is the manifest the Flathub submission repository holds verbatim; `build.sh` builds, installs, runs and lints it locally (`--local` swaps the git-tag source for the working tree - never submit that variant); `README.md` is the submission checklist. Runtime is `org.kde.Platform` (Qt 6, KF6, PipeWire); the manifest builds x264 + ffmpeg (the runtime's ffmpeg has no software H.264 encoder at all), leptonica + tesseract + `eng`/`pol` data, zxing-cpp, wl-clipboard and layer-shell-qt. Everything the manifest fetches must be pinned with a checksum: the build machines have **no network**. Six code paths branch on `FLATPAK_ID` (KWin fast path off, the two portal app-id registrations skipped, the screenshot permission granted to the flatpak id only, no update staging, `installKind() == "flatpak"` with the Updates pane turned into an explanation, and autostart through `org.freedesktop.portal.Background`) - each is a sandbox limitation, so removing one silently breaks the flatpak while every other channel keeps working. `.github/workflows/flatpak.yml` builds and lints it; `build.yml` calls it on every PR (`local: true`) and `release.yml` calls it (`local: true`, because the `v<version>` tag does not exist yet) to attach `unisic-<version>-x86_64.flatpak` to the release - the asset `install.sh` installs from until Flathub is live. That job is deliberately outside the release job's `needs` (slowest build in the pipeline; a broken Flatpak must not block the packages), so the bundle is uploaded afterwards by `flatpak-asset` and the release job's artifact sweep is pinned to `pattern: unisic-*`. A release bump means: tag, `<release>` entry in `resources/app.unisic.Unisic.metainfo.xml`, then `tag:` + `commit:` in the manifest.

---

## 4. Repository map

```
src/
  main.cpp              Entry point: QApplication, single-instance socket, signal handlers,
                        QQuickStyle=Basic, KWin .desktop authz setup, CLI dispatch, batch modes.
  AppContext.{h,cpp}    THE facade exposed to QML as context property `App`. Owns every
                        subsystem + the after-capture pipeline (editor/save/clipboard/upload/
                        history), tray icon, hotkey dispatch, filename templating. Largest file.
                        Its diagnostics half is diag/SmokeTests.cpp - SAME class, second .cpp.
  Settings.{h,cpp}      All persisted settings as Q_PROPERTYs. Metaobject-driven export/import.
  ConfigPath.h          UnisicConfig::filePath() - the ONE config file path.
  FilenameTemplate.h    Save-name template expansion (%date%/%time%/%i%/…) + image
                        extension mapping, header-only so tests skip AppContext.

  capture/              KWinScreenShot2 (silent KDE), PortalScreenshot, GnomeScreenshot (niri/
                        GNOME), GrimScreenshot (wlroots), PortalRequest (portal handle pattern),
                        ScreenCastSession (ScreenCast portal for recording), CaptureManager
                        (backend selection + per-desktop fallback chain), KWinWindowGeometry
                        (active-window rect via a throwaway KWin script, KDE only).
  editor/              AnnotationCanvas (the core QQuickPaintedItem drawing surface - all tools,
                        selection, undo/redo, compositing in IMAGE-PIXEL space, DPR forced 1.0;
                        used by BOTH overlay and editor), EditorSession.
  overlay/             OverlayController (freezes each screen, one fullscreen OverlayWindow per
                        monitor).
  record/              PipeWireGrabber (libpipewire thread, keeps latest SHM frame), GifRecorder
                        (samples to ffmpeg; also drives MP4/WebM), ClickCapture/KeyCapture
                        (libinput observers, HAVE_LIBINPUT), CursorOverlayPainter (halo/ripples),
                        KeystrokeOverlayPainter (screenkey-style badge; pure logic, unit-tested).
                        GifRecorder picks the frame source at runtime: PipeWire on Wayland, the
                        kit's X11ShmGrabber on an xcb session (openX11Session, no window source).
  upload/              UploadManager (.sxcu-like destinations.json; $text$/$json:$/$regex:$;
                        type:"curl" shells to curl for FTP/SFTP).
  history/             HistoryStore (capture history + thumbnails).
  hotkeys/             GlobalHotkeys (KGlobalAccel/DBus), PortalGlobalShortcuts (non-KDE), and
                       on X11 the kit's X11Hotkeys (XGrabKey). AppContext::m_hotkeyBackend
                       ("kglobalaccel" | "portal" | "x11") names which one won.
  theme/               ThemeController (module QML singleton; system-palette bridge; community
                        themes: <config>/themes/*.json, hot-reloaded), ThemeJson.h (theme-file
                        schema, header-only + unit-tested), IconImageProvider (image://icon/...
                        recolored SVGs / QIcon::fromTheme).
  notify/              CaptureNotification.
  ocr/                 OcrEngine (HAVE_TESSERACT only).
  diag/                DiagLog (500-line ring + rotated file), CrashHandler (async-signal-safe
                        only), DiagRedact (one choke point, unit-tested), SmokeTests.cpp (every
                        devTest*/`*Check()`/runSmokeTest - AppContext's second translation unit)
                        + SmokeSupport.h (the helpers both halves share).

qml/                   APP-SPECIFIC QML only (module `Unisic`). The shared design system lives
                       in the kit (below); files using it carry an explicit `import Unisic.Kit`.
  ToolCatalog.qml      SINGLETON. Single source of the annotation tool set AND its letter
                        shortcuts; both toolbars build from visibleFor(ctx, hiddenTools) and
                        both keyboard hosts resolve through toolForShortcut().
  Main.qml, OverlayWindow.qml, EditorWindow.qml, NotificationPopup.qml, PreviewWindow.qml,
  TrimWindow.qml, RecordBorder.qml
  pages/               CapturePage, RecordPage (video + GIF in one page), EditPage, HistoryPage,
                        DestinationsPage, SettingsPage
  components/          App-only composites: ToolPropsBar, UWelcome, USystemCheck, UPatchNotes,
                        UUpdatePrompt, UNotifPreview.

external/unisic-kit/   GIT SUBMODULE, module `Unisic.Kit` - the shared design system + a few
                       shared C++ pieces (ThemeController, IconImageProvider, ConfigPath,
                       FfmpegUtil, X11Hotkeys, X11ShmGrabber). Edit here for anything reusable; the app must
                       not grow a private copy of a kit control.
  qml/Theme.qml        SINGLETON. 9 palettes computed from tokens. Property names are load-
                        bearing - ~19 files depend on them. Keep names stable.
  qml/UKeys.qml        SINGLETON. THE keyboard-activation rule (unmodified/claim/activate).
                        The only copy of the explanation; every key handler calls it.
  qml/components/      UButton, UIcon(Button), UCard, USwitch, USlider, UTextField, UComboBox,
                        UValueCombo, UShortcutRecorder/List, UFocusRing, UNameBridge,
                        SidebarItem, ToolChip, ColorDot, MiddleScroll, …

resources/icons/sym/   Bundled monochrome SVGs recolored by IconImageProvider.
packaging/             Arch PKGBUILD (+ Debian/RPM via CPack in CMakeLists).
.github/               release.yml + one reusable workflow per package format
                       (deb/rpm/arch/appimage/flatpak/nix), which build.yml also calls
                       so every PR test-builds all of them. Issue templates.
scripts/               install.sh - END-USER universal installer/updater (NOT the build; see §3).
                       Auto-detects distro → right release asset (deb/rpm[Fedora]/pkg.tar.zst/
                       AppImage/portable tar.gz) or OBS zypper repo; atomic desktops → AppImage.
                       TUI-ONLY (no CLI): btop-style alt-screen arrow-key menu; whole run lives in
                       the window, thank-you after. Private --self-update feeds the systemd-user
                       auto-update timer. Update/uninstall, older-version picker, pre-release toggle.
                       Warns X11 users that recording is Wayland-only. Also: build-appimage.sh, gen-changelog.sh, vm-test.sh.
```

The whole `src/` tree is ~8.9k lines. It is meant to stay comprehensible in an afternoon. If a file balloons, that is a smell - prefer extracting a focused helper over piling onto `AppContext.cpp`.

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

- **Every `QObject` gets a parent, or an explicit owner.** Parented objects die with the parent - that is the primary lifetime mechanism. A parentless `new QObject` that nobody stores is a leak.
- **Never `delete` a QObject that has pending signals/events queued to it. Use `deleteLater()`.** See the single-instance socket handling in `main.cpp` (`QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater)`) - follow that pattern.
- **Disconnect or scope lambdas that capture raw pointers.** A lambda connected to a long-lived object that captures a soon-to-die pointer is a dangling-capture crash. Give the connection a context object (the 3-arg `connect`) so it auto-disconnects when that object dies.
- **Temp files must be cleaned up on every exit path**, including signals. `main.cpp` installs SIGINT/SIGTERM/SIGHUP self-pipe handlers *specifically so destructors run* (QSettings flush, temp-file cleanup, tray teardown). If you create temp files (GIF/video intermediates, capture scratch), ensure they are removed on success, failure, AND signal-triggered quit. Prefer `QTemporaryFile`/`QTemporaryDir` with RAII scope.
- **Child processes (`ffmpeg`, `curl`, `grim`) must not outlive the app and must not leak.** Set `SOCK_CLOEXEC`-style hygiene (see the self-pipe: CLOEXEC so children don't inherit it). Kill/await child processes on teardown; don't leave zombie ffmpeg encoders after a cancelled recording.
- **PipeWire / D-Bus resources are manual.** `PipeWireGrabber` runs its own thread and holds SHM frames - join the thread and release buffers on stop. D-Bus `PortalRequest` handles follow the Request/Response pattern; close/clean the handle, don't leak the object path subscription.
- **Threads: `Qt::Concurrent` / worker threads must be joined or their futures awaited before teardown.** Don't touch GUI objects from a worker thread (see the `IconImageProvider` note in §9 - `QIcon::fromTheme`/`qApp->palette()` are not thread-safe).
- **QImage/QPixmap are cheap-copy (implicitly shared) but big when detached.** In `AnnotationCanvas` and the record path, avoid gratuitous deep copies of full-resolution frames in hot loops. Reuse buffers (see the `memcpy` mask trick in §8).
- **When you add a subsystem, add its teardown in the same PR.** Construction and destruction are one change, not two.

**Before committing anything nontrivial, mentally trace: who owns this object, and when/where does it die?** If you can't answer, it's probably a leak.

---

## 7. Correctness landmines (hard-won - do not relearn these)

Each of these cost real debugging hours and is now load-bearing. Changing the surrounding code without understanding them reintroduces the bug.

### Settings / persistence

- **NEVER use a QSettings group named `general`/`General` (any case).** It collides with INI's magic `General` section: writes serialize as `[%General]`, a *fresh* process parses that back as group `"General"`, and QSettings reads are case-sensitive so the read misses and returns defaults **every launch**. This was the root cause of "General-tab settings reset on every restart." General-tab settings are **top-level bare keys** (plain `[General]` section) for exactly this reason. Qt's own docs warn: "Do not use a group called 'General'."
- **Verify persistence from a FRESH process, never the writing process.** QSettings reads served from the in-process `QConfFile` cache will *lie* - a round-trip can "work" in-process while being broken on disk. Launch a second process and dump `allKeys()` to check.
- **QSettings only flushes on `sync()`/destructor.** Abnormal exit loses everything since launch. That's why there's a debounce-sync (~800 ms after writes) + `aboutToQuit` + the self-pipe signal handlers. Don't remove them.
- **Check `QSettings::status()`.** A corrupt/unwritable config (classic trigger: `~/.config/unisic` owned by root after one `sudo`-launched run) silently returns *all defaults*. Surface it, don't paper over it.
- **One config file:** `~/.config/unisic/unisic.conf` (lowercase) via `UnisicConfig::filePath()` in `src/ConfigPath.h`, shared by `Settings` and `ThemeController`. Don't reintroduce a second path or a QSettings-org-derived path.
- **Settings export/import serializes Q_PROPERTYs via the metaobject**, not raw QSettings keys (raw keys omit defaults). When you add a setting, add it as a `Q_PROPERTY` so it flows through export/import automatically.
- **Native file dialogs = C++ `QFileDialog`** (Q_INVOKABLE on `AppContext`), NOT the QML `QtQuick.Dialogs` `FileDialog` - the QML one renders as the ugly Basic-styled fallback under the forced Basic style.

### Hotkeys (KGlobalAccel / D-Bus)

- **`GlobalHotkeys::setShortcut` on a user edit must use flag `0x2|0x4` (SetPresent|NoAutoloading), not `0x2` alone.** With autoloading left on, the daemon *ignores* new keys for an action that already has a stored binding and returns the OLD keys. Startup `defineAction` uses `IsDefault (0x8)` only - it sets the default column and does NOT clobber a user's active binding on restart. (Adding `SetPresent (0x2)` at startup *clears* the binding on some daemons.)
- **`setShortcut` returns the keys ACTUALLY in effect** - compare reply vs requested to detect a silent conflict (another owner holds the combo). But don't report "conflict" when the daemon simply isn't present (non-KDE).
- **KGlobalAccel is gated on `XDG_CURRENT_DESKTOP` containing KDE.** D-Bus-activating `kglobalacceld` on GNOME/sway "works" but never fires - fake availability. Non-KDE uses `PortalGlobalShortcuts`; the bind *response* is the truth, not interface presence.

### Capture (per-compositor)

- **KWin `ScreenShot2` requires the installed `.desktop` file to declare `X-KDE-DBUS-Restricted-Interfaces` AND for `/proc/<pid>/exe` to match its quoted `Exec`.** A stale/unquoted Exec path silently breaks authorization. `main.cpp::ensureDesktopFile()` handles this - including a `kbuildsycoca6` rebuild so it works in the same session - and deliberately **skips it for AppImage runs** (transient FUSE mount path goes stale every run). Don't "simplify" this away.
- **niri: any multi-monitor setup deterministically fails both the GNOME-direct and portal Screenshot paths** (niri's `ensure!(outputs.len() == 1)`), and niri implements no window/area/interactive screenshot over D-Bus. The working path is `grim` (`wlr-screencopy`). `CaptureManager::workspaceFallback` encodes the per-desktop chain - don't flatten it.
- **`allowInteractive` must stay `true` for single-capture** (fresh-install safety net) **and `false` for the overlay freeze.**
- **X11: never decide "X11" from `WAYLAND_DISPLAY`/`DISPLAY` presence.** Capture routing uses `XDG_SESSION_TYPE == "x11"` (`CaptureManager::isX11Session()`) because a stale `WAYLAND_DISPLAY` survives a Wayland→X11 relogin in the systemd user environment and would send the capture to a dead or *foreign* compositor; that same guard is why `grim` is skipped there. The runtime code paths (recorder, hotkeys) key on `QGuiApplication::platformName() == "xcb"` instead - what Qt actually connected to. Two different questions, two different tests; don't unify them.

### Overlay / editor (Qt Quick)

- **`AnnotationCanvas` composites in image-pixel space with DPR forced to 1.0.** Export is exactly what's rendered. Don't reintroduce DPR scaling into the compositing math.
- **`QImage::convertToFormat(Format_Alpha8)` from `Format_Grayscale8` yields ALL-OPAQUE** (it routes through RGB). Build Alpha8 by per-scanline `memcpy` of the gray bytes - see `grayToAlpha` in `AnnotationCanvas.cpp`. Using `convertToFormat` silently no-ops mask cutouts and their previews.
- **Children of a `Flickable` live in the moving `contentItem`** - measuring pointer displacement there feeds the scroll back into itself (runaway/decay). Capture positions in SCENE coords (`mapToItem(null, ...)`) at event time. See `qml/components/MiddleScroll.qml`.
- **Qt Quick double-click fires: press, release, press, dblclick** - the second press lands synchronously *before* `mouseDoubleClickEvent`, before any queued watcher can run. Guard press-handler side effects that might be the first half of a confirm gesture.
- **`QQuickPaintedItem` texture = item size × DPR;** past ~8-16k device px it exceeds GPU texture limits (blank canvas). Zoom is capped by item dimension (~6000 logical px/side, DPR-aware, re-clamped on `imageChanged` because undo-crop can grow the image).

### Process / single-instance

- **Single-instance socket is keyed on UID alone** (`org.unisic.Unisic.<uid>`), deliberately not on any session/display env var - those disagree across autostart vs click vs keybind-spawn and would split into duplicate instances (double hotkey dispatch, racing QSettings writers). A CLI capture flag forwards to the running instance and must trigger the action there, not just raise the window.
- **A running release AppImage owns the socket** (`/tmp/org.unisic.Unisic.<uid>`), so a dev `./build/unisic` will *forward and exit in ~60 ms* - your code never runs. See §11.

### QML singleton trap

- **Do NOT `qmlRegisterSingletonInstance` `ThemeController` into the `Unisic` URI.** It clobbers the module's other `QML_ELEMENT` C++ types (e.g. `AnnotationCanvas` becomes "not a type"). `ThemeController` is a module QML singleton; `IconImageProvider` shares the one engine-created instance via `ThemeController::instance()`.

---

## 8. UI / QML conventions

- **All colors come from the `Theme.qml` singleton's tokens** (`external/unisic-kit/qml/Theme.qml`, reached with `import Unisic.Kit`). Never hardcode a hex in a component. The mandatory palette (§1) is enforced through Theme. `Theme.qml` property names are consumed by ~19 files - **keep names stable**; renaming a token is a breaking change across the UI.
- **`ToolCatalog.qml` is the single source of the annotation tool set.** Add/remove/reorder tools there; both toolbars build from `visibleFor(ctx, hiddenTools)`. Don't hardcode tool lists in a page.
- **Icons are freedesktop `iconName`s via `UIcon`**, not emoji, not inline SVG in components. `IconImageProvider` serves `image://icon/<name>?color=%23RRGGBB&sz=NN&v=<rev>`. `UIcon` must stay `asynchronous: false` - the provider runs on the GUI thread and `QIcon::fromTheme`/`qApp->palette()` are not thread-safe.
- **Reuse the kit's `components/` primitives** (`UButton`, `USwitch`, `UComboBox`, …) - don't reinvent a styled control inline, and put anything reusable you do add in the kit rather than beside it. Four kit singletons own the cross-cutting rules and each is the only copy of its rule: `UKeys` (keyboard activation and the modifier guard), `UFlyout` (flyout containment: parent a popup to its ANCHOR, set `margins: UFlyout.margin`, size it through `UFlyout.fitHeight` - it still escapes `Flickable` clipping, and it can no longer hang off the window or cover its own field), `UNameBridge` (accessible names from row captions) and `UFocusRing` (focus ring + the inset rule), plus `FocusScroll` for keyboard focus inside a Flickable. Route new controls through them instead of matching the old per-component pattern.
- **No Kirigami, no Breeze QML, no Qt Quick Controls default styling.** Basic style is forced globally.
- **Theme awareness:** the "system" theme bridges the live palette via `ThemeController`. Don't hardcode light/dark assumptions.
- **Translate every user-facing string.** Wrap it in `qsTr()` (QML) / `tr()` (C++) **and** fill it in all seven `i18n/unisic_{en,pl,es,it,fr,ru,de}.ts` files (English == source text). Workflow: add the call → `cmake --build build --target update_translations` (lupdate appends new strings as `type="unfinished"`) → fill every unfinished `<translation>` in all seven and drop the marker. A plain build bakes `.qm` into the qrc, so `unfinished`/empty entries silently fall back to English - never ship those. Keep placeholders (`%1`), tokens (`%date%`), globs, and tool names verbatim; match the existing per-language sound-cue convention. New QML files must be listed in `qt_add_qml_module` so lupdate scans them.
- **Layout rules, which are contract and not taste:**
  - **Nothing moves, grows, or appears under the pointer.** Hover feedback is colour only - no scale, no translate, no shadow lift, no control that fades in on hover. A control that does not apply right now **stays and disables**; flipping its `visible` reflows the row and slides its neighbours under the pointer mid-click. (Both were real regressions: the tool swatches scaled on hover, and the Servers list hid the active row's Use button.) The one sanctioned exception is a **drag** overlay - a drag state is not a hover response, and it must stay input-transparent.
  - **One full-width grid per page.** Every option is a bordered `USettingRow`-style card in the same visual language as Settings; don't invent a second column width or a bespoke inline control.
  - **The main pages must still fit above the fold at the default 1060×700 window.** Adding a card to a page that already fills it is a layout change, not a free addition - check it at that size before claiming it fits.
- **Bringing a file IN goes through ONE router**, `AppContext::openPath()` (image → editor, recording → trim window, anything else → a toast): the Edit page's file dialog, the main window's `DropArea` (`openDroppedUrls`/`openImageData`) and Ctrl+V (`pasteFromClipboard`) all end there, so no payload can land silently. What the file IS decides, never what the drag source or the dialog filter claimed. Anything the UI says BEFORE the drop must agree with what that router will accept: the drop overlay promising "Drop to open in the editor" for a payload `openPath()` then refuses is a bug, and where the answer genuinely cannot be known at drag time (a source that has not handed over its urls yet), the overlay must stay neutral and let the toast explain.
- **A page's MODE is a persisted `Settings` property, never page state.** Every page lives behind a `Loader` that is *destroyed* on navigation, so a mode kept in the page resets the moment the user walks away. `RecordPage.qml` hosts both video and GIF behind one Video/GIF segment (`GifPage.qml` is gone; the two still have separate `AppContext` entry points) and remembers which half is showing in `Settings::recordPageMode` (`ui/recordMode`). Its Screen/Region/Window buttons keep their places across the flip and only enable/disable - GIF has no window source - and their labels deliberately do not change, because swapping them would reflow the row. That makes the sidebar five entries (Capture, Record, Edit, History, Servers) plus the Settings gear in the bottom app card, with page shortcuts `Ctrl+1`…`Ctrl+6` (6 = Settings, same as `Ctrl+,`) - keep the sidebar, the `Loader` list and the shortcut list in sync.

### Accessibility contract (applies to every new control)

- **Tab walks the interface.** A focusable control sets `activeFocusOnTab`, draws a `UFocusRing` in the accent colour while focused, and activates on Space/Enter through the SAME `_activate()` function its `MouseArea` and `Accessible.onPressAction` call - pointer, keyboard and assistive tech must never drift apart.
- **Every control carries `Accessible.role` and `Accessible.name`, plus its state** (`checked`/`checkable`/`selected`/`editable`). A composite item names itself **once**: the container gets the assembled name and each label inside sets `Accessible.ignored: true` (ignoring the `Column` instead only promotes its children).
- **Never a blanket `Keys.onPressed` with an unconditional accept on a focusable control.** Use the dedicated handlers (`Keys.onSpacePressed`/`onReturnPressed`/`onEnterPressed`) routed through `UKeys.activate`/`UKeys.claim`, or a focused tile swallows the window's `Ctrl+1`…`Ctrl+6`/`Ctrl+V` and the editor's `Ctrl+Enter`. In a plain `Keys.onPressed`, the bare-key test is `UKeys.unmodified(event)` - one shared rule (it masks `KeypadModifier`, which a hand-written `=== Qt.NoModifier` does not), never a hand-rolled comparison.
- **Two places are deliberately OUT of the tab chain and must stay out:** the **history grid** (the `GridView` owns keyboard navigation via `currentIndex` + its own `Keys` handler; per-tile tab stops would make Tab walk a hundred captures and move a different cursor than the ring shows - AT-SPI still enumerates and Presses them), and the **capture overlay's toolbar** (`activeFocusOnTab: false` on every chip and button: there Space/Enter mean "confirm the capture" and the overlay holds an exclusive keyboard grab). The overlay keeps its own vocabulary instead - the tool letters from `ToolCatalog`, Space/Enter to confirm, Escape to cancel.

---

## 9. Style, conventions, and scope

- **Match the surrounding code.** Comment density, naming, and idiom in this repo lean toward *explaining the non-obvious "why"* - the D-Bus flag, the Wayland quirk, the case-collision. Terse where obvious, a paragraph where a future reader would otherwise reintroduce a bug. Mirror that.
- **C++20, Qt idioms:** `QStringLiteral`/`QLatin1String` for literals in hot paths, signal/slot over polling, RAII for resources, `const` correctness. Follow the existing files.
- **Keep the diff scoped.** Fix the thing asked; don't opportunistically reformat, rename, or "modernize" unrelated code - it obscures the real change and risks the landmines above. A separate cleanup PR is fine.
- **Don't grow `AppContext`.** It's already the largest file and the central facade. New behavior usually belongs in a focused subsystem class that `AppContext` wires up, not another 200 lines in `finishCapture`. Its diagnostics already live in a second translation unit, `src/diag/SmokeTests.cpp`: a new `devTest*`/`*Check()` is declared in `AppContext.h` and DEFINED there, never back in `AppContext.cpp`. A helper both files need loses its `static` and gets a declaration in `src/diag/SmokeSupport.h`.
- **After-capture actions fire independently and immediately** in `AppContext::finishCapture` - copy/save/upload/editor each run on their own; the editor never blocks the others. Preserve that independence.
- **Version string is single-sourced** from `project(... VERSION x.y.z)` in `CMakeLists.txt` via `UNISIC_VERSION`. Don't hardcode a version elsewhere.
- **The user-facing docs are part of the change, not a follow-up.** `README.md` states counts and lists - shipped languages, editor tool count, themes, formats, supported sessions, hotkey defaults - and they rot silently: nothing builds them, no test fails, so a stale line survives until a user is misled by it. Whenever a change adds or removes something the README enumerates, update the README in the SAME commit, and check `CLAUDE.md`/`AGENTS.md`/`docs/dev/**` for the same fact stated a second time. The same claims are duplicated in the sibling `unisic-website` repo (`content/docs/**`, `lib/i18n/dictionaries/**`); update them too when that repo is available, and say plainly that they still need updating when it is not. Do not wait to be asked - "the README says five languages, we ship seven" is a bug report, and the fix belongs with the change that caused it.

---

## 10. Verifying a change (do NOT skip this)

This app is GUI + Wayland + D-Bus + external processes. Unit tests (`tests/`, run via `ctest` in the build dir) cover the pure-logic parts - Settings persistence (fresh-process round-trip), filename templating, shortcut formatting, version compare, annotation canvas, history filter - but barely touch the Wayland/D-Bus capture paths. **The real verification is exercising the affected flow on a live Wayland session and observing behavior** - not "it compiles."

Before you claim a change works:

1. **Build clean:** `cmake --build build` with no new warnings.
2. **Kill every running instance first - including AppImages.** A running release AppImage (process `AppRun` / `Unisic-*.AppImage`, *not* `unisic`) owns the single-instance socket; your dev build will forward to it and exit in ~60 ms, so your fix never runs and it looks "still broken." Detect: `ps aux | grep -iE 'AppRun|AppImage'` and `ss -xlp | grep unisic`. `pkill -x unisic` does NOT kill an AppImage - use `pkill -f AppImage`. Also remove a stale socket after a crash.
3. **Exercise the actual flow**, e.g.:
   - Capture change → run each of `unisic --fullscreen`, `--region`, `--window`, `--gif` on the target compositor.
   - Settings change → change it in the UI, fully quit, relaunch a **fresh process**, confirm it persisted (fresh process - see §7).
   - Hotkey change → set it in-app, confirm it actually fires (KDE) or falls back cleanly (non-KDE).
   - Recording change → record, confirm the output plays and no `ffmpeg` process is left behind.
4. **Watch for leaks and stragglers:** no orphaned `ffmpeg`/`curl`/`grim`, no runaway CPU at idle, no growing RSS after repeated captures.
5. **If you touched capture/hotkeys and can only test one compositor, say so explicitly** in the PR - the fallback chains differ per desktop and an untested branch is a likely regression.
6. **If the change touches recording, hotkeys or the record border, name the session type too** (Wayland or X11). The X11 halves have their own dev buttons and F8 lines - `devTestX11Record` / `devTestX11Hotkeys`, both defined in `src/diag/SmokeTests.cpp` - and they report `SKIP (not an X11 session)` off X11, so a green F8 on Wayland proves nothing about them.

Never report "done" for an untested runtime change. If you couldn't run it, state that plainly and describe what still needs verification.

---

## 11. Commits and PRs

- **Conventional Commits:** `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`, etc. Subject in imperative mood, ≤ ~72 chars. See `git log` for the house style (e.g. `fix: migrate legacy settings from "general" group to top-level keys...`).
- **Explain the "why," especially for a landmine fix.** A one-line "fix settings reset" is useless to the next person; name the mechanism (the `[%General]` case-collision) so the fix isn't undone.
- **Credit the bug reporter.** When a fix closes a GitHub issue, reference the issue in the commit (`Fixes #51` / `Closes #51`) so it links, AND thank the reporter by name in the `resources/CHANGELOG.md` entry ("Thanks to <handle> for reporting this (#51)") - bilingual EN/PL like everything else there. Users who report bugs are the reason we know about them; the changelog is where they get named.
- **Branch off `main`; don't commit or push unless the human asks.** Never force-push shared branches.
- **One logical change per PR.** Keep it reviewable.
- **PR description checklist** (see §12) - state what you tested and on which compositor.
- The GitHub release pipeline is `.github/workflows/release.yml`; packaging metadata lives in `CMakeLists.txt` (CPack), `packaging/arch/PKGBUILD` (OBS + the release asset), `packaging/flatpak/` and `packaging/aur/` (`unisic` + `unisic-bin`). If a change affects runtime deps or installed files, update all relevant ones - a dependency added only to `packaging/arch/PKGBUILD` makes `packaging/aur/sync.sh` refuse to publish, which is the intended failure.

---

## 12. Definition of done - checklist

Before opening a PR, confirm:

- [ ] Builds clean (`Release`, Ninja), no new compiler/QML warnings.
- [ ] **No new dependency** (Qt module, `.so`, bundled lib) without justification + sign-off; optional heavy deps are compile-time guarded.
- [ ] **No leak:** every new `QObject` has an owner; temp files/processes/threads/D-Bus handles are torn down on all exit paths (incl. signals).
- [ ] **No landmine reintroduced** (§7): no `general` QSettings group; DPR-1.0 compositing intact; `setShortcut` flags correct; single-instance/socket semantics intact; no `ThemeController` register into `Unisic` URI.
- [ ] Persistence changes verified from a **fresh process**.
- [ ] Colors from `Theme.qml`; tools from `ToolCatalog.qml`; icons via `UIcon`; reusable controls in the kit, not beside it.
- [ ] **Nothing moves/appears under the pointer**, no control toggles `visible` where disabling it would do, and the touched pages still fit above the fold at 1060×700.
- [ ] **New controls are keyboard- and screen-reader-complete**: `activeFocusOnTab` + `UFocusRing`, Space/Enter through the same `_activate()` the pointer uses, `Accessible.role`/`name`/state, no blanket `Keys.onPressed`.
- [ ] **Exercised the real flow on a live Wayland session**; compositors tested are named in the PR. No orphaned helper processes, no idle CPU/RAM growth.
- [ ] **`resources/CHANGELOG.md` entry** (bilingual EN/PL, current beta heading) for every user-facing change, and **every README/docs count or list the change invalidates is updated in the same commit** (§9).
- [ ] Diff is scoped - no drive-by reformatting or unrelated renames.
- [ ] Startup / CLI-dispatch / batch-mode paths unchanged in cost (no new blocking work).
- [ ] Feature actually belongs in Unisic (§1) - not creep.

---

## 13. Quick "do NOT" list

- ❌ Route a **Wayland** session through X11, screen-scrape, or bypass the security model. (An X11-*session* path, gated on `xcb` and compile-guarded, is supported - §1.)
- ❌ Introduce a QSettings group named `general`/`General`.
- ❌ Verify persistence by reading from the writing process.
- ❌ `qmlRegisterSingletonInstance(ThemeController)` into the `Unisic` URI.
- ❌ Use `convertToFormat(Format_Alpha8)` on grayscale mask data.
- ❌ Measure Flickable-child pointer motion in content coords.
- ❌ Add Kirigami / Breeze / KDE Frameworks / Boost / any heavy framework.
- ❌ Hardcode colors, tool lists, version strings, or config paths.
- ❌ Move, scale, or reveal a control under the pointer, or toggle a control's `visible` when it just doesn't apply (disable it instead).
- ❌ Add a focusable control with no `Accessible.role`/`name`, no `UFocusRing`, or a blanket `Keys.onPressed` that eats the window's chords.
- ❌ Hand-roll a bare-key modifier test instead of `UKeys.unmodified(event)`.
- ❌ Keep a page's selected mode in the page (its `Loader` is destroyed on navigation) - persist it in `Settings`.
- ❌ Block the startup / CLI-forward / batch-mode paths.
- ❌ `new` a QObject with no owner, or `delete` one with pending events.
- ❌ Leave `ffmpeg`/`curl`/`grim` children or worker threads running after teardown.
- ❌ Assume KWin, `kglobalacceld`, Breeze, `grim`, or `curl` is present - detect and degrade.
- ❌ Report "done" on a runtime change you didn't actually run.
- ❌ Add features the screenshot/record/share workflow doesn't need.
- ❌ Ship a language, theme, tool, format or hotkey without updating the counts and lists in `README.md` and the website docs (§9).

---

*When this file and the code disagree, the code is authoritative - but fix the drift: update this file in the same PR.*

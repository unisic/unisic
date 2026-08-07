Name:           unisic
Version:        0.8
Release:        1%{?dist}
Summary:        Capture, annotate, record and share your screen on Linux Wayland

License:        GPL-3.0-or-later
URL:            https://github.com/unisic/unisic
# Both builders hand this spec a tarball built elsewhere: Packit runs
# packaging/packit-archive.sh, OBS runs tar_scm with submodules enabled. The
# URL is the documented origin, but do NOT feed the spec that tag archive
# directly - GitHub archives carry no submodule content, and the build needs
# external/unisic-kit. The release assets include a complete
# unisic-%%{version}.tar.gz for a manual rpmbuild.
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

# cmake()/pkgconfig() virtual provides instead of distro package names: the
# real -devel names differ between Fedora (qt6-qtbase-devel) and openSUSE
# (qt6-core-devel), but both distros auto-generate these provides - so this
# ONE spec serves COPR/Packit (Fedora) AND the OBS openSUSE targets.
#
# Targets: Fedora 43/44/rawhide (COPR/Packit) and openSUSE Tumbleweed +
# Leap 16.0 (OBS). openSUSE Leap 15.x is NOT one, and no %%if below pretends
# otherwise any more. Unisic has no optional dependencies: every compile-time
# gate is a hard BuildRequires and CMake stops at configure time naming the
# package to install, so a guard that switched a gate off for one distro would
# now only turn a loud configure error into a silently crippled package.
# 15.6 could not satisfy the set anyway - it has no KF6 at all (so
# cmake(KF6GuiAddons) never resolved there), it provides cmake(LayerShellQt)
# only from the Qt5 build, and its Qt 6.6 ships no Qt6GuiPrivate CMake config.
# Its PlasmaWaylandProtocols is 1.10.0, which does satisfy the >= 1.7 below;
# the comment that used to blame that half was wrong.
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
BuildRequires:  extra-cmake-modules
%if !0%{?suse_version}
# Fedora builds with Ninja (matches CI); openSUSE's %%cmake_build drives
# plain make, so no -G there and no ninja dependency.
BuildRequires:  ninja-build
# appstream-util for the %%check metainfo validation; Fedora-only - the
# %%check line is `|| :`-guarded and skips quietly where the tool is absent.
BuildRequires:  libappstream-glib
%endif
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6Concurrent)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  cmake(Qt6LinguistTools)
BuildRequires:  cmake(Qt6WaylandClient)
# KWin-native screencasting (HAVE_KWIN_SCREENCAST, zkde_screencast_unstable_v1
# - the Spectacle path). Without all three of these (Qt6WaylandClient above
# plus the pair below) every recording on Plasma falls back to the portal share
# dialog, which is the whole thing the feature removes - so they are hard
# BuildRequires and the build stops rather than shipping the fallback. Qt6
# GuiPrivate is a separate package (Fedora qt6-qtbase-private-devel, openSUSE
# qt6-gui-private-devel) and both distros generate the cmake() provide; the kit
# also accepts the bare private headers where a distro ships no CMake config
# for them, but on rpm targets the config is there and this is the cheap check.
BuildRequires:  cmake(Qt6GuiPrivate)
BuildRequires:  cmake(PlasmaWaylandProtocols) >= 1.7
BuildRequires:  pkgconfig(libpipewire-0.3)
BuildRequires:  pkgconfig(tesseract)
# openSUSE's tesseract link interface drags in -lcurl (libarchive chain);
# Fedora resolves it transitively - harmless there.
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(lept)
BuildRequires:  cmake(ZXing)
# HAVE_LAYERSHELL: the recording border frame and the on-screen notifications
# anchor themselves as layer-shell surfaces. Fedora ships layer-shell-qt-devel,
# Tumbleweed and Leap 16.0 layer-shell-qt6-devel; all three provide
# cmake(LayerShellQt). (Leap 15.x provided that same symbol from the Qt5 build,
# layer-shell-qt5-devel, which would poison a Qt6 link - one more reason that
# target is gone rather than guarded.)
BuildRequires:  cmake(LayerShellQt)
# KSystemClipboard: puts screenshots into KDE Plasma's Klipper clipboard
# history (needs the x-kde-force-image-copy hint QClipboard/wl-copy can't set).
BuildRequires:  cmake(KF6GuiAddons)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols)
# HAVE_LIBINPUT: the pressed-key badge and the click ripple read /dev/input
# through libinput's udev backend. CMake needs BOTH modules (libinput.pc and
# libudev.pc) and stops the configure if either is absent. It used to compile
# the feature out instead, which is how every package before 0.8.3 shipped two
# Settings switches that could not do anything, whatever the user's input
# group was.
BuildRequires:  pkgconfig(libinput)
BuildRequires:  pkgconfig(libudev)
# HAVE_X11 (XShm recording) and HAVE_X11_HOTKEYS (XGrabKey) in the kit: the
# X11 session support shipped in 0.8. Missing here, an rpm built from this
# spec records nothing and grabs no shortcut on an xcb session.
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xfixes)
BuildRequires:  pkgconfig(xcb)
BuildRequires:  desktop-file-utils

# Runtime helpers are hard Requires, not Recommends. The app does not degrade
# gracefully without them: it cannot record, cannot region-capture, cannot copy
# and cannot OCR, and a `dnf --setopt=install_weak_deps=False` or a zypper with
# solver.onlyRequires used to produce exactly that install. Same policy as the
# build gates, and the same list the CPack RPM/DEB blocks in CMakeLists.txt
# carry - a package that installs into a state where a documented feature
# cannot run is a broken package.
#
# "/usr/bin/ffmpeg" is a FILE dependency on purpose: stock Fedora ships
# ffmpeg-free, which installs the binary (all GifRecorder/TrimController need)
# but only RPM Fusion's package Provides the NAME "ffmpeg" - requiring the name
# would make this rpm uninstallable on stock Fedora, while the path is
# satisfied by ffmpeg-free, by RPM Fusion's ffmpeg and by openSUSE's alike.
Requires:       /usr/bin/ffmpeg
# Region/window screenshots (PortalScreenshot) and all ScreenCast recording
# route through xdg-desktop-portal. The BACKEND stays weak: which one is right
# depends on the desktop, a session always has one, and picking for the user
# would drag half a foreign desktop in.
Requires:       xdg-desktop-portal
Recommends:     (xdg-desktop-portal-kde or xdg-desktop-portal-gnome or xdg-desktop-portal-wlr or xdg-desktop-portal-gtk)
Requires:       wl-clipboard
# The pipewire DAEMON: the linked libpipewire soname only brings the library in
# via autodeps, and every ScreenCast stream needs the service running.
Requires:       pipewire
# curl is the only transport for the ftp/ftps/sftp upload destinations (one
# builtin destination is a curl destination); zip builds the diagnostics bundle
# that Settings offers. Neither is visible to the autodep scanner.
Requires:       curl
Requires:       zip
# Per-distro names for the same two things: the pipewire CLI tools
# (pw-record/pw-dump feed app-audio recording and node enumeration, pw-play
# plays the capture cue) and the tesseract language data. OCR is compiled into
# every build, so shipping without data would mean an empty language list;
# eng is the pinned default, pol is a shipped UI language, and osd.traineddata
# is what the script auto-detection (on by default) reads. Fedora calls the OSD
# data plain "tesseract-osd" - there is no tesseract-langpack-osd.
%if 0%{?fedora}
Requires:       pipewire-utils
Requires:       tesseract-langpack-eng
Requires:       tesseract-langpack-pol
Requires:       tesseract-osd
%else
Requires:       pipewire-tools
Requires:       tesseract-ocr-traineddata-eng
Requires:       tesseract-ocr-traineddata-pol
Requires:       tesseract-ocr-traineddata-osd
%endif
# Runtime pieces the auto-dep scanner cannot see (dlopened QML modules, the
# SVG image plugin, the wayland platform plugin). Fedora's monolithic
# qt6-qtdeclarative comes in via the linked libQt6Qml, but the svg/wayland
# plugin packages do not; openSUSE additionally splits the QML imports out.
# QtMultimedia is its own module on both: nothing links it, the trim editor's
# video preview imports it from QML, and the CPack rpm requires it the same way
# (CPACK_RPM_PACKAGE_REQUIRES in CMakeLists.txt).
%if 0%{?fedora}
Requires:       qt6-qtsvg
Requires:       qt6-qtwayland
Requires:       qt6-qtmultimedia
%endif
%if 0%{?suse_version}
# Verified on Tumbleweed (2026-07): the SVG imageformat plugin ships inside
# libQt6Svg6 (no qt6-svg-imageformat package), and libQt6Svg6 arrives via the
# linked-soname autodeps - only the dlopened QML imports and the wayland
# platform plugin need explicit names.
Requires:       qt6-declarative-imports
Requires:       qt6-wayland
Requires:       qt6-multimedia-imports
%endif

%description
Unisic covers the whole workflow after you press the hotkey: annotate on the
selection overlay before the shot is taken, keep editing afterwards (blur,
pixelate, numbered steps, crop, object cutout), record the same region as
GIF/MP4/WebM, and push the result to the clipboard, disk or a custom upload
destination. Built for Wayland on legitimate APIs (xdg-desktop-portal, KWin
ScreenShot2, PipeWire, KGlobalAccel), with a fully silent native capture path
on KDE Plasma. Zero telemetry.

%prep
%autosetup -n %{name}-%{version}

%build
# COPR/mock strips the environment, so UNISIC_BUILD_NUMBER is never set and
# the sidebar footer would say "dev" despite this being a release build. Use
# the RPM release as the build number ("build 1.fc44"); bump Release: (or let
# rpkg/tito bump it) for a new number.
export UNISIC_BUILD_NUMBER=%{release}
# BUILD_TESTING=OFF: include(CTest) defaults it ON and the unit tests need
# Qt6Test, which openSUSE ships as a separate qt6-test-devel - packages
# don't run unit tests (CI does).
%cmake %{!?suse_version:-G Ninja} -DUNISIC_DEV_BUILD=OFF -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/app.unisic.Unisic.desktop
appstream-util validate-relax --nonet \
    %{buildroot}%{_datadir}/metainfo/app.unisic.Unisic.metainfo.xml || :

%files
%license LICENSE
%doc README.md
%{_bindir}/unisic
%dir %{_datadir}/unisic
%{_datadir}/unisic/obs-signing-key.asc
# The build-gate manifest CMake writes at configure time and installs. Listed
# here or rpmbuild fails the build on an unpackaged file, which is the right
# outcome: an rpm without it is an rpm nothing can audit.
%{_datadir}/unisic/unisic-features.txt
%{_datadir}/applications/app.unisic.Unisic.desktop
%{_mandir}/man1/unisic.1*
%{_datadir}/metainfo/app.unisic.Unisic.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/unisic.svg
%{_datadir}/icons/hicolor/scalable/apps/app.unisic.Unisic.svg

%changelog
* Mon Jul 27 2026 Unisic maintainers <unisic@debondor.com> - 0.8-1
- Recording on KDE Plasma runs through KWin's native screencasting, so screen,
  region and window clips start with no screen-picker dialog.
- X11 sessions gain fast screen recording and native global hotkeys.
- Rebuilt main window: one backdrop, every page on a shared card grid, nothing
  that moves or appears under the pointer. Recording and GIF are one page.
- Drag and drop or paste an image or a recording onto the window; test an
  upload destination before saving it.
- Full keyboard and screen-reader support across the interface.
- Adds a French translation, a man page, an activity log plus crash report, a
  Flatpak build, AUR packages, and in-app updates for deb/rpm/Arch installs.

* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 0.6.4-1
- Fix GNOME capture: the silent-screenshot permission is repaired before
  every portal request (a once-denied GNOME access dialog left a sticky "no"
  that made region capture fail with code 2 forever), and grants now cover
  the systemd-scope app id.
- Fix the openSUSE Leap 15.6 build: gcc13 toolchain, Qt 6.6-safe QTP0004
  policy guard, zxing-cpp 1.x text() compatibility.
- Release page now ships per-distro rpms (.fedora / .opensuse-tumbleweed /
  .opensuse-leap15.6) that install the app directly and register the COPR
  repo for updates.
* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 0.6.3-1
- openSUSE (Tumbleweed + Leap 15.6) now ships from COPR deandark/Unisic; one
  release rpm installs on Fedora and openSUSE and self-registers the matching
  COPR repo (dnf or zypp) on first install.
- Step markers get their own size setting, decoupled from the text font size.
- GNOME: the region-recording border frame is drawn by an XWayland helper
  (mutter has no layer-shell); recording memory use drops measurably.
* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 0.6.2-1
- System theme mirrors the full KDE colorscheme: the Button role drives cards,
  dedicated tooltip base/text roles style tooltips, and the kdeglobals
  positive/negative colours plus the hover decoration feed the accent and
  hover fills - a KDE session now looks like a native KDE app.
- Fix filled-body system icons (camera-photo, monitor): they no longer flatten
  to a solid square under the SourceIn tint, falling back to the bundled
  symbolic glyph when a themed icon would not survive the flatten.
- Drop Flatpak packaging; the native OBS/COPR repositories and the AppImage
  remain the supported install paths.

* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 0.6.1-1
- Fix global hotkeys on GNOME: the GlobalShortcuts portal binds carried an
  empty app id for terminal/AppImage launches (identity is pinned per D-Bus
  connection at the first portal call, which Qt makes before app code runs);
  all shortcut traffic now runs on a private connection registered via
  Registry.Register first. Portal probe timeout raised for cold autostarts.
- Version footer shows the exact git-commit date of the built state
  (YYYYMMDD-HHMM); hotkeys-unavailable card gains GNOME guidance.
- Fill 42 missing pl/es/it/en translations (auto-update UI and others).

* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 0.6.0-1
- Fully automatic updates: daily GitHub check, AppImage self-swap with idle
  auto-restart, deb/rpm downloads register the OBS/COPR repo on install.
- New OBS channel home:unisic (Debian 13, Ubuntu 25.10/26.04, Arch,
  Tumbleweed, Leap 16.0); Updates section in Settings.

* Sat Jul 11 2026 Unisic maintainers <unisic@debondor.com> - 0.5.1-1
- Fix region recording on multi-monitor setups (per-monitor portal restore
  tokens + wrong-monitor stream detection with self-heal).
- Dedicated Copy-last-capture hotkey replaces the 2s Ctrl+C grab; Ctrl+C on
  the selection overlay confirms and copies (Spectacle-style).
- Capture-on-release option, separate recordings folder (~/Videos/Unisic),
  trash sound on history deletions.

* Sat Jul 11 2026 Unisic maintainers <unisic@debondor.com> - 0.5.0-1
- Editable shapes and shape groups, OCR text selection, richer text styling.
- Spanish and Italian translations (full catalogs, en/pl gaps filled).
- Separate sound cue when a recording/GIF finishes encoding; custom sounds.

* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.4.0-1
- Rename app ID to app.unisic.Unisic (unisic.app).

* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.3.1-1
- Add Unisic to COPR (dnf install/upgrade).

* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.3-1
- Initial COPR package.

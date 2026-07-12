Name:           unisic
Version:        0.6.2
Release:        1%{?dist}
Summary:        Capture, annotate, record and share your screen on Linux Wayland

License:        GPL-3.0-or-later
URL:            https://github.com/unisic/unisic
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

# cmake()/pkgconfig() virtual provides instead of distro package names: the
# real -devel names differ between Fedora (qt6-qtbase-devel) and openSUSE
# (qt6-core-devel), but both distros auto-generate these provides — so this
# ONE spec serves COPR/Packit (Fedora) AND the OBS openSUSE targets.
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
BuildRequires:  extra-cmake-modules
%if !0%{?suse_version}
# Fedora builds with Ninja (matches CI); openSUSE's %%cmake_build drives
# plain make, so no -G there and no ninja dependency.
BuildRequires:  ninja-build
# appstream-util for the %%check metainfo validation; Fedora-only — the
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
BuildRequires:  pkgconfig(libpipewire-0.3)
BuildRequires:  pkgconfig(tesseract)
# openSUSE's tesseract link interface drags in -lcurl (libarchive chain);
# Fedora resolves it transitively — harmless there.
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(lept)
BuildRequires:  cmake(ZXing)
# Leap 15.x ships LayerShellQt only for Qt5 (its cmake(LayerShellQt) provide
# points at layer-shell-qt5-devel, which would poison a Qt6 link) — skip it
# there; the HAVE_LAYERSHELL features compile out gracefully.
%if !0%{?suse_version} || 0%{?suse_version} >= 1600
BuildRequires:  cmake(LayerShellQt)
%endif
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  desktop-file-utils

# Runtime helpers are optional — the app degrades gracefully without them, so
# they are Recommends (not Requires) to keep install working on stock Fedora
# where the GPL ffmpeg is only in RPM Fusion (ffmpeg-free covers most codecs).
%if 0%{?fedora}
Recommends:     ffmpeg-free
# Capture-sound cue plays through one of these if present.
Recommends:     pipewire-utils
%else
Recommends:     ffmpeg
Recommends:     pipewire-tools
%endif
Recommends:     wl-clipboard
# Region/window screenshots (PortalScreenshot) and all ScreenCast recording
# route through xdg-desktop-portal; matches the CPack RPM/DEB dependency lists.
Recommends:     xdg-desktop-portal
# Runtime pieces the auto-dep scanner cannot see (dlopened QML modules, the
# SVG image plugin, the wayland platform plugin). Fedora's monolithic
# qt6-qtdeclarative comes in via the linked libQt6Qml, but the svg/wayland
# plugin packages do not; openSUSE additionally splits the QML imports out.
%if 0%{?fedora}
Requires:       qt6-qtsvg
Requires:       qt6-qtwayland
%endif
%if 0%{?suse_version}
# Verified against Tumbleweed and Leap 15.6 (2026-07): the SVG imageformat
# plugin ships inside libQt6Svg6 (no qt6-svg-imageformat package), and
# libQt6Svg6 arrives via the linked-soname autodeps — only the dlopened
# QML imports and the wayland platform plugin need explicit names.
Requires:       qt6-declarative-imports
Requires:       qt6-wayland
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
# Qt6Test, which openSUSE ships as a separate qt6-test-devel — packages
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
%{_datadir}/applications/app.unisic.Unisic.desktop
%{_datadir}/metainfo/app.unisic.Unisic.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/unisic.svg
%{_datadir}/icons/hicolor/scalable/apps/app.unisic.Unisic.svg

%changelog
* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 0.6.2-1
- System theme mirrors the full KDE colorscheme: the Button role drives cards,
  dedicated tooltip base/text roles style tooltips, and the kdeglobals
  positive/negative colours plus the hover decoration feed the accent and
  hover fills — a KDE session now looks like a native KDE app.
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

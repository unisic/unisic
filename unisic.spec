Name:           unisic
Version:        0.5.1
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
%if 0%{?suse_version}
BuildRequires:  ninja
%else
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
BuildRequires:  pkgconfig(lept)
BuildRequires:  cmake(ZXing)
BuildRequires:  cmake(LayerShellQt)
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
# NOTE: verify these split-package names against the first OBS build
# (rpm -qp --requires / build log) — see packaging/obs/README.md.
Requires:       qt6-declarative-imports
Requires:       qt6-svg-imageformat
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
%cmake -G Ninja -DUNISIC_DEV_BUILD=OFF
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
- Rename app ID to app.unisic.Unisic (unisic.app); Flathub submission prep.

* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.3.1-1
- Add Unisic to COPR (dnf install/upgrade).

* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.3-1
- Initial COPR package.

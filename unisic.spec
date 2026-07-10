Name:           unisic
Version:        0.3.1
Release:        1%{?dist}
Summary:        Capture, annotate, record and share your screen on Linux Wayland

License:        GPL-3.0-or-later
URL:            https://github.com/unisic/unisic
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qtwayland-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  pipewire-devel
BuildRequires:  tesseract-devel
BuildRequires:  leptonica-devel
BuildRequires:  zxing-cpp-devel
BuildRequires:  layer-shell-qt-devel
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

# Runtime helpers are optional — the app degrades gracefully without them, so
# they are Recommends (not Requires) to keep install working on stock Fedora
# where the GPL ffmpeg is only in RPM Fusion (ffmpeg-free covers most codecs).
Recommends:     ffmpeg-free
Recommends:     wl-clipboard
# Capture-sound cue plays through one of these if present.
Recommends:     pipewire-utils

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
%{_datadir}/applications/app.unisic.Unisic.desktop
%{_datadir}/metainfo/app.unisic.Unisic.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/unisic.svg
%{_datadir}/icons/hicolor/scalable/apps/app.unisic.Unisic.svg

%changelog
* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.3.1-1
- Add Unisic to COPR (dnf install/upgrade).

* Fri Jul 10 2026 Unisic maintainers <unisic@debondor.com> - 0.3-1
- Initial COPR package.

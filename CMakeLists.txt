cmake_minimum_required(VERSION 3.21)
project(unisic VERSION 0.8.4 LANGUAGES CXX)
# Human-facing version. project(VERSION) only takes numeric tuples, so
# pre-release suffixes live here (a letter suffix like "0.3b" -> the
# workflow marks the GitHub release pre-release); this string
# drives the binary footer, package versions and the GitHub release tag.
set(UNISIC_VERSION_STRING "0.8.4")

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Qml QuickControls2 DBus Network Concurrent)
find_package(PkgConfig REQUIRED)

# UNISIC HAS NO OPTIONAL DEPENDENCIES. Every probe below is mandatory and a
# missing one stops the configure right here, naming the package to install on
# four distros.
#
# The rule exists because the alternative was tried for four releases and it
# does not work. A gate that quietly switches itself off produces a binary
# whose interface still offers the feature, and nothing in the build, the
# packaging or CI ever says otherwise: HAVE_LIBINPUT was off in every Flatpak
# ever built, HAVE_KGUIADDONS in every rpm on the GitHub release page,
# HAVE_X11 in every Nix build, and every one of those jobs was green. Sandboxed
# formats make it worse still, since a Flatpak user cannot install the missing
# library afterwards even if they knew to. A build that cannot do what the UI
# promises is a broken build, so it must not be producible.
#
# pkg_check_modules and find_package are called WITHOUT their own REQUIRED
# keyword on purpose: their abort messages name a pkg-config module or a CMake
# package, neither of which is what anyone types into a package manager. The
# helper below names the real package instead. Keep its lists in sync with
# .github/workflows/{rpm,deb,arch}.yml and unisic.spec, which install exactly
# these.
function(unisic_require_failed HUMAN_NAME WHAT_BREAKS FEDORA DEBIAN ARCH SUSE)
    message(FATAL_ERROR
        "${HUMAN_NAME} not found.\n"
        "  Unisic has no optional features, so this is a hard stop rather than\n"
        "  a build that quietly ships without it.\n"
        "  What would be missing: ${WHAT_BREAKS}\n"
        "  Fedora:   sudo dnf install ${FEDORA}\n"
        "  Debian:   sudo apt install ${DEBIAN}\n"
        "  Arch:     sudo pacman -S ${ARCH}\n"
        "  openSUSE: sudo zypper install ${SUSE}\n")
endfunction()

# Screen and region recording. The grabber itself lives in the kit, which
# probes for the same module and fails the same way; this copy is what the
# app-side code links against.
pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
if(NOT PIPEWIRE_FOUND)
    unisic_require_failed("libpipewire-0.3"
        "screen and region recording on a Wayland session"
        "pipewire-devel" "libpipewire-0.3-dev" "libpipewire" "pipewire-devel")
endif()

# OCR: copy the text out of a captured region.
pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)
if(NOT TESSERACT_FOUND)
    unisic_require_failed("tesseract + leptonica"
        "OCR, i.e. copying text out of a capture"
        "tesseract-devel leptonica-devel" "libtesseract-dev libleptonica-dev"
        "tesseract leptonica" "tesseract-ocr-devel leptonica-devel")
endif()

# QR/barcode decoding inside the OCR path: a code in the scanned region copies
# its payload instead of OCR-ing its pixels. Probed at top level rather than
# nested inside the tesseract branch, so a missing tesseract cannot mask this
# error message with its own.
find_package(ZXing CONFIG QUIET)
if(NOT ZXing_FOUND)
    unisic_require_failed("zxing-cpp"
        "reading a QR or barcode inside a capture (its pixels would be OCR-ed instead)"
        "zxing-cpp-devel" "libzxing-dev" "zxing-cpp" "zxing-cpp-devel")
endif()

# Global pointer-button and key capture for recordings (the click ripple and
# the pressed-key badge), via libinput's udev backend. CMake needs BOTH
# pkg-config modules, and on Fedora libudev.pc ships in systemd-devel rather
# than in anything called libudev-*.
pkg_check_modules(LIBINPUT IMPORTED_TARGET libinput)
pkg_check_modules(LIBUDEV IMPORTED_TARGET libudev)
if(NOT LIBINPUT_FOUND OR NOT LIBUDEV_FOUND)
    unisic_require_failed("libinput + libudev (both)"
        "the click ripple and the pressed-key badge in recordings"
        "libinput-devel systemd-devel" "libinput-dev libudev-dev"
        "libinput systemd-libs" "libinput-devel systemd-devel")
endif()

# On-top custom capture card via wlr-layer-shell (KWin/wlroots). Needs both
# LayerShellQt (to make the window a layer surface) and wayland-client (to
# probe at runtime whether the compositor actually exposes zwlr_layer_shell_v1;
# GNOME/Mutter don't, and there we fall back to the native desktop
# notification). That fallback stays: it answers a RUNTIME question about the
# compositor, which is a different thing from the code being absent.
find_package(LayerShellQt QUIET)
if(NOT LayerShellQt_FOUND)
    unisic_require_failed("LayerShellQt"
        "the styled capture card, the overlay above fullscreen windows and the pinned preview"
        "layer-shell-qt-devel" "liblayershellqtinterface-dev"
        "layer-shell-qt" "layer-shell-qt6-devel")
endif()
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
if(NOT WAYLAND_CLIENT_FOUND)
    unisic_require_failed("wayland-client"
        "the runtime probe for zwlr_layer_shell_v1, and with it the styled capture card"
        "wayland-devel" "libwayland-dev" "wayland" "wayland-devel")
endif()

# KDE Plasma clipboard-history integration (KSystemClipboard). See the long
# note at the link step for why plain image/png is not enough.
find_package(KF6GuiAddons QUIET)
if(NOT KF6GuiAddons_FOUND)
    unisic_require_failed("KF6GuiAddons"
        "screenshots entering Plasma's Klipper clipboard history (they would paste now but vanish on the next copy)"
        "kf6-kguiaddons-devel" "libkf6guiaddons-dev" "kguiaddons" "kf6-kguiaddons-devel")
endif()

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0001 NEW)
# QTP0004 NEW: auto-generate qmldir for qml/ subdirectories (components/,
# pages/) so tooling resolves them; also silences the configure warning.
# The policy only exists since Qt 6.8 - setting it unguarded is a hard
# configure error on Leap 15.6's Qt 6.6 (the qmldir convenience is lost
# there; the build itself is unaffected).
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

# Shared foundation library (git submodule, also consumed by Unisic Studio):
# portal ScreenCast/PortalRequest, PipeWireGrabber, ThemeController +
# IconImageProvider, FfmpegUtil, ConfigPath, the Unisic.Kit QML module (Theme
# singleton + U* design system) and the symbolic icon set. The kit is the
# single source for those files - see external/unisic-kit/UPSTREAM.md.
add_subdirectory(external/unisic-kit)

qt_add_executable(unisic
    src/main.cpp
    src/AppContext.h src/AppContext.cpp
    src/PreviewController.h src/PreviewController.cpp
    src/Settings.h src/Settings.cpp
    # The one place that turns a QImage into bytes: format policy (JPEG has no
    # alpha, GIF needs ffmpeg, PNG catches both) lives there and nowhere else.
    src/ImageEncode.h src/ImageEncode.cpp
    src/capture/PortalScreenshot.h src/capture/PortalScreenshot.cpp
    src/capture/KWinScreenShot2.h src/capture/KWinScreenShot2.cpp
    src/capture/KWinWindowGeometry.h src/capture/KWinWindowGeometry.cpp
    src/capture/GnomeScreenshot.h src/capture/GnomeScreenshot.cpp
    src/capture/GrimScreenshot.h src/capture/GrimScreenshot.cpp
    src/capture/CaptureManager.h src/capture/CaptureManager.cpp
    src/record/GifRecorder.h src/record/GifRecorder.cpp
    src/record/VideoQuality.h
    src/record/CursorOverlayPainter.h src/record/CursorOverlayPainter.cpp
    src/record/CursorSmoother.h src/record/CursorSmoother.cpp
    src/record/ClickCapture.h src/record/ClickCapture.cpp
    src/record/KeyCapture.h src/record/KeyCapture.cpp
    src/record/KeystrokeOverlayPainter.h src/record/KeystrokeOverlayPainter.cpp
    src/record/InputPermission.h src/record/InputPermission.cpp
    src/record/TrimController.h src/record/TrimController.cpp
    src/record/RecordBorderHelper.h src/record/RecordBorderHelper.cpp
    src/record/RecordBorderController.h src/record/RecordBorderController.cpp
    src/editor/AnnotationCanvas.h src/editor/AnnotationCanvas.cpp
    src/editor/EditorSession.h src/editor/EditorSession.cpp
    src/editor/ImageEffects.h src/editor/ImageEffects.cpp
    src/editor/WatermarkPreview.h src/editor/WatermarkPreview.cpp
    src/overlay/OverlayController.h src/overlay/OverlayController.cpp
    src/upload/UploadManager.h src/upload/UploadManager.cpp
    src/actions/ExternalActionRunner.h src/actions/ExternalActionRunner.cpp
    src/history/HistoryStore.h src/history/HistoryStore.cpp
    src/history/HistoryFilterModel.h src/history/HistoryFilterModel.cpp
    src/hotkeys/GlobalHotkeys.h src/hotkeys/GlobalHotkeys.cpp
    src/hotkeys/PortalGlobalShortcuts.h src/hotkeys/PortalGlobalShortcuts.cpp
    src/hotkeys/ShortcutBinder.h src/hotkeys/ShortcutBinder.cpp
    src/update/UpdateChecker.h src/update/UpdateChecker.cpp
    # Diagnostic log + crash report. CrashHandler.cpp is
    # async-signal-safe-only code; see its header before touching it.
    src/diag/DiagLog.h src/diag/DiagLog.cpp
    src/diag/DiagRedact.h
    # AppContext's diagnostics half: the devTest* actions, the *Check() helpers
    # and runSmokeTest(). Same class, second translation unit.
    src/diag/SmokeSupport.h src/diag/SmokeTests.cpp
    src/diag/CrashHandler.h src/diag/CrashHandler.cpp
    src/update/VersionCompare.h
    src/notify/CaptureNotification.h src/notify/CaptureNotification.cpp
    src/notify/DesktopNotifier.h src/notify/DesktopNotifier.cpp
    src/notify/NotifCard.h
    src/notify/NotificationHelper.h src/notify/NotificationHelper.cpp
    src/notify/NotificationInhibitor.h src/notify/NotificationInhibitor.cpp
)

# qt_standard_project_setup bakes an $ORIGIN install-RPATH into the binary;
# distro packages link system Qt so it is dead weight - and openSUSE's
# rpmlint hard-fails the build on it (binary-or-shlib-defines-rpath).
# Build-tree binaries keep their build RPATH, and linuxdeploy patches the
# AppImage's own, so nothing else changes.
set_target_properties(unisic PROPERTIES INSTALL_RPATH "")

# Single source of the app version string, exported to C++ from the project() call.
# UNISIC_BUILD carries the CI build number (GITHUB_RUN_NUMBER, set in the release
# workflow) when built in CI, or "dev" for local builds. Shown in the sidebar footer.
if(DEFINED ENV{UNISIC_BUILD_NUMBER} AND NOT "$ENV{UNISIC_BUILD_NUMBER}" STREQUAL "")
    set(UNISIC_BUILD "$ENV{UNISIC_BUILD_NUMBER}")
else()
    set(UNISIC_BUILD "dev")
endif()
target_compile_definitions(unisic PRIVATE
    UNISIC_VERSION="${UNISIC_VERSION_STRING}"
    UNISIC_BUILD="${UNISIC_BUILD}")

# Exact git-commit date of the built state (YYYYMMDD-HHMM), regenerated at
# BUILD time (a configure-time value would go stale across commits). See
# cmake/BuildDate.cmake for the env fallback used by tarball (rpm/OBS) builds.
add_custom_target(unisic_build_date
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/unisic_build_date.h
        -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildDate.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/unisic_build_date.h
    COMMENT "Stamping build date")
add_dependencies(unisic unisic_build_date)
target_include_directories(unisic PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Developer build: exposes the F8 smoke-test hotkey and leaves compositor-capability
# options editable in Settings. A CI build (UNISIC_BUILD carries a run number, not
# "dev") defaults to a RELEASE build, where unsupported options are shown greyed-out
# with an explanation instead. Override explicitly with -DUNISIC_DEV_BUILD=ON/OFF.
if(UNISIC_BUILD STREQUAL "dev")
    set(_unisic_dev_default ON)
else()
    set(_unisic_dev_default OFF)
endif()
option(UNISIC_DEV_BUILD "Developer build (F8 smoke test + editable capability toggles)" ${_unisic_dev_default})
if(UNISIC_DEV_BUILD)
    target_compile_definitions(unisic PRIVATE UNISIC_DEV_BUILD)
    message(STATUS "Developer build: UNISIC_DEV_BUILD=ON (F8 smoke test enabled)")
endif()

# Click and keystroke capture. The kit carries the frame grabbers; these two
# pkg-config modules are the app's own, for ClickCapture/KeyCapture reading
# /dev/input through libinput's udev backend.
target_link_libraries(unisic PRIVATE PkgConfig::LIBINPUT PkgConfig::LIBUDEV)
target_compile_definitions(unisic PRIVATE HAVE_LIBINPUT)

# Native X11 global hotkeys (X11Hotkeys) live in unisic-kit next to the X11 frame
# grabber and arrive with HAVE_X11_HOTKEYS through its PUBLIC compile definitions.
# PipeWireGrabber is there too, under the kit's own HAVE_PIPEWIRE, likewise PUBLIC.

target_sources(unisic PRIVATE src/ocr/OcrEngine.h src/ocr/OcrEngine.cpp)
target_link_libraries(unisic PRIVATE PkgConfig::TESSERACT ZXing::ZXing)
target_compile_definitions(unisic PRIVATE HAVE_TESSERACT HAVE_ZXING)

target_sources(unisic PRIVATE src/notify/LayerShellNotifier.h src/notify/LayerShellNotifier.cpp)
target_link_libraries(unisic PRIVATE LayerShellQt::Interface PkgConfig::WAYLAND_CLIENT)
target_compile_definitions(unisic PRIVATE HAVE_LAYERSHELL)

# KDE Plasma clipboard-history (Klipper) integration. Klipper only
# copies an image into its HISTORY (the tray applet, paste-it-later) when the
# clipboard offer carries the x-kde-force-image-copy marker MIME - plain
# image/png (what QClipboard and wl-copy send) is pasteable now but never
# recorded, so the shot vanishes as soon as anything else is copied (issue #51).
# KF6::GuiAddons' KSystemClipboard sets a full QMimeData (hint included) over the
# wlr data-control protocol, which also lands without a focused window - the very
# thing the wl-copy fallback papers over. NOT KDE-only to build: the library is
# an ordinary Qt dependency and on a non-KDE desktop KSystemClipboard just
# behaves like the plain clipboard, which is why it can be required everywhere.
target_link_libraries(unisic PRIVATE KF6::GuiAddons)
target_compile_definitions(unisic PRIVATE HAVE_KGUIADDONS)

set_source_files_properties(qml/ToolCatalog.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(unisic
    URI Unisic
    VERSION 1.0
    # On case-insensitive filesystems the default "Unisic" output dir would
    # collide with the "unisic" executable name.
    OUTPUT_DIRECTORY qmlmod/Unisic
    # The Theme singleton and the generic U* design-system components live in
    # the Unisic.Kit module (external/unisic-kit) - only app-specific QML stays
    # here. Files referencing kit types carry an explicit `import Unisic.Kit`.
    QML_FILES
        qml/ToolCatalog.qml
        qml/Main.qml
        qml/OverlayWindow.qml
        qml/EditorWindow.qml
        qml/NotificationPopup.qml
        qml/RecordBorder.qml
        qml/PreviewWindow.qml
        qml/TrimWindow.qml
        qml/pages/CapturePage.qml
        qml/pages/RecordPage.qml
        qml/pages/EditPage.qml
        qml/pages/HistoryPage.qml
        qml/pages/DestinationsPage.qml
        qml/pages/SettingsPage.qml
        qml/components/USystemCheck.qml
        qml/components/UWelcome.qml
        qml/components/UNotifPreview.qml
        qml/components/UOverlayPreview.qml
        qml/components/UModeGlyph.qml
        qml/components/UPatchNotes.qml
        qml/components/UUpdatePrompt.qml
        qml/components/ToolPropsBar.qml
)

# qmltyperegistrations includes QML_ELEMENT headers by basename
target_include_directories(unisic PRIVATE src src/editor src/upload src/history src/notify src/update src/record)
# unisic-kit propagates its src/ include dir (capture/, record/, theme/,
# media/ headers), Qt6::DBus and, when found, PkgConfig::PIPEWIRE +
# HAVE_PIPEWIRE - so the app-side guards keep working unchanged.
target_link_libraries(unisic PRIVATE
    unisic-kit unisic-kitplugin
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::QuickControls2
    Qt6::DBus Qt6::Network Qt6::Concurrent
)

qt_add_resources(unisic "icons" PREFIX "/" FILES resources/icons/unisic.svg)
qt_add_resources(unisic "changelog" PREFIX "/" FILES resources/CHANGELOG.md)

# Bundled color themes: the non-core palettes ship as the SAME JSON format the
# user themes folder takes (one parser, one schema); core system/unisic/dark/
# light stay hardcoded in Theme.qml (startup fallback + the mandatory palette).
file(GLOB UNISIC_THEMES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/themes/*.json)
qt_add_resources(unisic "themes" PREFIX "/" FILES ${UNISIC_THEMES})

# Symbolic icons (resources/icons/sym) ship inside unisic-kit's qrc under the
# same :/resources/icons/sym/ path IconImageProvider looks up - nothing to add
# here. App-only icon additions belong in the kit now.

# Capture sound cues (General > Capture sound). Short bundled WAVs; extracted
# to the cache dir and played via pw-play/paplay/aplay at capture time.
file(GLOB UNISIC_SOUNDS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} resources/sounds/*.wav)
qt_add_resources(unisic "sounds" PREFIX "/" FILES ${UNISIC_SOUNDS})

# Bundled tray-icon presets: drop .svg/.png into resources/icons/tray/ and they
# show up in Settings > Appearance > System tray icon (re-run cmake after adding).
file(GLOB UNISIC_TRAY_ICONS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     resources/icons/tray/*.svg resources/icons/tray/*.png)
if(UNISIC_TRAY_ICONS)
    qt_add_resources(unisic "tray_icons" PREFIX "/" FILES ${UNISIC_TRAY_ICONS})
endif()

# Translations: the seven .ts files are compiled to .qm and embedded in the qrc
# under /i18n/, and `cmake --build build --target update_translations` refreshes
# the source strings. Needs Qt6 LinguistTools (lupdate/lrelease).
#
# This is the gate with the quietest failure of them all. Without the tools the
# build used to succeed and simply ship English, while the interface still
# offered seven languages in its picker and the README still promised them, and
# nothing anywhere printed more than a STATUS line. One SDK or distro change
# was all it took.
find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(NOT Qt6LinguistTools_FOUND)
    unisic_require_failed("Qt6 LinguistTools (lupdate/lrelease)"
        "every translation: the build would ship English only while the language picker still offers seven"
        "qt6-qttools-devel" "qt6-tools-dev qt6-l10n-tools" "qt6-tools" "qt6-linguist-devel")
endif()
set(UNISIC_TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_pl.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_es.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_it.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_fr.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_ru.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_de.ts)
qt_add_lrelease(unisic TS_FILES ${UNISIC_TS_FILES}
                QM_FILES_OUTPUT_VARIABLE UNISIC_QM_FILES)
qt_add_resources(unisic "i18n" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR}
                 FILES ${UNISIC_QM_FILES})
target_compile_definitions(unisic PRIVATE HAVE_TRANSLATIONS)
# Manual refresh target (`cmake --build build --target update_translations`):
# rescans C++ + QML and rewrites the .ts source strings. Not run per build.
# The kit's QML components carry user-facing qsTr() strings too (their
# translation contexts are the file base names, unchanged by the move to
# the submodule - the existing .ts entries keep matching).
file(GLOB_RECURSE UNISIC_LUPDATE_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml
     ${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/src/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/src/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/qml/*.qml)
# The target-less qt_add_lupdate() signature is Qt 6.7+. On the declared
# 6.5/6.6 floor the first positional arg is the mandatory target, so pass
# `unisic` explicitly there.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
    qt_add_lupdate(TS_FILES ${UNISIC_TS_FILES} SOURCES ${UNISIC_LUPDATE_SOURCES})
else()
    qt_add_lupdate(unisic TS_FILES ${UNISIC_TS_FILES} SOURCES ${UNISIC_LUPDATE_SOURCES})
endif()
message(STATUS "Translations: enabled (English, Polish, Spanish, Italian, French, Russian, German)")

# The gate manifest CI asserts on. Every feature this build was supposed to
# have, one NAME=ON line each, written where a packaging recipe can read it no
# matter which build tool drove the configure - the previous check grepped
# build.ninja, which does not exist under flatpak-builder or makepkg and could
# not tell the app's HAVE_PIPEWIRE from the kit's either.
#
# Nothing here is conditional: reaching this line means every probe above
# succeeded. The list is written out anyway so that .github/actions/assert-gates
# fails loudly on an OLD build directory, on a gate someone adds here and
# forgets to add there, and on a kit whose UNISIC_KIT_FEATURES went missing.
set(UNISIC_ALL_FEATURES
    HAVE_PIPEWIRE HAVE_TESSERACT HAVE_ZXING HAVE_LIBINPUT
    HAVE_LAYERSHELL HAVE_KGUIADDONS HAVE_TRANSLATIONS
    ${UNISIC_KIT_FEATURES})
# HAVE_PIPEWIRE is defined on both sides (the app links it, the kit builds the
# grabber), so it would otherwise appear twice.
list(REMOVE_DUPLICATES UNISIC_ALL_FEATURES)
list(LENGTH UNISIC_ALL_FEATURES UNISIC_FEATURE_COUNT)
if(UNISIC_FEATURE_COUNT LESS 10)
    message(FATAL_ERROR
        "Only ${UNISIC_FEATURE_COUNT} build features were collected, expected 10.\n"
        "  UNISIC_KIT_FEATURES is empty or short, which means external/unisic-kit\n"
        "  is out of date with this tree. Run: git submodule update --init --recursive")
endif()
set(UNISIC_FEATURES_MANIFEST "")
foreach(_unisic_feature IN LISTS UNISIC_ALL_FEATURES)
    string(APPEND UNISIC_FEATURES_MANIFEST "${_unisic_feature}=ON\n")
endforeach()
file(WRITE ${CMAKE_BINARY_DIR}/unisic-features.txt "${UNISIC_FEATURES_MANIFEST}")

install(TARGETS unisic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# OBS repo signing key, shipped so the deb postinst can reference it from the
# apt .sources file it writes (native repo updates for direct .deb downloads -
# see packaging/obs/README.md).
install(FILES packaging/obs/home_unisic.key
    DESTINATION ${CMAKE_INSTALL_DATADIR}/unisic RENAME obs-signing-key.asc)
# The gate manifest, installed rather than left in the build tree. Two reasons.
# flatpak-builder deletes each module's build directory on success, so an
# assertion that reads the build dir has nothing to read on the one channel
# where a missing feature hurts most; and shipping it means anyone holding a
# package can see exactly which features that binary was built with, without
# guessing from behaviour.
install(FILES ${CMAKE_BINARY_DIR}/unisic-features.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/unisic)
# Man page. Generated so the version in its header cannot drift from
# UNISIC_VERSION_STRING, and so the date is the build's, not whenever the file
# was last hand-edited. It is the ONLY CLI documentation Unisic has - there is
# no --help - and Debian's linters flag a binary that ships without one.
string(TIMESTAMP UNISIC_MAN_DATE "%Y-%m-%d" UTC)
configure_file(resources/unisic.1.in ${CMAKE_CURRENT_BINARY_DIR}/unisic.1 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/unisic.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

# The desktop file's Exec must be an ABSOLUTE path, or KWin refuses both
# restricted interfaces this app declares. KWin authorizes a caller by
# comparing QFileInfo(splitCommand(Exec).first()).canonicalFilePath() against
# /proc/<pid>/exe: a bare "unisic" is resolved against KWIN's working
# directory, finds nothing, and canonicalFilePath() returns an empty string
# that can never match. Every KDE app that holds one of these interfaces
# installs an absolute Exec for exactly this reason (Spectacle installs
# ${KDE_INSTALL_FULL_BINDIR}/spectacle). Dev builds already got this right by
# accident - ensureDesktopFile() writes applicationFilePath() - which is why
# the silent KWin capture path worked here and on no packaged install.
#
# UNISIC_DESKTOP_EXEC is overridable because the AppImage/tarball AppDir is
# relocatable: an absolute /usr/bin path baked into the AppDir would point at
# the host system, and the sandboxed/portable channels cannot use the KWin
# interfaces anyway (the exe lives on a transient mount or inside /app).
set(UNISIC_DESKTOP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/unisic"
    CACHE STRING "Exec= line written into the installed .desktop file")
configure_file(resources/app.unisic.Unisic.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/app.unisic.Unisic.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/app.unisic.Unisic.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/app.unisic.Unisic.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES resources/icons/unisic.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME app.unisic.Unisic.svg)
install(FILES resources/icons/unisic.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME unisic.svg)

set(CPACK_PACKAGE_NAME "unisic")
set(CPACK_PACKAGE_VENDOR "Unisic")
set(CPACK_PACKAGE_CONTACT "Unisic maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Capture, annotate, record and share your screen on Linux Wayland")
set(CPACK_PACKAGE_DESCRIPTION "Unisic covers the whole workflow after you press the hotkey: annotate on the selection overlay before the shot is taken, keep editing afterwards (blur, pixelate, numbered steps, crop, smart eraser), record the same region as GIF/MP4/WebM, and push the result to the clipboard, disk or a custom upload destination. Built for Wayland on legitimate APIs (xdg-desktop-portal, KWin ScreenShot2, PipeWire, KGlobalAccel), with a fully silent native capture path on KDE Plasma. Zero telemetry.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/unisic/unisic")
set(CPACK_PACKAGE_VERSION ${UNISIC_VERSION_STRING})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${UNISIC_VERSION_STRING}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES TRUE)

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Unisic maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Everything the binary needs at RUNTIME that dpkg-shlibdeps cannot see for
# itself: dlopened QML modules, the ffmpeg/curl/zip CLIs, the portal, and the
# tesseract language data. Same policy as the build gates - a package that
# installs into a state where a documented feature cannot run is a broken
# package, so none of this is a Recommends.
#   curl              the only transport for the ftp/ftps/sftp destinations
#   zip               the diagnostics bundle in Settings shells out to it
#   qml6-module-qtmultimedia  the trim editor's video preview
#   tesseract-ocr-*   OCR is compiled into every build and had no language data
#                     on any of them; osd is what the script auto-detection
#                     (on by default) needs
# KEEP THIS LIST CHARACTER-IDENTICAL to Depends in packaging/obs/debian.control.
set(UNISIC_DEBIAN_RUNTIME_DEPENDS
    curl
    ffmpeg
    libqt6svg6
    pipewire
    qml6-module-qtmultimedia
    qml6-module-qtquick
    qml6-module-qtquick-controls
    qml6-module-qtquick-dialogs
    qml6-module-qtquick-effects
    qml6-module-qtquick-window
    qt6-wayland
    tesseract-ocr-eng
    tesseract-ocr-osd
    tesseract-ocr-pol
    wl-clipboard
    xdg-desktop-portal
    zip)
string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS ${UNISIC_DEBIAN_RUNTIME_DEPENDS})
# The portal BACKEND stays a Recommends: which one is right depends on the
# desktop, the alternation cannot be expressed as a Depends without picking for
# the user, and a session always has one.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "xdg-desktop-portal-kde | xdg-desktop-portal-gnome | xdg-desktop-portal-wlr | xdg-desktop-portal-lxqt | xdg-desktop-portal-phosh | xdg-desktop-portal-gtk | xdg-desktop-portal-xapp")
# Direct-download debs self-register the OBS apt repo (postinst) so later
# versions arrive natively via apt; postrm drops the entry on purge.
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postrm")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
# The CPack rpm ships as the FEDORA release asset only: QML type
# registration links Qt PRIVATE API symbols (Qt_6.x_PRIVATE_API), pinning
# the binary to its build root's exact Qt minor - one rpm can never span
# distros, and openSUSE is served by the OBS repos instead. The Requires
# below stay distro-neutral anyway (boolean picks per distro; costs nothing
# and keeps a local `cpack -G RPM` on openSUSE installable):
# - "/usr/bin/ffmpeg" (file dependency) instead of the package name "ffmpeg":
#   stock Fedora ships ffmpeg-free, which installs the binary (sufficient for
#   GifRecorder's CLI usage) but only RPM Fusion Provides the name "ffmpeg" -
#   a package-name Requires would make the RPM uninstallable on stock Fedora.
# - Linked libraries (Qt, pipewire libs, tesseract, leptonica, ZXing,
#   LayerShellQt) are NOT listed: rpmbuild's soname autodeps cover them with
#   distro-neutral "libFoo.so.N()(64bit)" requires, and their package names
#   differ per distro ("tesseract" on openSUSE is a first-person shooter -
#   the OCR engine is "tesseract-ocr").
# - Boolean deps pick the split-package name per distro for the dlopened
#   pieces the autodep scanner cannot see (QML imports, wayland platform
#   plugin). The SVG imageformat plugin needs no entry: Fedora's qt6-qtsvg
#   and openSUSE's libQt6Svg6 both ship it and both arrive via the soname
#   autodep (verified on Tumbleweed + Leap 15.6).
# - The tesseract language data, the curl/zip CLIs and the QtMultimedia QML
#   module are here for the same reason as in the Debian list: OCR, the
#   ftp/ftps/sftp destinations, the diagnostics bundle and the trim preview all
#   fail at runtime without them, and nothing in the autodeps can see any of it.
#   Boolean per distro again, since the langpack names differ (Fedora
#   tesseract-langpack-*, and its OSD data is plain "tesseract-osd").
set(CPACK_RPM_PACKAGE_REQUIRES "/usr/bin/ffmpeg, pipewire, wl-clipboard, xdg-desktop-portal, curl, zip, (qt6-qtdeclarative or qt6-declarative-imports), (qt6-qtwayland or qt6-wayland), (qt6-qtmultimedia or qt6-multimedia-imports), (tesseract-langpack-eng or tesseract-ocr-traineddata-eng), (tesseract-langpack-pol or tesseract-ocr-traineddata-pol), (tesseract-osd or tesseract-ocr-traineddata-osd)")
# Direct-download rpms self-register the COPR repo (%post - dnf repo on
# Fedora, zypp repo on openSUSE) so later versions arrive natively;
# %postun drops the entry on erase.
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm/copr-post.sh")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/rpm/copr-postun.sh")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(unisic_shortcut_format_test
        tests/ShortcutFormatTest.cpp
        src/hotkeys/ShortcutFormat.h
    )
    target_link_libraries(unisic_shortcut_format_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_include_directories(unisic_shortcut_format_test PRIVATE src/hotkeys)
    add_test(NAME unisic_shortcut_format_test COMMAND unisic_shortcut_format_test)

    qt_add_executable(unisic_shortcut_binder_test
        tests/ShortcutBinderTest.cpp
        external/unisic-kit/src/hotkeys/ShortcutKeyMap.h
        src/hotkeys/ShortcutBinder.h src/hotkeys/ShortcutBinder.cpp
    )
    target_link_libraries(unisic_shortcut_binder_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    # ShortcutKeyMap.h moved into the kit; ShortcutBinder includes it by kit path.
    target_include_directories(unisic_shortcut_binder_test
        PRIVATE src/hotkeys external/unisic-kit/src)
    add_test(NAME unisic_shortcut_binder_test COMMAND unisic_shortcut_binder_test)

    qt_add_executable(unisic_version_compare_test
        tests/VersionCompareTest.cpp
        src/update/VersionCompare.h
    )
    target_link_libraries(unisic_version_compare_test PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(unisic_version_compare_test PRIVATE src/update)
    add_test(NAME unisic_version_compare_test COMMAND unisic_version_compare_test)

    qt_add_executable(unisic_annotation_canvas_test
        tests/AnnotationCanvasTest.cpp
        src/editor/AnnotationCanvas.h src/editor/AnnotationCanvas.cpp
        src/editor/ImageEffects.h src/editor/ImageEffects.cpp
    )
    target_link_libraries(unisic_annotation_canvas_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Concurrent Qt6::Test)
    target_include_directories(unisic_annotation_canvas_test PRIVATE src src/editor)
    add_test(NAME unisic_annotation_canvas_test COMMAND unisic_annotation_canvas_test)
    set_tests_properties(unisic_annotation_canvas_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(unisic_history_filter_test
        tests/HistoryFilterTest.cpp
        src/history/HistoryStore.h src/history/HistoryStore.cpp
        src/history/HistoryFilterModel.h src/history/HistoryFilterModel.cpp
    )
    target_link_libraries(unisic_history_filter_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
    target_include_directories(unisic_history_filter_test PRIVATE src src/history)
    add_test(NAME unisic_history_filter_test COMMAND unisic_history_filter_test)
    # HistoryStore writes thumbnails; QStandardPaths test mode keeps them out of
    # the real data dir, but it still needs a QGuiApplication.
    set_tests_properties(unisic_history_filter_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(unisic_theme_json_test
        tests/ThemeJsonTest.cpp
        external/unisic-kit/src/theme/ThemeJson.h
    )
    # QColor lives in Qt6::Gui; no QGuiApplication or platform needed.
    # ThemeJson.h moved to the kit; shippedThemesAllParse still reads the
    # SHIPPED seed themes from THIS repo (resources/themes stays app content).
    target_link_libraries(unisic_theme_json_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_include_directories(unisic_theme_json_test PRIVATE external/unisic-kit/src)
    # shippedThemesAllParse reads resources/themes/*.json from the source tree.
    target_compile_definitions(unisic_theme_json_test
        PRIVATE UNISIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME unisic_theme_json_test COMMAND unisic_theme_json_test)

    qt_add_executable(unisic_theme_seed_test
        tests/ThemeSeedTest.cpp
        external/unisic-kit/src/theme/ThemeController.h
        external/unisic-kit/src/theme/ThemeController.cpp
    )
    # The same qrc copy the app seeds from, so the test asserts against what is
    # actually shipped rather than a list written twice.
    qt_add_resources(unisic_theme_seed_test "seedthemes" PREFIX "/" FILES ${UNISIC_THEMES})
    # Qt6::Qml for qqmlregistration.h only (ThemeController is a QML singleton in
    # the app); the test drives it as a plain QObject.
    target_link_libraries(unisic_theme_seed_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Test)
    target_include_directories(unisic_theme_seed_test PRIVATE external/unisic-kit/src)
    add_test(NAME unisic_theme_seed_test COMMAND unisic_theme_seed_test)
    # QGuiApplication (styleHints, palette) without a real display.
    set_tests_properties(unisic_theme_seed_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The kit's wheel scroller, driven through a real QQuickView: synthetic
    # wheel + click, so "a click during a scroll reaches the control it is
    # over" is a checked property and not a hope. WheelBoost.qml imports plain
    # QtQuick, so the file is loaded straight from the kit checkout.
    qt_add_executable(unisic_wheel_boost_test tests/WheelBoostTest.cpp)
    target_link_libraries(unisic_wheel_boost_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::Test)
    target_compile_definitions(unisic_wheel_boost_test PRIVATE
        UNISIC_KIT_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/qml/components")
    add_test(NAME unisic_wheel_boost_test COMMAND unisic_wheel_boost_test)
    # A window, without a display.
    set_tests_properties(unisic_wheel_boost_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(unisic_keystroke_overlay_test
        tests/KeystrokeOverlayTest.cpp
        src/record/KeystrokeOverlayPainter.h src/record/KeystrokeOverlayPainter.cpp
    )
    target_link_libraries(unisic_keystroke_overlay_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_include_directories(unisic_keystroke_overlay_test PRIVATE src)
    add_test(NAME unisic_keystroke_overlay_test COMMAND unisic_keystroke_overlay_test)
    # QPainter text needs a font database, i.e. a QGuiApplication + platform.
    set_tests_properties(unisic_keystroke_overlay_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(unisic_filename_template_test
        tests/FilenameTemplateTest.cpp
        src/FilenameTemplate.h
    )
    target_link_libraries(unisic_filename_template_test PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(unisic_filename_template_test PRIVATE src)
    add_test(NAME unisic_filename_template_test COMMAND unisic_filename_template_test)

    qt_add_executable(unisic_stream_geometry_test
        tests/StreamGeometryTest.cpp
        src/record/StreamGeometry.h
    )
    target_link_libraries(unisic_stream_geometry_test PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(unisic_stream_geometry_test PRIVATE src)
    add_test(NAME unisic_stream_geometry_test COMMAND unisic_stream_geometry_test)

    # The percent <-> CRF scale the recorder's quality setting is stored in.
    qt_add_executable(unisic_video_quality_test
        tests/VideoQualityTest.cpp
        src/record/VideoQuality.h
    )
    target_link_libraries(unisic_video_quality_test PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(unisic_video_quality_test PRIVATE src)
    add_test(NAME unisic_video_quality_test COMMAND unisic_video_quality_test)

    qt_add_executable(unisic_settings_persistence_test
        tests/SettingsPersistenceTest.cpp
        src/Settings.h
        src/ConfigPath.h
    )
    # Settings.h pulls in qqmlregistration.h (QML_ELEMENT), hence Qt6::Qml.
    # The kit include dir provides SettingMacro.h (the U_SETTING macro).
    target_link_libraries(unisic_settings_persistence_test PRIVATE
        Qt6::Core Qt6::Qml Qt6::Test)
    target_include_directories(unisic_settings_persistence_test PRIVATE src external/unisic-kit/src)
    add_test(NAME unisic_settings_persistence_test COMMAND unisic_settings_persistence_test)
    # The test re-execs itself (--verify-child) for a fresh-process read-back.
    set_tests_properties(unisic_settings_persistence_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Where a curl destination puts the file and what it makes of the answer -
    # pure string work, so it needs neither a server nor a compositor. The rest
    # of UploadManager is network transport and stays with the smoke test.
    qt_add_executable(unisic_upload_url_test
        tests/UploadUrlTest.cpp
        src/upload/UploadManager.h src/upload/UploadManager.cpp
        src/Settings.h src/Settings.cpp
        src/actions/ExternalActionRunner.h src/actions/ExternalActionRunner.cpp
    )
    target_link_libraries(unisic_upload_url_test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Test)
    target_include_directories(unisic_upload_url_test PRIVATE src external/unisic-kit/src)
    add_test(NAME unisic_upload_url_test COMMAND unisic_upload_url_test)
    set_tests_properties(unisic_upload_url_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(unisic_still_gif_test
        tests/StillGifTest.cpp
        src/FilenameTemplate.h
        external/unisic-kit/src/media/FfmpegUtil.h external/unisic-kit/src/media/FfmpegUtil.cpp
    )
    target_link_libraries(unisic_still_gif_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_include_directories(unisic_still_gif_test PRIVATE src external/unisic-kit/src)
    add_test(NAME unisic_still_gif_test COMMAND unisic_still_gif_test)
    set_tests_properties(unisic_still_gif_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The conversion policy on real bytes: which format actually comes out, and
    # which substitutions (JPEG with alpha, GIF with no ffmpeg) are allowed to
    # happen silently. No AppContext, so it needs no compositor.
    qt_add_executable(unisic_image_convert_test
        tests/ImageConvertTest.cpp
        src/ImageEncode.h src/ImageEncode.cpp
        src/FilenameTemplate.h
        external/unisic-kit/src/media/FfmpegUtil.h external/unisic-kit/src/media/FfmpegUtil.cpp
    )
    target_link_libraries(unisic_image_convert_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_include_directories(unisic_image_convert_test PRIVATE src external/unisic-kit/src)
    add_test(NAME unisic_image_convert_test COMMAND unisic_image_convert_test)
    # QPainter needs a platform plugin.
    set_tests_properties(unisic_image_convert_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(unisic_diag_redact_test
        tests/DiagRedactTest.cpp
        src/diag/DiagRedact.h
    )
    target_link_libraries(unisic_diag_redact_test PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(unisic_diag_redact_test PRIVATE src)
    add_test(NAME unisic_diag_redact_test COMMAND unisic_diag_redact_test)

endif()

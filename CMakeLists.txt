cmake_minimum_required(VERSION 3.21)
project(unisic VERSION 0.7.5 LANGUAGES CXX)
# Human-facing version. project(VERSION) only takes numeric tuples, so
# pre-release suffixes live here (a letter suffix like "0.3b" -> the
# workflow marks the GitHub release pre-release); this string
# drives the binary footer, package versions and the GitHub release tag.
set(UNISIC_VERSION_STRING "0.7.5")

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Qml QuickControls2 DBus Network Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)
# Optional: global pointer-button capture for recordings (click overlays), via
# libinput's udev backend. Needs both libinput and libudev; without them the
# build still succeeds and InputPermission reports NotBuilt / ClickCapture stays
# inert. Guarded exactly like HAVE_PIPEWIRE.
pkg_check_modules(LIBINPUT IMPORTED_TARGET libinput)
pkg_check_modules(LIBUDEV IMPORTED_TARGET libudev)
# Optional: on-top custom capture card via wlr-layer-shell (KWin/wlroots). Needs
# both LayerShellQt (to make the window a layer surface) and wayland-client (to
# probe at runtime whether the compositor actually exposes zwlr_layer_shell_v1;
# GNOME/Mutter don't, and there we fall back to the native desktop notification).
find_package(LayerShellQt QUIET)
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
# Optional: KDE Plasma clipboard-history integration (KSystemClipboard).
find_package(KF6GuiAddons QUIET)
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0001 NEW)
# QTP0004 NEW: auto-generate qmldir for qml/ subdirectories (components/,
# pages/) so tooling resolves them; also silences the configure warning.
# The policy only exists since Qt 6.8 — setting it unguarded is a hard
# configure error on Leap 15.6's Qt 6.6 (the qmldir convenience is lost
# there; the build itself is unaffected).
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

# Shared foundation library (git submodule, also consumed by Unisic Studio):
# portal ScreenCast/PortalRequest, PipeWireGrabber, ThemeController +
# IconImageProvider, FfmpegUtil, ConfigPath, the Unisic.Kit QML module (Theme
# singleton + U* design system) and the symbolic icon set. The kit is the
# single source for those files — see external/unisic-kit/UPSTREAM.md.
add_subdirectory(external/unisic-kit)

qt_add_executable(unisic
    src/main.cpp
    src/AppContext.h src/AppContext.cpp
    src/PreviewController.h src/PreviewController.cpp
    src/Settings.h src/Settings.cpp
    src/capture/PortalScreenshot.h src/capture/PortalScreenshot.cpp
    src/capture/KWinScreenShot2.h src/capture/KWinScreenShot2.cpp
    src/capture/GnomeScreenshot.h src/capture/GnomeScreenshot.cpp
    src/capture/GrimScreenshot.h src/capture/GrimScreenshot.cpp
    src/capture/CaptureManager.h src/capture/CaptureManager.cpp
    src/record/GifRecorder.h src/record/GifRecorder.cpp
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
    src/overlay/OverlayController.h src/overlay/OverlayController.cpp
    src/upload/UploadManager.h src/upload/UploadManager.cpp
    src/actions/ExternalActionRunner.h src/actions/ExternalActionRunner.cpp
    src/history/HistoryStore.h src/history/HistoryStore.cpp
    src/history/HistoryFilterModel.h src/history/HistoryFilterModel.cpp
    src/hotkeys/GlobalHotkeys.h src/hotkeys/GlobalHotkeys.cpp
    src/hotkeys/PortalGlobalShortcuts.h src/hotkeys/PortalGlobalShortcuts.cpp
    src/hotkeys/ShortcutKeyMap.h
    src/hotkeys/ShortcutBinder.h src/hotkeys/ShortcutBinder.cpp
    src/update/UpdateChecker.h src/update/UpdateChecker.cpp
    src/update/VersionCompare.h
    src/notify/CaptureNotification.h src/notify/CaptureNotification.cpp
    src/notify/DesktopNotifier.h src/notify/DesktopNotifier.cpp
    src/notify/NotifCard.h
    src/notify/NotificationHelper.h src/notify/NotificationHelper.cpp
    src/notify/NotificationInhibitor.h src/notify/NotificationInhibitor.cpp
)

# qt_standard_project_setup bakes an $ORIGIN install-RPATH into the binary;
# distro packages link system Qt so it is dead weight — and openSUSE's
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

# PipeWireGrabber now lives in the kit (built there under the same
# HAVE_PIPEWIRE gate, which the kit exports PUBLIC); this only keeps the
# runtime warning for a pipewire-less build.
if(NOT PIPEWIRE_FOUND)
    message(WARNING "libpipewire-0.3 not found — GIF/screen recording will be disabled at runtime")
endif()

# Optional click capture: link libinput/libudev and flip HAVE_LIBINPUT only when
# both were found. The capture sources always compile; this just enables the real
# code path (else InputPermission::probe() returns NotBuilt and ClickCapture is inert).
if(LIBINPUT_FOUND AND LIBUDEV_FOUND)
    target_link_libraries(unisic PRIVATE PkgConfig::LIBINPUT PkgConfig::LIBUDEV)
    target_compile_definitions(unisic PRIVATE HAVE_LIBINPUT)
    message(STATUS "Click capture: enabled (HAVE_LIBINPUT=ON)")
else()
    message(WARNING "libinput/libudev not found — click capture (ClickCapture) will be unavailable")
endif()

if(TESSERACT_FOUND)
    target_sources(unisic PRIVATE src/ocr/OcrEngine.h src/ocr/OcrEngine.cpp)
    target_link_libraries(unisic PRIVATE PkgConfig::TESSERACT)
    target_compile_definitions(unisic PRIVATE HAVE_TESSERACT)
    # QR/barcode decoding inside the OCR path (zxing-cpp, optional): a QR in
    # the scanned region copies its payload instead of OCR-ing its pixels.
    find_package(ZXing CONFIG QUIET)
    if(ZXing_FOUND)
        target_link_libraries(unisic PRIVATE ZXing::ZXing)
        target_compile_definitions(unisic PRIVATE HAVE_ZXING)
        message(STATUS "QR decoding in OCR: enabled (zxing-cpp)")
    else()
        message(STATUS "QR decoding in OCR: disabled (needs zxing-cpp-devel)")
    endif()
else()
    message(WARNING "tesseract/leptonica not found — OCR (copy text from capture) will be disabled")
endif()

if(LayerShellQt_FOUND AND WAYLAND_CLIENT_FOUND)
    target_sources(unisic PRIVATE src/notify/LayerShellNotifier.h src/notify/LayerShellNotifier.cpp)
    target_link_libraries(unisic PRIVATE LayerShellQt::Interface PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(unisic PRIVATE HAVE_LAYERSHELL)
    message(STATUS "Layer-shell on-top capture card: enabled")
else()
    message(STATUS "Layer-shell on-top capture card: disabled (needs layer-shell-qt-devel + wayland-client) — native notification only")
endif()

# Optional: KDE Plasma clipboard-history (Klipper) integration. Klipper only
# copies an image into its HISTORY (the tray applet, paste-it-later) when the
# clipboard offer carries the x-kde-force-image-copy marker MIME — plain
# image/png (what QClipboard and wl-copy send) is pasteable now but never
# recorded, so the shot vanishes as soon as anything else is copied (issue #51).
# KF6::GuiAddons' KSystemClipboard sets a full QMimeData (hint included) over the
# wlr data-control protocol, which also lands without a focused window — the very
# thing the wl-copy fallback papers over. Optional and NOT KDE-only to build:
# without it images still copy and paste fine, they just skip Klipper history.
if(KF6GuiAddons_FOUND)
    target_link_libraries(unisic PRIVATE KF6::GuiAddons)
    target_compile_definitions(unisic PRIVATE HAVE_KGUIADDONS)
    message(STATUS "KDE clipboard-history (Klipper) integration: enabled (KF6GuiAddons)")
else()
    message(STATUS "KDE clipboard-history (Klipper) integration: disabled (needs kf6-kguiaddons-devel) — images copy but skip Klipper history")
endif()

set_source_files_properties(qml/ToolCatalog.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(unisic
    URI Unisic
    VERSION 1.0
    # On case-insensitive filesystems the default "Unisic" output dir would
    # collide with the "unisic" executable name.
    OUTPUT_DIRECTORY qmlmod/Unisic
    # The Theme singleton and the generic U* design-system components live in
    # the Unisic.Kit module (external/unisic-kit) — only app-specific QML stays
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
        qml/pages/GifPage.qml
        qml/pages/EditPage.qml
        qml/pages/HistoryPage.qml
        qml/pages/DestinationsPage.qml
        qml/pages/SettingsPage.qml
        qml/components/USystemCheck.qml
        qml/components/UWelcome.qml
        qml/components/UNotifPreview.qml
        qml/components/UPatchNotes.qml
        qml/components/ToolPropsBar.qml
)

# qmltyperegistrations includes QML_ELEMENT headers by basename
target_include_directories(unisic PRIVATE src src/editor src/upload src/history src/notify src/update src/record)
# unisic-kit propagates its src/ include dir (capture/, record/, theme/,
# media/ headers), Qt6::DBus and, when found, PkgConfig::PIPEWIRE +
# HAVE_PIPEWIRE — so the app-side guards keep working unchanged.
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
# same :/resources/icons/sym/ path IconImageProvider looks up — nothing to add
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

# Translations (optional): needs Qt6 LinguistTools (lupdate/lrelease). When
# present, the .ts files are compiled to .qm and embedded in the qrc under
# /i18n/, and `cmake --build build --target update_translations` refreshes the
# source strings. Without the tools the app still builds — UI falls back to the
# English source strings and the language picker only offers "System"/"English".
find_package(Qt6 QUIET COMPONENTS LinguistTools)
set(UNISIC_TS_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_en.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_pl.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_es.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_it.ts
    ${CMAKE_CURRENT_SOURCE_DIR}/i18n/unisic_fr.ts)
if(Qt6LinguistTools_FOUND)
    qt_add_lrelease(unisic TS_FILES ${UNISIC_TS_FILES}
                    QM_FILES_OUTPUT_VARIABLE UNISIC_QM_FILES)
    qt_add_resources(unisic "i18n" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR}
                     FILES ${UNISIC_QM_FILES})
    target_compile_definitions(unisic PRIVATE HAVE_TRANSLATIONS)
    # Manual refresh target (`cmake --build build --target update_translations`):
    # rescans C++ + QML and rewrites the .ts source strings. Not run per build.
    # The kit's QML components carry user-facing qsTr() strings too (their
    # translation contexts are the file base names, unchanged by the move to
    # the submodule — the existing .ts entries keep matching).
    file(GLOB_RECURSE UNISIC_LUPDATE_SOURCES CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
         ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
         ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml
         ${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/src/*.cpp
         ${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/src/*.h
         ${CMAKE_CURRENT_SOURCE_DIR}/external/unisic-kit/qml/*.qml)
    # The target-less qt_add_lupdate() signature is Qt 6.7+. On the declared
    # 6.5/6.6 floor (e.g. openSUSE Leap ships 6.6) the first positional arg is
    # the mandatory target, so pass `unisic` explicitly there.
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
        qt_add_lupdate(TS_FILES ${UNISIC_TS_FILES} SOURCES ${UNISIC_LUPDATE_SOURCES})
    else()
        qt_add_lupdate(unisic TS_FILES ${UNISIC_TS_FILES} SOURCES ${UNISIC_LUPDATE_SOURCES})
    endif()
    message(STATUS "Translations: enabled (Polish, Spanish, Italian, French)")
else()
    message(STATUS "Translations: disabled (needs Qt6 LinguistTools) — English source strings only")
endif()

install(TARGETS unisic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# OBS repo signing key, shipped so the deb postinst can reference it from the
# apt .sources file it writes (native repo updates for direct .deb downloads —
# see packaging/obs/README.md).
install(FILES packaging/obs/home_unisic.key
    DESTINATION ${CMAKE_INSTALL_DATADIR}/unisic RENAME obs-signing-key.asc)
install(FILES resources/app.unisic.Unisic.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
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
set(UNISIC_DEBIAN_RUNTIME_DEPENDS
    ffmpeg
    libqt6svg6
    pipewire
    qml6-module-qtquick
    qml6-module-qtquick-controls
    qml6-module-qtquick-dialogs
    qml6-module-qtquick-effects
    qml6-module-qtquick-window
    qt6-wayland
    wl-clipboard
    xdg-desktop-portal)
string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS ${UNISIC_DEBIAN_RUNTIME_DEPENDS})
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "curl, xdg-desktop-portal-kde | xdg-desktop-portal-gnome | xdg-desktop-portal-wlr | xdg-desktop-portal-lxqt | xdg-desktop-portal-phosh | xdg-desktop-portal-gtk | xdg-desktop-portal-xapp")
# Direct-download debs self-register the OBS apt repo (postinst) so later
# versions arrive natively via apt; postrm drops the entry on purge.
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postrm")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
# The CPack rpm ships as the FEDORA release asset only: QML type
# registration links Qt PRIVATE API symbols (Qt_6.x_PRIVATE_API), pinning
# the binary to its build root's exact Qt minor — one rpm can never span
# distros, and openSUSE is served by the OBS repos instead. The Requires
# below stay distro-neutral anyway (boolean picks per distro; costs nothing
# and keeps a local `cpack -G RPM` on openSUSE installable):
# - "/usr/bin/ffmpeg" (file dependency) instead of the package name "ffmpeg":
#   stock Fedora ships ffmpeg-free, which installs the binary (sufficient for
#   GifRecorder's CLI usage) but only RPM Fusion Provides the name "ffmpeg" —
#   a package-name Requires would make the RPM uninstallable on stock Fedora.
# - Linked libraries (Qt, pipewire libs, tesseract, leptonica, ZXing,
#   LayerShellQt) are NOT listed: rpmbuild's soname autodeps cover them with
#   distro-neutral "libFoo.so.N()(64bit)" requires, and their package names
#   differ per distro ("tesseract" on openSUSE is a first-person shooter —
#   the OCR engine is "tesseract-ocr").
# - Boolean deps pick the split-package name per distro for the dlopened
#   pieces the autodep scanner cannot see (QML imports, wayland platform
#   plugin). The SVG imageformat plugin needs no entry: Fedora's qt6-qtsvg
#   and openSUSE's libQt6Svg6 both ship it and both arrive via the soname
#   autodep (verified on Tumbleweed + Leap 15.6).
set(CPACK_RPM_PACKAGE_REQUIRES "/usr/bin/ffmpeg, pipewire, wl-clipboard, xdg-desktop-portal, (qt6-qtdeclarative or qt6-declarative-imports), (qt6-qtwayland or qt6-wayland)")
# Direct-download rpms self-register the COPR repo (%post — dnf repo on
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
        src/hotkeys/ShortcutKeyMap.h
        src/hotkeys/ShortcutBinder.h src/hotkeys/ShortcutBinder.cpp
    )
    target_link_libraries(unisic_shortcut_binder_test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_include_directories(unisic_shortcut_binder_test PRIVATE src/hotkeys)
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

endif()

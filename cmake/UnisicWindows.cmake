# Windows (MinGW-w64, MSYS2 UCRT64) platform block of the app, included by
# CMakeLists.txt after the target exists. Nothing here touches the Linux build.
#
# Three mechanisms, so shared sources stay free of #ifdef:
#  1. A Linux-only .cpp stays in the source list but is not compiled; its
#     *_win.cpp sibling implements the same header instead.
#  2. src/platform/win/include holds stand-in headers for libraries MSYS2 does
#     not ship (LayerShellQt, KSystemClipboard, wayland-client, the evdev key
#     codes). Each one is the no-op the Linux code already falls back to.
#  3. The kit force-includes a POSIX compat header (fsync, O_CLOEXEC, fcntl...)
#     into every C/C++ file, this target included.
#
# A new Linux-only source file means one line in each list below, or the
# Windows CI job stops at link time.

set(_unisic_linux_only
    src/capture/KWinScreenShot2.cpp
    src/record/ClickCapture.cpp
    src/record/KeyCapture.cpp
    src/record/InputPermission.cpp
    src/diag/CrashHandler.cpp
    src/diag/RuntimeTools.cpp
    src/notify/NativeSound.cpp
    src/update/StagedUpdate.cpp)
set_source_files_properties(${_unisic_linux_only} PROPERTIES HEADER_FILE_ONLY ON SKIP_AUTOMOC ON)

target_sources(unisic PRIVATE
    src/capture/KWinScreenShot2_win.cpp
    src/record/ClickCapture_win.cpp
    src/record/KeyCapture_win.cpp
    src/record/InputPermission_win.cpp
    src/diag/CrashHandler_win.cpp
    src/diag/RuntimeTools_win.cpp
    src/notify/NativeSound_win.cpp
    src/update/StagedUpdate_win.cpp
    # The exe's icon (Explorer, taskbar, Start menu, the uninstall entry).
    src/platform/win/unisic.rc)

target_include_directories(unisic BEFORE PRIVATE src/platform/win/include)
# The test executables are created after this file runs and compile some of the
# same sources (KeystrokeOverlayPainter.cpp reads the evdev key codes), so the
# stand-ins go on the directory too, which every later target inherits.
include_directories(BEFORE src/platform/win/include)
# gdi32: BitBlt capture. dwmapi: window frame bounds. shcore: per-monitor DPI.
# winmm: PlaySoundW for the capture cues.
target_link_libraries(unisic PRIVATE gdi32 user32 dwmapi shcore winmm)
# NOMINMAX keeps <windows.h> from defining min/max over std:: and Qt's.
# WIN32_LEAN_AND_MEAN keeps it from pulling in ole2.h, whose `interface` macro
# breaks QDBusConnection::interface() in main.cpp.
target_compile_definitions(unisic PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN HAVE_WIN32_GDI_CAPTURE)

# A developer build keeps its console for qDebug; a release build is a GUI app
# that opens no console window.
if(NOT UNISIC_DEV_BUILD)
    set_target_properties(unisic PROPERTIES WIN32_EXECUTABLE ON)
endif()

set(UNISIC_WINDOWS_FEATURES HAVE_WIN32_GDI_CAPTURE)

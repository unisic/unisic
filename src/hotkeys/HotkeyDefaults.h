#pragma once

// Default global chords that differ per platform, as string literals (they go
// inside QStringLiteral). Keys every platform leaves free stay as literals in
// Settings.h.
//
// Windows owns Win+Shift+1..9 (taskbar), Win+Shift+C/F/R/S/T/W (Snipping Tool
// and other shell features) and Ctrl+Esc (Start), so RegisterHotKey refuses
// all of them. Its capture keys sit on Print Screen,
// where Windows users look for them. A bare Print goes to the Snipping Tool
// until the user turns that off in Windows Settings, so region also takes
// Ctrl+Print, which works out of the box.
//
// UNISIC_HOTKEY_NATIVE_BACKEND is the AppContext::hotkeyBackend id of the
// X11Hotkeys class, which Windows backs with RegisterHotKey instead of XGrabKey.
#ifdef _WIN32
#define UNISIC_HOTKEY_NATIVE_BACKEND "win32"
#define UNISIC_HOTKEY_FULLSCREEN "Ctrl+Shift+Print"
#define UNISIC_HOTKEY_REGION "Print, Ctrl+Print"
#define UNISIC_HOTKEY_WINDOW "Alt+Print"
#define UNISIC_HOTKEY_RECORD "Shift+Print"
#define UNISIC_HOTKEY_OCR "Meta+Shift+O"
#define UNISIC_HOTKEY_COPY_LAST "Meta+Shift+L"
#define UNISIC_HOTKEY_EMERGENCY_STOP "Ctrl+Alt+Escape"
#else
#define UNISIC_HOTKEY_NATIVE_BACKEND "x11"
#define UNISIC_HOTKEY_FULLSCREEN "Meta+Shift+1"
#define UNISIC_HOTKEY_REGION "Meta+Shift+2"
#define UNISIC_HOTKEY_WINDOW "Meta+Shift+3"
#define UNISIC_HOTKEY_RECORD "Meta+Shift+R"
#define UNISIC_HOTKEY_OCR "Meta+Shift+T"
#define UNISIC_HOTKEY_COPY_LAST "Meta+Shift+C"
#define UNISIC_HOTKEY_EMERGENCY_STOP "Ctrl+Escape"
#endif

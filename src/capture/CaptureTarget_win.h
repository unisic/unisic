#pragma once
#include <windows.h>

// The window "the active window" means on Windows, shared by the GDI window
// capture and the overlay's W key so both always aim at the same one.
namespace UnisicWin {
HWND captureTarget();
}

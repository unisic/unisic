// Windows build of InputPermission: click and key capture use low-level input
// hooks (ClickCapture_win.cpp, KeyCapture_win.cpp), which any desktop process
// may install, so there is no group to join and nothing to fix.
#include "InputPermission.h"

InputPermission::Status InputPermission::probe()
{
    return Available;
}

#include "PrintKeyRelay.h"

// Linux delivers the Print key press like any other key: nothing to relay.
// The Windows build is PrintKeyRelay_win.cpp.

PrintKeyRelay::~PrintKeyRelay() = default;

void PrintKeyRelay::setActive(bool active)
{
    m_active = active;
}

bool PrintKeyRelay::nativeEventFilter(const QByteArray &, void *, qintptr *)
{
    return false;
}

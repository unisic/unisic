// Windows build of PrintKeyRelay: while a shortcut recorder listens, a
// WM_KEYUP / WM_SYSKEYUP of VK_SNAPSHOT becomes a Qt Print key press for the
// focused window. Measured with injected Ctrl+Print on Windows 11: a plain
// Win32 window gets WM_KEYDOWN Ctrl, WM_KEYUP Print, WM_KEYUP Ctrl (no key-down
// for Print), and the QML recorder got Ctrl and nothing else.
#include "PrintKeyRelay.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>

#include <windows.h>

namespace {

// The modifiers held when the key-up was generated: GetKeyState answers for
// the message being processed, not for the moment it is read.
Qt::KeyboardModifiers heldModifiers()
{
    Qt::KeyboardModifiers m;
    if (GetKeyState(VK_SHIFT) < 0)
        m |= Qt::ShiftModifier;
    if (GetKeyState(VK_CONTROL) < 0)
        m |= Qt::ControlModifier;
    if (GetKeyState(VK_MENU) < 0)
        m |= Qt::AltModifier;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0)
        m |= Qt::MetaModifier;
    return m;
}

} // namespace

PrintKeyRelay::~PrintKeyRelay()
{
    setActive(false);
}

void PrintKeyRelay::setActive(bool active)
{
    if (active == m_active || !qApp)
        return;
    m_active = active;
    if (active)
        qApp->installNativeEventFilter(this);
    else
        qApp->removeNativeEventFilter(this);
}

bool PrintKeyRelay::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (eventType != QByteArrayLiteral("windows_generic_MSG"))
        return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if ((msg->message != WM_KEYUP && msg->message != WM_SYSKEYUP) || msg->wParam != VK_SNAPSHOT)
        return false;
    QWindow *window = QGuiApplication::focusWindow();
    if (!window)
        return false;
    const Qt::KeyboardModifiers mods = heldModifiers();
    const quint32 scanCode = (quint32(msg->lParam) >> 16) & 0x1ffu;
    // Posted, not sent: the recorder answers by ending the recording, which
    // removes this filter, and that must not happen inside the filter call.
    QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, Qt::Key_Print, mods,
                                                      scanCode, VK_SNAPSHOT, 0));
    QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, Qt::Key_Print, mods,
                                                      scanCode, VK_SNAPSHOT, 0));
    // Qt goes on to drop the key-up, as it always did.
    return false;
}

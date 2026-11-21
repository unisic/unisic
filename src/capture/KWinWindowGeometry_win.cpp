// Windows build of KWinWindowGeometry: the overlay's W key aims at the window
// the GDI "active window" capture would take, read from Win32 instead of a
// KWin script. Sampled at the same moment as on Linux (OverlayController::
// begin(), before the overlay maps and takes the focus), and answered right
// away, so the callback runs inside queryActiveWindow().
#include "KWinWindowGeometry.h"
#include "CaptureTarget_win.h"

#include <QGuiApplication>
#include <QScreen>
#include <dwmapi.h>

KWinWindowGeometry::~KWinWindowGeometry() = default;

bool KWinWindowGeometry::isAvailable()
{
    return QGuiApplication::platformName() == QLatin1String("windows");
}

void KWinWindowGeometry::queryActiveWindow(Callback cb)
{
    if (!cb)
        return;
    HWND wnd = UnisicWin::captureTarget();
    if (!wnd) {
        cb({}, QStringLiteral("no active window"));
        return;
    }
    // Cloaked: a suspended UWP app or one on another virtual desktop, which
    // Win32 still reports as visible.
    DWORD cloaked = 0;
    if (IsIconic(wnd)
        || (SUCCEEDED(DwmGetWindowAttribute(wnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
            && cloaked)) {
        cb({}, QStringLiteral("active window is not on screen"));
        return;
    }
    // The extended frame bounds leave out the invisible resize border that
    // GetWindowRect counts on Windows 10 and 11. Both are physical pixels.
    RECT r = {};
    if (FAILED(DwmGetWindowAttribute(wnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))
        && !GetWindowRect(wnd, &r)) {
        cb({}, QStringLiteral("could not read the active window's frame"));
        return;
    }
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        cb({}, QStringLiteral("active window is off screen"));
        return;
    }
    // Qt keeps each screen's origin in native pixels and divides only the
    // offsets from it by that screen's DPR (QHighDpi::fromNativePixels), so the
    // monitor holding most of the window maps it; on mixed DPI the part on the
    // other monitor is scaled by this one's factor.
    const QPoint origin(mi.rcMonitor.left, mi.rcMonitor.top);
    QScreen *screen = nullptr;
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->geometry().topLeft() == origin) {
            screen = s;
            break;
        }
    }
    if (!screen) {
        cb({}, QStringLiteral("no screen matches the active window's monitor"));
        return;
    }
    const qreal dpr = screen->devicePixelRatio();
    const QRect rect(origin.x() + qRound((r.left - origin.x()) / dpr),
                     origin.y() + qRound((r.top - origin.y()) / dpr),
                     qRound((r.right - r.left) / dpr), qRound((r.bottom - r.top) / dpr));
    if (rect.isEmpty()) {
        cb({}, QStringLiteral("no active window"));
        return;
    }
    cb(rect, QString());
}

// shortcut: the active window, whatever pids says; a process-tree window
// search (EnumWindows) would find a game that is not in front.
void KWinWindowGeometry::queryWindow(const QList<qint64> &, WindowCallback cb)
{
    if (!cb)
        return;
    queryActiveWindow([cb](const QRect &rect, const QString &error) { cb(rect, {}, error); });
}

// Only the KWin script calls this, over D-Bus; moc still needs a definition.
void KWinWindowGeometry::report(int, int, int, int, const QString &)
{
}

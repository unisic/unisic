// Windows build of KWinScreenShot2: the same four silent capture calls, served
// by GDI instead of org.kde.KWin.ScreenShot2. CaptureManager already tries this
// class first and falls back on any error, so no caller changes for Windows.
//
// Coordinates: Qt 6 runs per-monitor DPI aware (v2), so GetSystemMetrics,
// MONITORINFO and the DWM frame bounds are all physical pixels, the same space
// BitBlt reads from. Each image carries the DPR of the monitor it came from,
// as the KWin path carries KWin's scale.
#include "KWinScreenShot2.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <windows.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

namespace {

qreal dprForMonitor(HMONITOR mon)
{
    UINT dx = 0, dy = 0;
    if (mon && SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx > 0)
        return qreal(dx) / 96.0;
    return 1.0;
}

void drawCursor(HDC dc, const RECT &area)
{
    CURSORINFO ci = {};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return;
    ICONINFO ii = {};
    if (!GetIconInfo(ci.hCursor, &ii))
        return;
    DrawIconEx(dc, ci.ptScreenPos.x - LONG(ii.xHotspot) - area.left,
               ci.ptScreenPos.y - LONG(ii.yHotspot) - area.top,
               ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    if (ii.hbmMask)
        DeleteObject(ii.hbmMask);
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
}

// One BitBlt of a physical-pixel rect of the desktop. CAPTUREBLT pulls in
// layered windows (tooltips, translucent popups) that a plain SRCCOPY skips.
QImage grabRect(const RECT &r, bool includeCursor, qreal dpr, QString *error)
{
    const int w = int(r.right - r.left);
    const int h = int(r.bottom - r.top);
    if (w <= 0 || h <= 0) {
        *error = QStringLiteral("empty capture area");
        return {};
    }
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) {
        *error = QStringLiteral("Failed to allocate image buffer");
        return {};
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    const bool blitted = BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    if (blitted && includeCursor)
        drawCursor(mem, r);
    SelectObject(mem, old);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down, the row order QImage uses
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    // 32 bpp rows are w*4 bytes in both a DIB and a QImage, so one call fills it.
    const int rows = blitted ? GetDIBits(mem, bmp, 0, UINT(h), img.bits(), &bi, DIB_RGB_COLORS) : 0;
    const DWORD gdiError = rows == h ? 0 : GetLastError(); // before the cleanup calls touch it

    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    if (rows != h) {
        *error = QStringLiteral("GDI screen grab failed (error %1)").arg(gdiError);
        return {};
    }
    // GDI leaves the alpha byte undefined; the image is opaque.
    auto *px = reinterpret_cast<quint32 *>(img.bits());
    for (qsizetype i = 0, n = qsizetype(w) * h; i < n; ++i)
        px[i] |= 0xff000000u;
    img.setDevicePixelRatio(dpr);
    return img;
}

// The desktop, the taskbar, or one of ours (hidden for the capture, or the
// tray menu): never what "the active window" means.
bool isShellOrOwn(HWND w)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() || w == GetShellWindow())
        return true;
    wchar_t cls[32];
    const int n = GetClassNameW(w, cls, 32);
    const QString c = QString::fromWCharArray(cls, n > 0 ? n : 0);
    return c == QLatin1String("Shell_TrayWnd") || c == QLatin1String("Shell_SecondaryTrayWnd")
           || c == QLatin1String("Progman") || c == QLatin1String("WorkerW");
}

// A window a user would call one: shown, not minimised, not a tool window or
// an owned popup, not cloaked (a suspended UWP app or one on another virtual
// desktop stays "visible" to Win32), and on a screen (an app parking a
// helper window at -32000 would otherwise be picked and fail as off screen).
BOOL CALLBACK pickTopWindow(HWND w, LPARAM out)
{
    if (!IsWindowVisible(w) || IsIconic(w) || GetWindow(w, GW_OWNER) || isShellOrOwn(w)
        || (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
        return TRUE;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;
    RECT r;
    if (!GetWindowRect(w, &r) || IsRectEmpty(&r) || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
        return TRUE;
    *reinterpret_cast<HWND *>(out) = w;
    return FALSE;
}

// The foreground window, unless that is the taskbar (where a tray-menu pick
// leaves the focus) or ours; then the topmost real window. EnumWindows walks
// top to bottom, and unlike a GetWindow loop it survives the z-order changing.
HWND captureTarget()
{
    HWND fg = GetForegroundWindow();
    if (fg && !isShellOrOwn(fg))
        return fg;
    HWND top = nullptr;
    EnumWindows(pickTopWindow, reinterpret_cast<LPARAM>(&top));
    return top;
}

struct MonitorMatch {
    const wchar_t *device = nullptr;
    QPoint topLeft;
    HMONITOR byName = nullptr;
    HMONITOR byPos = nullptr;
};

BOOL CALLBACK matchMonitor(HMONITOR mon, HDC, LPRECT, LPARAM data)
{
    auto *m = reinterpret_cast<MonitorMatch *>(data);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return TRUE;
    if (m->device && wcscmp(mi.szDevice, m->device) == 0)
        m->byName = mon;
    if (mi.rcMonitor.left == m->topLeft.x() && mi.rcMonitor.top == m->topLeft.y())
        m->byPos = mon;
    return TRUE;
}

// QScreen::name() on Windows is the GDI device name or the monitor's friendly
// name depending on what Qt could read; when it is not the device name, the
// screen's top-left is the fallback key (Qt keeps each screen's origin in
// native pixels and scales only its size).
HMONITOR monitorForScreen(const QString &screenName, qreal *dpr)
{
    QScreen *screen = nullptr;
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->name() == screenName) {
            screen = s;
            break;
        }
    }
    if (!screen)
        return nullptr;
    const std::wstring device = screenName.toStdWString();
    MonitorMatch m;
    m.device = device.c_str();
    m.topLeft = screen->geometry().topLeft();
    EnumDisplayMonitors(nullptr, nullptr, matchMonitor, reinterpret_cast<LPARAM>(&m));
    *dpr = screen->devicePixelRatio();
    return m.byName ? m.byName : m.byPos;
}

QImage grabMonitor(HMONITOR mon, bool includeCursor, qreal dpr, QString *error)
{
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        *error = QStringLiteral("monitor not found");
        return {};
    }
    return grabRect(mi.rcMonitor, includeCursor, dpr, error);
}

// The capture runs on the next event-loop turn, never inside the caller: the
// KWin path is asynchronous and callers are written for that, and the turn
// lets a just-closed tray menu leave the screen before the grab.
void deliverLater(QObject *ctx, std::function<QImage(QString *)> grab, KWinScreenShot2::Callback cb)
{
    QTimer::singleShot(0, ctx, [grab = std::move(grab), cb = std::move(cb)] {
        QString error;
        const QImage img = grab(&error);
        if (img.isNull() && error.isEmpty())
            error = QStringLiteral("capture failed");
        cb(img, error);
    });
}

} // namespace

bool KWinScreenShot2::isAvailable()
{
    return QGuiApplication::platformName() == QLatin1String("windows");
}

QString KWinScreenShot2::backendName()
{
    return QStringLiteral("GDI");
}

void KWinScreenShot2::captureWorkspace(bool includeCursor, Callback cb)
{
    deliverLater(this, [includeCursor](QString *error) {
        RECT r;
        r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        // KWin renders a workspace shot at the highest output scale; do the same.
        qreal dpr = 1.0;
        for (QScreen *s : QGuiApplication::screens())
            dpr = qMax(dpr, s->devicePixelRatio());
        return grabRect(r, includeCursor, dpr, error);
    }, std::move(cb));
}

void KWinScreenShot2::captureScreen(const QString &screenName, bool includeCursor, Callback cb)
{
    deliverLater(this, [screenName, includeCursor](QString *error) {
        qreal dpr = 1.0;
        HMONITOR mon = monitorForScreen(screenName, &dpr);
        if (!mon) {
            *error = QStringLiteral("no monitor matches screen %1").arg(screenName);
            return QImage();
        }
        return grabMonitor(mon, includeCursor, dpr, error);
    }, std::move(cb));
}

void KWinScreenShot2::captureActiveScreen(bool includeCursor, Callback cb)
{
    deliverLater(this, [includeCursor](QString *error) {
        POINT p = {};
        GetCursorPos(&p);
        HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
        return grabMonitor(mon, includeCursor, dprForMonitor(mon), error);
    }, std::move(cb));
}

void KWinScreenShot2::captureActiveWindow(bool includeCursor, Callback cb)
{
    deliverLater(this, [includeCursor](QString *error) {
        HWND wnd = captureTarget();
        if (!wnd) {
            *error = QStringLiteral("no active window");
            return QImage();
        }
        // The extended frame bounds leave out the invisible resize border that
        // GetWindowRect counts on Windows 10 and 11.
        RECT r = {};
        if (FAILED(DwmGetWindowAttribute(wnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
            GetWindowRect(wnd, &r);
        // A maximised window reaches past its monitor; keep the part on screen.
        RECT desk;
        desk.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        desk.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        desk.right = desk.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        desk.bottom = desk.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        RECT visible;
        if (!IntersectRect(&visible, &r, &desk)) {
            *error = QStringLiteral("active window is off screen");
            return QImage();
        }
        HMONITOR mon = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
        return grabRect(visible, includeCursor, dprForMonitor(mon), error);
    }, std::move(cb));
}

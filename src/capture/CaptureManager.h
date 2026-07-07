#pragma once
#include <QObject>
#include <QImage>
#include <QScreen>
#include <functional>

class PortalScreenshot;
class KWinScreenShot2;
class Settings;

// Chooses the best capture backend: KWin ScreenShot2 (silent, exact) when
// running on KDE and authorized, xdg-desktop-portal Screenshot otherwise.
class CaptureManager : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;
    using MultiCallback = std::function<void(const QVector<QImage> &images, const QString &error)>;

    explicit CaptureManager(Settings *settings, QObject *parent = nullptr);

    // Full virtual desktop (all monitors, one image in workspace coordinates).
    void captureWorkspace(Callback cb);
    // One screen by QScreen (KWin path uses screen->name()).
    void captureScreen(QScreen *screen, Callback cb);
    // All screens at once (overlay freeze): KWin per-screen serially, or ONE
    // portal workspace capture cropped per screen — never N portal requests.
    void captureAllScreens(const QVector<QScreen *> &screens, MultiCallback cb);
    void captureActiveWindow(Callback cb);

private:
    void portalFallback(Callback cb);
    void kwinScreensSerial(QVector<QScreen *> screens, int index, QVector<QImage> acc,
                           MultiCallback cb);
    void portalAllScreens(QVector<QScreen *> screens, MultiCallback cb,
                          const QString &previousError = {});
    static QImage cropForScreen(const QImage &workspace, QScreen *screen);

    Settings *m_settings;
    PortalScreenshot *m_portal;
    KWinScreenShot2 *m_kwin;
    bool m_kwinDenied = false; // remember auth failure, skip straight to portal
};

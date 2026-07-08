#pragma once
#include <QObject>
#include <QImage>
#include <QScreen>
#include <functional>

class PortalScreenshot;
class KWinScreenShot2;
class GnomeScreenshot;
class Settings;

// Chooses the best capture backend: KWin ScreenShot2 (silent, exact) when
// running on KDE and authorized, xdg-desktop-portal Screenshot otherwise.
class CaptureManager : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;
    using MultiCallback = std::function<void(const QVector<QImage> &images, const QString &error)>;

    // A QScreen* copied into an async capture callback can dangle if the monitor
    // is removed (unplug / DP link-drop / KVM) during the round-trip. Snapshot the
    // geometry we need into a plain value before issuing the async call and crop
    // from that instead of dereferencing the live QScreen later.
    struct ScreenGeom {
        QRect geometry;
        QRect virtualGeometry;
        qreal dpr = 1.0;
        QString name;
    };

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
    // One whole-workspace screenshot via the best non-KWin backend. On niri the
    // portal is broken (niri+NVIDIA), so we prefer the compositor-native
    // org.gnome.Shell.Screenshot there and fall back to the portal; everywhere
    // else the portal is primary and GNOME-direct is the last-resort rescue.
    void portalFallback(Callback cb);
    void workspaceFallback(Callback cb, bool allowInteractive, const QString &previousError = {});
    bool preferGnome() const;
    void kwinScreensSerial(QVector<QScreen *> screens, int index, QVector<QImage> acc,
                           MultiCallback cb);
    void portalAllScreens(QVector<QScreen *> screens, MultiCallback cb,
                          const QString &previousError = {});

    static ScreenGeom snapshotScreen(QScreen *screen);
    static QImage cropForScreen(const QImage &workspace, const ScreenGeom &screen);

    Settings *m_settings;
    PortalScreenshot *m_portal;
    KWinScreenShot2 *m_kwin;
    GnomeScreenshot *m_gnome;
    bool m_kwinDenied = false; // remember auth failure, skip straight to portal
};

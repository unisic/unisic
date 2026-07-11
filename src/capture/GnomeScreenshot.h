#pragma once
#include <QObject>
#include <QImage>
#include <QSet>
#include <QString>
#include <functional>

// Compositor-native backend using the org.gnome.Shell.Screenshot D-Bus API.
//
// Why this exists: niri emulates GNOME and exposes org.gnome.Shell.Screenshot
// *itself*, natively. On niri + NVIDIA (Fedora) the normal path —
// xdg-desktop-portal-gnome — fails ("Failed to associate portal window with
// parent window ''", "Failed to get screenshot: internal error"), so the portal
// can never return an image. Calling org.gnome.Shell.Screenshot directly skips
// that broken middle layer and talks to niri straight, which works.
//
// This is the same class of "compositor-native D-Bus enhancement/fallback" as
// KWinScreenShot2 is for KDE — a legit Wayland path, not an X11 hack.
//
// On real GNOME (mutter) the interface is locked down to allowlisted callers and
// the call fails; that is fine because we only use this as a *fallback* after the
// portal, or when the desktop is explicitly niri.
class GnomeScreenshot : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;

    explicit GnomeScreenshot(QObject *parent = nullptr) : QObject(parent) {}
    ~GnomeScreenshot() override;

    // The org.gnome.Shell.Screenshot service is on the bus (niri, gnome-shell, or
    // a Mutter-emulating compositor). Does not guarantee the call is permitted.
    static bool isAvailable();
    // True when the running session is niri (XDG_CURRENT_DESKTOP / XDG_SESSION_DESKTOP).
    // niri leaves the interface open, so we can prefer it there over the portal.
    static bool isNiriSession();

    // Whole workspace (all monitors) into one image. Mirrors the portal's
    // full-workspace result so CaptureManager::cropForScreen can slice it.
    void captureWorkspace(bool includeCursor, Callback cb);
    // The focused window (with frame).
    void captureActiveWindow(bool includeCursor, Callback cb);

private:
    // method = Screenshot | ScreenshotArea | ScreenshotWindow; leadingArgs are the
    // method-specific args that precede (flash, filename); the reply is (b success,
    // s filenameUsed) and the PNG is read back from the temp file we hand it.
    void shoot(const QString &method, const QVariantList &leadingArgs, Callback cb);

    // Temp PNGs the compositor may have written but whose reply hasn't landed yet.
    // Cleaned in the finished handler; any survivors are removed by the destructor
    // so a shutdown mid-capture doesn't orphan a file in the temp dir.
    QSet<QString> m_pending;
};

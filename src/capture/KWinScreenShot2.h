#pragma once
#include <QObject>
#include <QImage>
#include <QVariantMap>
#include <functional>

// KDE-specific backend using org.kde.KWin.ScreenShot2. Silent (no dialogs),
// supports per-screen / active-window / workspace capture. Requires the
// installed .desktop file to declare
//   X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
// otherwise KWin rejects the call and we fall back to the portal.
class KWinScreenShot2 : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QImage &image, const QString &error)>;

    explicit KWinScreenShot2(QObject *parent = nullptr) : QObject(parent) {}

    static bool isAvailable();

    void captureWorkspace(bool includeCursor, Callback cb);
    void captureScreen(const QString &screenName, bool includeCursor, Callback cb);
    void captureActiveWindow(bool includeCursor, Callback cb);
    void captureArea(int x, int y, int w, int h, bool includeCursor, Callback cb);

private:
    void call(const QString &method, const QVariantList &args, bool includeCursor, Callback cb);
};

#pragma once
#include <QObject>
#include <QRect>
#include <functional>

class QTemporaryFile;
class QTimer;

// Where the ACTIVE window is, in KWin's own coordinates. On Windows the
// KWinWindowGeometry_win.cpp sibling answers from Win32 in the same space.
//
// Wayland tells a client nothing about anyone else's window - not the stack,
// not the geometry - and ScreenShot2 hands back pixels with no coordinates
// attached, so neither route can aim the region recorder at the window the user
// is looking at. KWin's scripting API is the sanctioned source for that
// (`workspace.activeWindow.frameGeometry`), and it is not a restricted
// interface: load a throwaway script, let it call back over D-Bus, unload it.
//
// The reply lands on a per-process bus name, so a dev build and the installed
// app querying at the same moment can never eat each other's answer.
class KWinWindowGeometry : public QObject
{
    Q_OBJECT
    // Pins the D-Bus interface the KWin script calls into. Without it Qt derives
    // it from the class AND the application name ("local.unisic.KWinWindowGeometry"
    // for the installed app, "local.unisic-dev...." for a dev build - measured),
    // so the name the script hardcodes would be wrong in half the builds.
    Q_CLASSINFO("D-Bus Interface", "app.unisic.KWinWindowGeometry")
public:
    // rect: GLOBAL LOGICAL pixels (KWin's space, i.e. what QScreen::geometry
    // uses), empty when error is set.
    using Callback = std::function<void(const QRect &rect, const QString &error)>;
    // uuid: KWin's id of the window (what a window screencast takes), empty
    // on Windows.
    using WindowCallback = std::function<void(const QRect &rect, const QString &uuid, const QString &error)>;

    explicit KWinWindowGeometry(QObject *parent = nullptr) : QObject(parent) {}
    ~KWinWindowGeometry() override;

    static bool isAvailable();

    // One query at a time: a second call while one is in flight fails fast
    // instead of leaving two scripts loaded under the same name.
    void queryActiveWindow(Callback cb);
    // A game's window rather than the active one: the largest normal window
    // owned by one of pids, or the active window when pids is empty. "No
    // window" while the game is still loading is an error like any other.
    void queryWindow(const QList<qint64> &pids, WindowCallback cb);

public slots:
    // What the KWin script calls back into, on the interface fixed by the
    // Q_CLASSINFO above - the script's call must name exactly that.
    void report(int x, int y, int w, int h, const QString &uuid);

private:
    void finish(const QRect &rect, const QString &error, const QString &uuid = {});
    void cleanup();

    WindowCallback m_cb;
    QTemporaryFile *m_script = nullptr;
    QTimer *m_timeout = nullptr;
    QString m_pluginName;
    QString m_serviceName;
    QString m_objectPath;
};

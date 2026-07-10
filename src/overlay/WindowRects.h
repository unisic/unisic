#pragma once
#include <QObject>
#include <QRect>
#include <QVector>
#include <functional>

// Real window frame geometries from the compositor. Pixel analysis can only
// GUESS where windows are — the compositor knows. KDE-specific enhancement
// (KWin scripting API over D-Bus, same spirit as KWinScreenShot2); non-KDE
// sessions call back with an empty list and callers keep the pure
// pixel-detection candidates.
class WindowRects : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.unisic.WindowRects")
public:
    using Callback = std::function<void(const QVector<QRect> &globalLogicalRects)>;

    // One-shot, async: registers a temporary D-Bus receiver on this app's
    // connection, loads+runs a KWin script that reports every un-minimized
    // normal/dialog/dock window on the current desktop (bottom to top,
    // frameGeometry in GLOBAL LOGICAL coordinates), then unloads the script.
    // Falls back to an empty list after 500 ms or on any D-Bus failure.
    // `context` scopes the callback: if it dies first, the callback is
    // dropped.
    static void query(QObject *context, Callback cb);

public slots:
    // Called back BY THE KWIN SCRIPT via callDBus with a JSON rect array.
    Q_SCRIPTABLE void pushWindows(const QString &json);

private:
    explicit WindowRects(QObject *parent = nullptr) : QObject(parent) {}
    void finish(const QVector<QRect> &rects);

    Callback m_cb;
    QString m_dbusPath;
    QString m_pluginName;
    bool m_done = false;
};

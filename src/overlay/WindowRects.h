#pragma once
#include <QObject>
#include <QRect>
#include <QVector>
#include <functional>

// Real window frame geometries from the compositor. Pixel analysis can only
// GUESS where windows are — the compositor knows. Backends, picked by
// session: KWin scripting (KDE), GNOME Shell Introspect, sway (swaymsg),
// Hyprland (hyprctl). Anything else calls back with an empty list and
// callers keep the pure pixel-detection candidates, so smart pick works
// everywhere — just with less precision where the compositor tells nothing.
class WindowRects : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.unisic.WindowRects")
public:
    using Callback = std::function<void(const QVector<QRect> &globalLogicalRects,
                                        const QString &backend)>;

    // One-shot, async: dispatches to the session's backend and calls back
    // with frame rects in GLOBAL LOGICAL coordinates plus the backend name
    // ("" when none matched). Falls back to an empty list after 500 ms or on
    // any failure. `context` scopes the callback: if it dies first, the
    // callback is dropped.
    static void query(QObject *context, Callback cb);

public slots:
    // Called back BY THE KWIN SCRIPT via callDBus with a JSON rect array.
    Q_SCRIPTABLE void pushWindows(const QString &json);

private:
    explicit WindowRects(QObject *parent = nullptr) : QObject(parent) {}
    void finish(const QVector<QRect> &rects);
    void startKWin();
    void startGnome();
    void startProcess(const QString &program, const QStringList &args,
                      std::function<QVector<QRect>(const QByteArray &)> parse);

    Callback m_cb;
    QString m_backend;
    QString m_dbusPath;      // KWin backend only
    QString m_pluginName;    // KWin backend only
    bool m_done = false;
};

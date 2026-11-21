#pragma once
#include <QObject>
#include <QPointer>

class AppContext;
class QProcess;
class QQuickWindow;

// The clip indicator (ClipOsd.qml): "Instant replay on", "Saving clip…",
// "Clip saved". Shown over everything, fullscreen games included, and never
// takes a click or the keyboard. Where it lives follows the capture card:
// a layer-shell overlay (KWin, wlroots, COSMIC), a topmost window on Windows,
// or an override-redirect window in the --osd-helper process on X11 and on
// Wayland desktops without layer-shell (GNOME, through XWayland). Nowhere
// else (no layer-shell, no X server) it shows nothing.
//
// Cost: one small QML window (or one helper process) alive only while the
// pill is on screen; at most one at a time, reused by the next show().
class ClipOsd : public QObject
{
    Q_OBJECT
public:
    ClipOsd(AppContext *app, bool layerShell, QObject *parent = nullptr);
    ~ClipOsd() override;

    // tone: "rec", "busy", "ok" or "error" (ClipOsd.qml).
    void show(const QString &title, const QString &detail, const QString &tone, int ms = 2600);

private:
    AppContext *m_app;
    bool m_layerShell;
    QPointer<QQuickWindow> m_win;
    QProcess *m_helper = nullptr;
};

// `unisic --osd-helper <screen> <x> <y> <w> <h>`: the XWayland/X11 host of
// ClipOsd.qml. Reads "title\tdetail\ttone\tms" lines on stdin, quits once the
// pill fades out or stdin closes, so it never outlives the parent.
int runOsdHelper(int argc, char *argv[]);

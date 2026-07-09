#pragma once
#include <QObject>
#include <QPointer>
#include <qqmlregistration.h>

class QQuickWindow;

// Per-window backend for the floating capture preview. Owns the Wayland-legit
// "always on top" behaviour: where wlr-layer-shell is available it puts the
// window on the overlay layer (a plain xdg-toplevel's stays-on-top hint is
// advisory and KWin/wlroots ignore it), else it falls back to the Qt window
// flag. Also drives click-through, dragging and close so QML never touches
// window flags directly (rebinding them mid-life was resetting opacity and
// eating the close button).
class PreviewController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppContext")
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    Q_PROPERTY(bool passthrough READ passthrough WRITE setPassthrough NOTIFY passthroughChanged)
    // True when real always-on-top is available (layer-shell); QML uses it to
    // pick the drag strategy (margin nudging vs. compositor system-move).
    Q_PROPERTY(bool layerShell READ layerShell CONSTANT)

public:
    PreviewController(QQuickWindow *win, bool layerShell, QObject *parent = nullptr);

    bool pinned() const { return m_pinned; }
    bool passthrough() const { return m_passthrough; }
    bool layerShell() const { return m_layerShell; }

    void setPinned(bool on);
    void setPassthrough(bool on);

    // Configure the surface just before the window is shown. Must run while the
    // platform window can still be created as a layer surface.
    void attach();

    Q_INVOKABLE void togglePassthrough() { setPassthrough(!m_passthrough); }
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void startMove();               // non-layer: hand a move-grab to the compositor
    Q_INVOKABLE void moveBy(int dx, int dy);    // layer: nudge the surface via margins

signals:
    void pinnedChanged();
    void passthroughChanged();

private:
    void applyLayer();          // push the pinned/passthrough state onto the surface
    void applyWindowFlags();    // non-layer fallback

    QPointer<QQuickWindow> m_win;
    bool m_layerShell;
    bool m_pinned = true;
    bool m_passthrough = false;
    int m_marginTop = 64;
    int m_marginRight = 64;
};

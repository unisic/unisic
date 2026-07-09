#pragma once
#include <QObject>
#include <QPointer>
#include <qqmlregistration.h>

class QQuickWindow;

// Per-window backend for the floating capture preview. Owns the Wayland-legit
// "always on top" behaviour: where wlr-layer-shell is available the preview is
// a FULLSCREEN overlay-layer surface whose input region is masked down to the
// visible card (same pattern as the capture popup) — the card then moves as a
// plain scene-graph item, which is the only way to get smooth dragging on a
// layer surface (they can't be system-moved, and repositioning one per pointer
// event diverges against compositor lag). Without layer-shell it falls back to
// a normal window: stays-on-top hint + startSystemMove.
class PreviewController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppContext")
    Q_PROPERTY(bool pinned READ pinned WRITE setPinned NOTIFY pinnedChanged)
    // True when the fullscreen-surface + input-mask mode is active; QML uses it
    // to pick the card layout (movable item vs. fills-the-window) and the drag
    // strategy (item drag vs. compositor system-move).
    Q_PROPERTY(bool layerShell READ layerShell CONSTANT)

public:
    PreviewController(bool layerShell, QObject *parent = nullptr);

    // Bind the window after the QML component is created (the controller must
    // already exist as a context property before create() so QML resolves it).
    void setWindow(QQuickWindow *win);

    bool pinned() const { return m_pinned; }
    bool layerShell() const { return m_layerShell; }

    void setPinned(bool on);

    // Configure the surface just before the window is shown. Must run while the
    // platform window can still be created as a layer surface.
    void attach();

    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void startMove();               // non-layer: hand a move-grab to the compositor
    // Layer mode: clip pointer input to the card's rect (logical px in window
    // coords) so everything outside it clicks through to the desktop.
    Q_INVOKABLE void setInputRect(int x, int y, int w, int h);

signals:
    void pinnedChanged();

private:
    void applyLayer();          // push the pinned state onto the surface
    void applyWindowFlags();    // non-layer fallback

    QPointer<QQuickWindow> m_win;
    bool m_layerShell;
    bool m_pinned = true;
};

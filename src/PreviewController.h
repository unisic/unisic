#pragma once
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <qqmlregistration.h>

class QQuickWindow;
class QScreen;

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
    // True only inside a screen hop's hide/show: the window owner must not
    // treat that hide as a close (it deletes the window and its temp file).
    bool hopping() const { return m_hopping; }

    void setPinned(bool on);

    // Configure the surface just before the window is shown. Must run while the
    // platform window can still be created as a layer surface.
    void attach();

    Q_INVOKABLE void startMove();               // non-layer: hand a move-grab to the compositor
    // Layer mode: clip pointer input to the card's rect (logical px in window
    // coords) so everything outside it clicks through to the desktop.
    Q_INVOKABLE void setInputRect(int x, int y, int w, int h);
    // Layer mode: a layer surface is bound to one output for its whole life, so
    // the QML item drag stops at the surface edge. Once the pointer crosses it,
    // QML hands the drag over here (pointer in window coords, grab offset in
    // the card) and this object carries the card until the button is released,
    // re-creating the surface on whichever output the pointer reaches.
    // cardPlaced() tells QML where the card goes.
    Q_INVOKABLE void startCarry(qreal x, qreal y, qreal grabX, qreal grabY);
    // Re-create the surface on the output containing `global`; false when that
    // is no screen or the current one. Used by the carry and the smoke check.
    bool hopTo(const QPoint &global);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void pinnedChanged();
    void cardPlaced(qreal x, qreal y);

private:
    void applyLayer();          // push the pinned state onto the surface
    void applyWindowFlags();    // non-layer fallback
    void hop(QScreen *to);
    void carryTo(const QPointF &global);
    void endCarry();

    QPointer<QQuickWindow> m_win;
    bool m_layerShell;
    bool m_pinned = true;
    bool m_hopping = false;
    // Carry state. Pointer events keep arriving relative to some surface
    // origin that is not necessarily the current output's: KWin keeps the
    // implicit grab's transform from the press, so positions stay relative to
    // the ORIGINAL output until release. m_frameOrigin is that origin,
    // re-decided after each hop (m_checkFrame) by continuity with m_lastGlobal.
    bool m_carrying = false;
    bool m_checkFrame = false;
    QPointF m_grab;
    QPointF m_frameOrigin;
    QPointF m_lastGlobal;
};

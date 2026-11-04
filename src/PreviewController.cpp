#include "PreviewController.h"
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QScreen>
#include <QRegion>

#include <LayerShellQt/window.h>

PreviewController::PreviewController(bool layerShell, QObject *parent)
    : QObject(parent), m_layerShell(layerShell)
{
}

void PreviewController::setWindow(QQuickWindow *win)
{
    m_win = win;
}

void PreviewController::attach()
{
    if (!m_win)
        return;
    // m_layerShell is a RUNTIME flag from AppContext (the compositor advertises
    // zwlr_layer_shell_v1), not a build one: GNOME/mutter and X11 land in the
    // plain-window branch below.
    if (m_layerShell) {
        if (auto *ls = LayerShellQt::Window::get(m_win)) {
            using LW = LayerShellQt::Window;
            ls->setScope(QStringLiteral("unisic-preview"));
            // Fullscreen surface (all four anchors, size from the anchors); the
            // visible card is a movable item inside it and setInputRect() keeps
            // pointer input clipped to the card. Dragging is then pure QML.
            // All four anchors => the compositor sizes the surface to the
            // output (fullscreen). No setDesiredSize call: it is the default
            // (0,0 = follow anchors) AND it is absent from older LayerShellQt
            // (the distro/AppImage builds), which broke the CI compile.
            ls->setAnchors(LW::Anchors(LW::AnchorTop | LW::AnchorBottom
                                       | LW::AnchorLeft | LW::AnchorRight));
            ls->setExclusiveZone(-1); // may float over panels
            // OnDemand: the card takes keyboard focus when clicked but doesn't
            // steal it, so global shortcuts keep working.
            ls->setKeyboardInteractivity(LW::KeyboardInteractivityOnDemand);
        }
        applyLayer();
        return;
    }
    applyWindowFlags();
}

void PreviewController::setPinned(bool on)
{
    if (m_pinned == on)
        return;
    m_pinned = on;
    if (m_layerShell)
        applyLayer();
    else
        applyWindowFlags();
    emit pinnedChanged();
}

void PreviewController::applyLayer()
{
    if (!m_win)
        return;
    if (auto *ls = LayerShellQt::Window::get(m_win)) {
        using LW = LayerShellQt::Window;
        // Pinned → overlay (above panels & fullscreen). Unpinned → top (still
        // above normal windows, but yields to fullscreen).
        ls->setLayer(m_pinned ? LW::LayerOverlay : LW::LayerTop);
        // Layer-shell properties reach the compositor with the next surface
        // commit; a static preview renders no new frames, so force one.
        m_win->requestUpdate();
    }
}

void PreviewController::applyWindowFlags()
{
    if (!m_win)
        return;
    m_win->setFlag(Qt::WindowStaysOnTopHint, m_pinned);
}

void PreviewController::startMove()
{
    if (m_win && !m_layerShell)
        m_win->startSystemMove();
}

void PreviewController::setInputRect(int x, int y, int w, int h)
{
    if (!m_win || !m_layerShell)
        return;
    // qtwayland turns the mask into the wl_surface input region.
    m_win->setMask(QRegion(x, y, w, h));
}

void PreviewController::hop(QScreen *to)
{
    // The output is picked from QWindow::screen() when the layer surface is
    // created (same rule as the first show in AppContext::finishOpenPreview),
    // so re-map: hide, rebind, show. LayerShellQt keeps anchors/layer/scope on
    // the QWindow, they apply again to the new surface.
    m_hopping = true;
    m_win->hide();
    m_win->setScreen(to);
    m_win->show();
    m_hopping = false;
    m_win->requestActivate();
}

bool PreviewController::hopTo(const QPoint &global)
{
    if (!m_win || !m_layerShell || !m_win->screen())
        return false;
    QScreen *to = QGuiApplication::screenAt(global);
    if (!to || to == m_win->screen())
        return false;
    hop(to);
    return true;
}

void PreviewController::startCarry(qreal x, qreal y, qreal grabX, qreal grabY)
{
    if (!m_win || !m_layerShell || m_carrying || !m_win->screen())
        return;
    // All four anchors + exclusive zone -1: the surface covers its output
    // exactly, so window coords are output-local logical coords.
    m_frameOrigin = m_win->screen()->geometry().topLeft();
    m_grab = QPointF(grabX, grabY);
    m_carrying = true;
    m_checkFrame = false;
    m_win->installEventFilter(this);
    carryTo(m_frameOrigin + QPointF(x, y));
}

void PreviewController::carryTo(const QPointF &global)
{
    m_lastGlobal = global;
    if (hopTo(global.toPoint()))
        m_checkFrame = true;
    // Off every screen (a gap in the layout): the card stays on this output,
    // QML clamps it to the edge.
    const QPointF local = global - m_win->screen()->geometry().topLeft() - m_grab;
    emit cardPlaced(local.x(), local.y());
}

void PreviewController::endCarry()
{
    m_carrying = false;
    if (m_win)
        m_win->removeEventFilter(this);
}

bool PreviewController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_win || !m_carrying)
        return false;
    switch (event->type()) {
    case QEvent::MouseMove: {
        const QPointF pos = static_cast<QMouseEvent *>(event)->position();
        if (m_checkFrame) {
            // First event after a hop: still in the old frame (KWin) or already
            // relative to the new surface (a compositor that re-bases the grab)?
            // The pointer moved a few px since the last event, so the frame
            // that keeps it continuous is the right one.
            const QPointF onNew = QPointF(m_win->screen()->geometry().topLeft()) + pos;
            if ((onNew - m_lastGlobal).manhattanLength()
                < (m_frameOrigin + pos - m_lastGlobal).manhattanLength())
                m_frameOrigin = m_win->screen()->geometry().topLeft();
            m_checkFrame = false;
        }
        carryTo(m_frameOrigin + pos);
        return true; // the QML item drag would clamp the card against it
    }
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonPress: // release lost to the compositor: stop here
        endCarry();
        return false; // let a still-grabbing MouseArea see its release
    default:
        return false;
    }
}

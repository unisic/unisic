#include "PreviewController.h"
#include <QQuickWindow>
#include <QRegion>

#ifdef HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
#endif

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
#ifdef HAVE_LAYERSHELL
    if (m_layerShell) {
        if (auto *ls = LayerShellQt::Window::get(m_win)) {
            using LW = LayerShellQt::Window;
            ls->setScope(QStringLiteral("unisic-preview"));
            // Fullscreen surface (all four anchors, size from the anchors); the
            // visible card is a movable item inside it and setInputRect() keeps
            // pointer input clipped to the card. Dragging is then pure QML.
            ls->setAnchors(LW::Anchors(LW::AnchorTop | LW::AnchorBottom
                                       | LW::AnchorLeft | LW::AnchorRight));
            ls->setDesiredSize(QSize(0, 0));
            ls->setExclusiveZone(-1); // may float over panels
            // OnDemand: the card takes keyboard focus when clicked but doesn't
            // steal it, so global shortcuts (incl. the passthrough toggle) work.
            ls->setKeyboardInteractivity(LW::KeyboardInteractivityOnDemand);
        }
        applyLayer();
        return;
    }
#endif
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

void PreviewController::setPassthrough(bool on)
{
    if (m_passthrough == on)
        return;
    m_passthrough = on;
    // Click-through only makes sense floating above other windows.
    if (on && !m_pinned) {
        m_pinned = true;
        emit pinnedChanged();
    }
    if (m_win)
        m_win->setFlag(Qt::WindowTransparentForInput, on); // empty wl input region
    if (m_layerShell)
        applyLayer();
    emit passthroughChanged();
}

void PreviewController::applyLayer()
{
#ifdef HAVE_LAYERSHELL
    if (!m_win)
        return;
    if (auto *ls = LayerShellQt::Window::get(m_win)) {
        using LW = LayerShellQt::Window;
        // Pinned → overlay (above panels & fullscreen). Unpinned → top (still
        // above normal windows, but yields to fullscreen). Click-through drops
        // keyboard grab so it can't hold focus while transparent to input.
        ls->setLayer(m_pinned ? LW::LayerOverlay : LW::LayerTop);
        ls->setKeyboardInteractivity(m_passthrough ? LW::KeyboardInteractivityNone
                                                   : LW::KeyboardInteractivityOnDemand);
        // Layer-shell properties reach the compositor with the next surface
        // commit; a static preview renders no new frames, so force one.
        m_win->requestUpdate();
    }
#endif
}

void PreviewController::applyWindowFlags()
{
    if (!m_win)
        return;
    m_win->setFlag(Qt::WindowStaysOnTopHint, m_pinned);
}

void PreviewController::closeWindow()
{
    if (m_win)
        m_win->close();
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
    // qtwayland turns the mask into the wl_surface input region (and the
    // TransparentForInput flag overrides it with an empty one in passthrough).
    m_win->setMask(QRegion(x, y, w, h));
}

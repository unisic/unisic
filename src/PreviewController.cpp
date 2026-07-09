#include "PreviewController.h"
#include <QQuickWindow>
#include <QScreen>

#ifdef HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
#include <QMargins>
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
            // Float in the top-right by default; anchoring two edges + a desired
            // size makes a fixed-size box the user can nudge via margins.
            ls->setAnchors(LW::Anchors(LW::AnchorTop | LW::AnchorRight));
            ls->setDesiredSize(m_win->size());
            ls->setMargins(QMargins(0, m_marginTop, m_marginRight, 0));
            // OnDemand: the window takes keyboard focus when clicked but doesn't
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
        m_win->requestUpdate(); // flush with a commit (see moveBy)
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

void PreviewController::moveBy(int dx, int dy)
{
#ifdef HAVE_LAYERSHELL
    if (!m_win || !m_layerShell)
        return;
    // Called ONCE per drag, on release. Applying it per-move-event diverged:
    // pointer events arrive faster than the compositor repositions the surface,
    // so the same correction stacked up, the margins exploded, the surface left
    // the output and KWin DISMISSED it (which our visibleChanged cleanup then
    // turned into "the preview closed itself").
    //
    // Anchored top-right: dragging right shrinks the right margin, dragging down
    // grows the top margin. Clamp to the output so the surface always stays
    // placeable — an unplaceable layer surface is closed by the compositor.
    const QRect scr = m_win->screen() ? m_win->screen()->geometry() : QRect(0, 0, 1920, 1080);
    const int maxRight = qMax(0, scr.width() - m_win->width());
    const int maxTop = qMax(0, scr.height() - m_win->height());
    m_marginRight = qBound(0, m_marginRight - dx, maxRight);
    m_marginTop = qBound(0, m_marginTop + dy, maxTop);
    if (auto *ls = LayerShellQt::Window::get(m_win))
        ls->setMargins(QMargins(0, m_marginTop, m_marginRight, 0));
    // Layer-shell properties reach the compositor with the next surface commit;
    // a static preview renders no new frames, so force one or the move is inert.
    m_win->requestUpdate();
#else
    Q_UNUSED(dx) Q_UNUSED(dy)
#endif
}

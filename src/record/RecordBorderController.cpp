#include "RecordBorderController.h"
#include <QQuickWindow>
#include <QRegion>

RecordBorderController::RecordBorderController(QObject *parent) : QObject(parent) {}

void RecordBorderController::setWindow(QQuickWindow *win)
{
    m_win = win;
}

void RecordBorderController::setInputRect(int x, int y, int w, int h)
{
    if (!m_win)
        return;
    // setMask sets the wl_surface input region (device-independent px, window
    // coords) — non-empty over the badge, empty everywhere else so clicks pass
    // through to the app being recorded.
    //
    // "No input at all" must NOT be setMask(QRegion()): an empty QRegion CLEARS
    // the mask, and a cleared input region means the WHOLE surface accepts
    // input — the fullscreen frame swallowed every click during the
    // pre-recording countdown (badge hidden) and whenever the region left the
    // badge no room. A 1×1 region outside the surface keeps a mask installed
    // while leaving every on-screen pixel click-through (the compositor
    // intersects the input region with the surface).
    m_win->setMask((w > 0 && h > 0) ? QRegion(x, y, w, h) : QRegion(-1, -1, 1, 1));
}

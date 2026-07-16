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
    m_win->setMask((w > 0 && h > 0) ? QRegion(x, y, w, h) : QRegion());
}

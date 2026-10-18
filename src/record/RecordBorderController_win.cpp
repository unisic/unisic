// Windows sibling of RecordBorderController.cpp. QWindow::setMask is
// SetWindowRgn here, and a window region clips what the window paints as well
// as where it takes clicks: the masked frame showed its badge and nothing else.
// So the frame keeps its whole surface and is click-through instead
// (WindowTransparentForInput), and a cursor poll lifts that only while the
// pointer is over the badge, where the stop and pause controls need the click.
// A click-through window gets no mouse events of its own to track that with.
#include "RecordBorderController.h"

#include <QCursor>
#include <QQuickWindow>
#include <QTimer>

static void setClickThrough(QQuickWindow *win, bool on)
{
    if (win->flags().testFlag(Qt::WindowTransparentForInput) != on)
        win->setFlag(Qt::WindowTransparentForInput, on);
}

RecordBorderController::RecordBorderController(QObject *parent) : QObject(parent) {}

void RecordBorderController::setWindow(QQuickWindow *win)
{
    m_win = win;
}

void RecordBorderController::setInputRect(int x, int y, int w, int h)
{
    if (!m_win)
        return;
    m_inputRect = (w > 0 && h > 0) ? QRect(x, y, w, h) : QRect();
    if (m_inputRect.isEmpty()) {
        if (m_inputPoll)
            m_inputPoll->stop();
        setClickThrough(m_win, true);
        return;
    }
    if (!m_inputPoll) {
        m_inputPoll = new QTimer(this);
        // A hand reaching for a 22 px button takes far longer than 30 ms to
        // land on it, and a tick is one cursor read and a rect test.
        m_inputPoll->setInterval(30);
        connect(m_inputPoll, &QTimer::timeout, this, [this] {
            if (!m_win) {
                m_inputPoll->stop();
                return;
            }
            setClickThrough(m_win, !m_inputRect.contains(m_win->mapFromGlobal(QCursor::pos())));
        });
    }
    setClickThrough(m_win, !m_inputRect.contains(m_win->mapFromGlobal(QCursor::pos())));
    m_inputPoll->start();
}

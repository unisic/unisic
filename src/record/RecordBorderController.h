#pragma once
#include <QObject>
#include <QPointer>
#include <QRect>

class QQuickWindow;
class QTimer;

// Masks the record-border window's pointer input to just the badge rect, so the
// stop/pause controls painted on the badge are clickable while the rest of the
// fullscreen frame stays click-through (the same setMask() input-region trick
// PreviewController and LayerShellNotifier use). RecordBorder.qml reaches it as
// the context property "recordBorderCtl". Without this the frame is either fully
// click-through (no buttons) or fully input-grabbing (blocks the recorded app).
class RecordBorderController : public QObject
{
    Q_OBJECT
public:
    explicit RecordBorderController(QObject *parent = nullptr);

    // Bind the window after the QML component is created (the controller must
    // already exist as a context property before create() so QML resolves it).
    void setWindow(QQuickWindow *win);

    // Clip pointer input to (x,y,w,h) in window-local logical px; an empty rect
    // (w<=0 || h<=0) makes the whole frame click-through again (e.g. while the
    // pre-recording countdown hides the badge).
    Q_INVOKABLE void setInputRect(int x, int y, int w, int h);

private:
    QPointer<QQuickWindow> m_win;
    // Windows only (RecordBorderController_win.cpp): a window mask there clips
    // painting too, so the badge rect is hit-tested against a cursor poll.
    QRect m_inputRect;
    QTimer *m_inputPoll = nullptr;
};

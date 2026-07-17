#pragma once
#include <QObject>

class QThread;

// Global pointer-button capture via libinput's udev backend, on its own thread.
// libinput hands us button + a CLOCK_MONOTONIC timestamp only (no coordinates) —
// the recorder resolves each click's position from the cursor position at the same
// instant, so the two always agree.
//
// It opens devices with a PLAIN open (no EVIOCGRAB): it observes input, never
// steals it. Compile-guarded on HAVE_LIBINPUT — without it the class is inert
// (start() does nothing, no signal ever fires), so the recorder can own and wire
// one unconditionally and only ever start it when InputPermission == Available.
class ClickCapture : public QObject
{
    Q_OBJECT
public:
    explicit ClickCapture(QObject *parent = nullptr);
    ~ClickCapture() override;

    // Idempotent. Spins up the libinput poll thread; a second call while already
    // running is a no-op. Without libinput support it does nothing.
    void start();
    // Idempotent. Wakes the poll thread through the stop eventfd, joins it, and
    // releases the eventfd. Safe to call when not running.
    void stop();
    bool isRunning() const { return m_running; }

signals:
    // A pointer button changed state. tUsec is CLOCK_MONOTONIC microseconds
    // (libinput's own clock — the same domain as frame pts / cursor timestamps,
    // in ns). button is a Qt::MouseButton int (LeftButton / RightButton /
    // MiddleButton). Emitted from the poll thread, so the connection is queued.
    void buttonEvent(qint64 tUsec, int button, bool pressed);

private:
    bool m_running = false;
#ifdef HAVE_LIBINPUT
    void run();               // poll loop; runs on m_thread
    QThread *m_thread = nullptr;
    int m_stopFd = -1;        // eventfd: a write wakes and stops the poll loop
#endif
};

#pragma once
#include <QObject>

class QThread;

// Global keyboard-key capture via libinput's udev backend, on its own thread —
// the keystroke-badge sibling of ClickCapture (same lifecycle, same guards).
// libinput hands us the raw evdev keycode + a CLOCK_MONOTONIC timestamp;
// KeystrokeOverlayPainter turns codes into badge text.
//
// It opens devices with a PLAIN open (no EVIOCGRAB): it observes input, never
// steals it. Compile-guarded on HAVE_LIBINPUT — without it the class is inert
// (start() does nothing, no signal ever fires), so the recorder can own and
// wire one unconditionally and only ever start it when InputPermission ==
// Available.
class KeyCapture : public QObject
{
    Q_OBJECT
public:
    explicit KeyCapture(QObject *parent = nullptr);
    ~KeyCapture() override;

    // Idempotent. Spins up the libinput poll thread; a second call while already
    // running is a no-op. Without libinput support it does nothing.
    void start();
    // Idempotent. Wakes the poll thread through the stop eventfd, joins it, and
    // releases the eventfd. Safe to call when not running.
    void stop();
    bool isRunning() const { return m_running; }

signals:
    // A keyboard key changed state. tUsec is CLOCK_MONOTONIC microseconds
    // (libinput's clock — same domain as frame pts, in ns). code is the raw
    // evdev keycode (KEY_* from linux/input-event-codes.h). Emitted from the
    // poll thread, so the connection is queued.
    void keyEvent(qint64 tUsec, quint32 code, bool pressed);

private:
    bool m_running = false;
#ifdef HAVE_LIBINPUT
    void run();               // poll loop; runs on m_thread
    QThread *m_thread = nullptr;
    int m_stopFd = -1;        // eventfd: a write wakes and stops the poll loop
#endif
};

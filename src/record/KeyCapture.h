#pragma once
#include <QObject>

class QThread;

// Global keyboard-key capture via libinput's udev backend, on its own thread -
// the keystroke-badge sibling of ClickCapture (same lifecycle, same guards).
// libinput hands us the raw evdev keycode + a CLOCK_MONOTONIC timestamp;
// KeystrokeOverlayPainter turns codes into badge text.
//
// It opens devices with a PLAIN open (no EVIOCGRAB): it observes input, never
// steals it. libinput is a hard build requirement, so the class is always live;
// the recorder owns and wires one unconditionally and only ever starts it when
// InputPermission == Available.
class KeyCapture : public QObject
{
    Q_OBJECT
public:
    explicit KeyCapture(QObject *parent = nullptr);
    ~KeyCapture() override;

    // Idempotent. Spins up the libinput poll thread; a second call while already
    // running is a no-op.
    void start();
    // Idempotent. Wakes the poll thread through the stop eventfd, joins it, and
    // releases the eventfd. Safe to call when not running.
    void stop();
    bool isRunning() const { return m_running; }

signals:
    // A keyboard key changed state. tUsec is CLOCK_MONOTONIC microseconds
    // (libinput's clock - same domain as frame pts, in ns). code is the raw
    // evdev keycode (KEY_* from linux/input-event-codes.h). Emitted from the
    // poll thread, so the connection is queued.
    void keyEvent(qint64 tUsec, quint32 code, bool pressed);

private:
    bool m_running = false;
    void run();               // poll loop; runs on m_thread
    QThread *m_thread = nullptr;
    int m_stopFd = -1;        // eventfd: a write wakes and stops the poll loop
};

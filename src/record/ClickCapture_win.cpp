// Windows build of ClickCapture: a WH_MOUSE_LL hook on its own thread stands in
// for libinput. A low-level hook observes input and never steals it (the proc
// always passes the event on), and it needs no group membership, so
// InputPermission_win.cpp answers Available. Windows calls the hook on the
// thread that installed it, from inside that thread's message pump, so run()
// pumps messages until stop() signals the event in m_stopFd.
#include "ClickCapture.h"

#include <QThread>
#include <ctime>
#include <windows.h>

namespace {
// One hook per capture thread, and the hook proc gets no user pointer: this is
// its only way back to the object that installed it.
thread_local ClickCapture *t_capture = nullptr;

// CLOCK_MONOTONIC, like GifRecorder's monoNowNs() and the grabber's cursor
// samples, so a ripple lands on the frame its click happened in. The hook runs
// synchronously with input delivery, so "now" is the click's own instant.
qint64 monoNowUsec()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000LL + ts.tv_nsec / 1000;
}

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && t_capture) {
        int button = 0;
        bool pressed = false;
        switch (wParam) {
        case WM_LBUTTONDOWN: button = Qt::LeftButton;   pressed = true; break;
        case WM_LBUTTONUP:   button = Qt::LeftButton;   break;
        case WM_RBUTTONDOWN: button = Qt::RightButton;  pressed = true; break;
        case WM_RBUTTONUP:   button = Qt::RightButton;  break;
        case WM_MBUTTONDOWN: button = Qt::MiddleButton; pressed = true; break;
        case WM_MBUTTONUP:   button = Qt::MiddleButton; break;
        default: break;   // moves, wheel, X buttons: not a ripple, as on Linux
        }
        if (button)
            emit t_capture->buttonEvent(monoNowUsec(), button, pressed);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

ClickCapture::ClickCapture(QObject *parent) : QObject(parent) {}

ClickCapture::~ClickCapture() { stop(); }

void ClickCapture::start()
{
    if (m_running)
        return;
    const HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev)
        return;   // cannot arm the stop channel - refuse rather than leak a thread
    // Kernel handles fit in 32 bits on 64-bit Windows (documented, for 32/64-bit
    // interop), so the shared header's int slot carries it.
    m_stopFd = HandleToLong(ev);
    m_running = true;
    m_thread = QThread::create([this] { run(); });
    m_thread->setObjectName(QStringLiteral("ClickCapture"));
    // Every mouse event on the desktop waits for this hook to return, so the
    // thread must not queue behind the encoder. It sleeps between events.
    m_thread->start(QThread::HighestPriority);
}

void ClickCapture::stop()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_stopFd != -1)
        SetEvent(LongToHandle(m_stopFd));   // wake the message wait
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    if (m_stopFd != -1) {
        CloseHandle(LongToHandle(m_stopFd));
        m_stopFd = -1;
    }
}

void ClickCapture::run()
{
    const HANDLE stopEvent = LongToHandle(m_stopFd);
    t_capture = this;
    const HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
    if (!hook) {
        t_capture = nullptr;
        return;
    }
    for (;;) {
        // MWMO_INPUTAVAILABLE: wake for anything already queued too, not only
        // for what arrived since the last peek.
        const DWORD rc = MsgWaitForMultipleObjectsEx(1, &stopEvent, INFINITE,
                                                     QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (rc != WAIT_OBJECT_0 + 1)
            break;   // stop() signalled us, or the wait failed - bail rather than spin
        // Peeking delivers the pending hook calls; drain whatever was posted.
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    UnhookWindowsHookEx(hook);
    t_capture = nullptr;
}

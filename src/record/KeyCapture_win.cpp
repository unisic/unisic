// Windows build of KeyCapture: a WH_KEYBOARD_LL hook on its own thread stands in
// for libinput, with the same lifecycle as ClickCapture_win.cpp. The proc always
// passes the event on, so it observes keys and never steals them. Windows codes
// are translated into the evdev codes KeystrokeOverlayPainter names, and Windows
// autorepeat is dropped: libinput reports one press and one release per stroke,
// and the painter counts a repeated press as "x2", "x3".
#include "KeyCapture.h"

#include <QThread>
#include <array>
#include <ctime>
#include <linux/input-event-codes.h>
#include <windows.h>

namespace {
thread_local KeyCapture *t_capture = nullptr;
// Keys currently down, by evdev code (every mapped code is below 256), so the
// autorepeat presses of a held key are recognised and skipped.
thread_local std::array<bool, 256> t_held{};

qint64 monoNowUsec()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000LL + ts.tv_nsec / 1000;
}

// The evdev code of one hooked key event, or 0 for a key the badges never name.
// Set 1 scan codes equal the evdev codes up to F12 (88); the E0-prefixed keys
// are a table. Keys whose hook scan code is ambiguous go by virtual key:
// Pause and NumLock share 0x45, and PrtSc changes code under Alt.
quint32 evdevCode(const KBDLLHOOKSTRUCT &kb)
{
    // Scan codes above 0xFF are the ones Windows synthesises itself: the left
    // Ctrl that AltGr layouts send ahead of right Alt (0x21D) and the Shift
    // that NumLock numpad navigation toggles. A keyboard never sends them, and
    // libinput would not report them.
    if (kb.scanCode > 0xFF)
        return 0;
    switch (kb.vkCode) {
    case VK_PAUSE: case VK_CANCEL: return KEY_PAUSE;
    case VK_NUMLOCK:  return KEY_NUMLOCK;
    case VK_SNAPSHOT: return KEY_SYSRQ;
    case VK_LSHIFT:   return KEY_LEFTSHIFT;
    case VK_RSHIFT:   return KEY_RIGHTSHIFT;
    case VK_LCONTROL: return KEY_LEFTCTRL;
    case VK_RCONTROL: return KEY_RIGHTCTRL;
    case VK_LMENU:    return KEY_LEFTALT;
    case VK_RMENU:    return KEY_RIGHTALT;
    case VK_LWIN:     return KEY_LEFTMETA;
    case VK_RWIN:     return KEY_RIGHTMETA;
    default: break;
    }
    UINT scan = kb.scanCode;
    bool extended = kb.flags & LLKHF_EXTENDED;
    if (scan == 0) {
        // Injected without a scan code (keybd_event, SendInput by VK): ask the
        // active layout which key that virtual key sits on.
        const UINT sc = MapVirtualKeyW(kb.vkCode, MAPVK_VK_TO_VSC_EX);
        scan = sc & 0xFF;
        extended = (sc & 0xFF00) == 0xE000;
    }
    if (!extended)
        return scan >= 1 && scan <= KEY_F12 ? scan : 0;
    switch (scan) {
    case 0x1C: return KEY_KPENTER;
    case 0x1D: return KEY_RIGHTCTRL;
    case 0x35: return KEY_KPSLASH;
    case 0x37: return KEY_SYSRQ;
    case 0x38: return KEY_RIGHTALT;
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PAGEUP;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PAGEDOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x5B: return KEY_LEFTMETA;
    case 0x5C: return KEY_RIGHTMETA;
    case 0x5D: return KEY_COMPOSE;
    default:   return 0;   // media and browser keys: not a badge's business
    }
}

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && t_capture) {
        const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        const bool pressed = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const quint32 code = evdevCode(*kb);
        if (code && t_held[code] != pressed) {
            t_held[code] = pressed;
            emit t_capture->keyEvent(monoNowUsec(), code, pressed);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

KeyCapture::KeyCapture(QObject *parent) : QObject(parent) {}

KeyCapture::~KeyCapture() { stop(); }

void KeyCapture::start()
{
    if (m_running)
        return;
    const HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ev)
        return;   // cannot arm the stop channel - refuse rather than leak a thread
    m_stopFd = HandleToLong(ev);   // see ClickCapture_win.cpp
    m_running = true;
    m_thread = QThread::create([this] { run(); });
    m_thread->setObjectName(QStringLiteral("KeyCapture"));
    // Every key on the desktop waits for this hook to return.
    m_thread->start(QThread::HighestPriority);
}

void KeyCapture::stop()
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

void KeyCapture::run()
{
    const HANDLE stopEvent = LongToHandle(m_stopFd);
    t_capture = this;
    t_held.fill(false);
    const HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!hook) {
        t_capture = nullptr;
        return;
    }
    for (;;) {
        const DWORD rc = MsgWaitForMultipleObjectsEx(1, &stopEvent, INFINITE,
                                                     QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (rc != WAIT_OBJECT_0 + 1)
            break;   // stop() signalled us, or the wait failed - bail rather than spin
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    UnhookWindowsHookEx(hook);
    t_capture = nullptr;
}

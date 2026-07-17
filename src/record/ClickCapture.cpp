#include "ClickCapture.h"

#ifdef HAVE_LIBINPUT
#include <QThread>
#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace {
int clickOpen(const char *path, int flags, void *)
{
    const int fd = ::open(path, flags);   // plain open, NO grab
    return fd < 0 ? -errno : fd;
}
void clickClose(int fd, void *) { ::close(fd); }
const libinput_interface kClickInterface = {clickOpen, clickClose};
} // namespace
#endif

ClickCapture::ClickCapture(QObject *parent) : QObject(parent) {}

ClickCapture::~ClickCapture() { stop(); }

void ClickCapture::start()
{
#ifdef HAVE_LIBINPUT
    if (m_running)
        return;
    m_stopFd = ::eventfd(0, EFD_CLOEXEC);
    if (m_stopFd < 0)
        return;   // cannot arm the stop channel — refuse rather than leak a thread
    m_running = true;
    m_thread = QThread::create([this] { run(); });
    m_thread->setObjectName(QStringLiteral("ClickCapture"));
    m_thread->start();
#endif
}

void ClickCapture::stop()
{
#ifdef HAVE_LIBINPUT
    if (!m_running)
        return;
    m_running = false;
    if (m_stopFd >= 0) {
        const uint64_t one = 1;
        (void)::write(m_stopFd, &one, sizeof(one));   // wake poll()
    }
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    if (m_stopFd >= 0) {
        ::close(m_stopFd);
        m_stopFd = -1;
    }
#endif
}

#ifdef HAVE_LIBINPUT
void ClickCapture::run()
{
    // The libinput context lives entirely on THIS thread; every libinput call is
    // made here, so no locking is needed around it.
    udev *ud = udev_new();
    if (!ud)
        return;
    libinput *li = libinput_udev_create_context(&kClickInterface, nullptr, ud);
    if (!li) {
        udev_unref(ud);
        return;
    }
    if (libinput_udev_assign_seat(li, "seat0") != 0) {
        libinput_unref(li);
        udev_unref(ud);
        return;
    }

    pollfd fds[2];
    fds[0] = {libinput_get_fd(li), POLLIN, 0};
    fds[1] = {m_stopFd, POLLIN, 0};

    for (;;) {
        // Drain first: the initial dispatch queues DEVICE_ADDED for the seat's
        // devices, and consuming keeps the fd from staying perpetually readable.
        libinput_dispatch(li);
        for (libinput_event *ev = libinput_get_event(li); ev; ev = libinput_get_event(li)) {
            if (libinput_event_get_type(ev) == LIBINPUT_EVENT_POINTER_BUTTON) {
                libinput_event_pointer *pev = libinput_event_get_pointer_event(ev);
                const uint32_t button = libinput_event_pointer_get_button(pev);
                const bool pressed = libinput_event_pointer_get_button_state(pev)
                                     == LIBINPUT_BUTTON_STATE_PRESSED;
                const uint64_t tUsec = libinput_event_pointer_get_time_usec(pev);
                int qtButton = 0;
                switch (button) {
                case BTN_LEFT:   qtButton = int(Qt::LeftButton);   break;
                case BTN_RIGHT:  qtButton = int(Qt::RightButton);  break;
                case BTN_MIDDLE: qtButton = int(Qt::MiddleButton); break;
                default:         qtButton = 0;                     break;   // ignore extras
                }
                if (qtButton != 0)
                    emit buttonEvent(qint64(tUsec), qtButton, pressed);
            }
            libinput_event_destroy(ev);   // everything else: dispatched and dropped
        }

        const int rc = ::poll(fds, 2, -1);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            break;   // unexpected poll failure — bail rather than spin
        }
        if (fds[1].revents & POLLIN)
            break;   // stop() signalled us
        // Otherwise the libinput fd is readable: loop back to dispatch + drain.
    }

    libinput_unref(li);
    udev_unref(ud);
}
#endif

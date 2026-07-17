#include "InputPermission.h"

#ifdef HAVE_LIBINPUT
#include <libinput.h>
#include <libudev.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
// open_restricted is a PLAIN open — no EVIOCGRAB, so we observe input without
// stealing it. close_restricted mirrors it. libinput calls these on the caller's
// thread during the synchronous probe.
int probeOpen(const char *path, int flags, void *)
{
    const int fd = ::open(path, flags);
    return fd < 0 ? -errno : fd;
}
void probeClose(int fd, void *) { ::close(fd); }
const libinput_interface kProbeInterface = {probeOpen, probeClose};
} // namespace
#endif

InputPermission::Status InputPermission::probe()
{
#ifndef HAVE_LIBINPUT
    return NotBuilt;
#else
    udev *ud = udev_new();
    if (!ud)
        return NoPermission;
    libinput *li = libinput_udev_create_context(&kProbeInterface, nullptr, ud);
    if (!li) {
        udev_unref(ud);
        return NoPermission;
    }
    // assign_seat != 0 means udev/seat setup failed outright. A user lacking read
    // access to /dev/input/* still gets a context and seat, but every device
    // open_restricted fails, so no DEVICE_ADDED events land → pointers stays 0.
    if (libinput_udev_assign_seat(li, "seat0") != 0) {
        libinput_unref(li);
        udev_unref(ud);
        return NoPermission;
    }
    // The initial dispatch enumerates the seat's current devices as DEVICE_ADDED
    // events; count the pointer-capable ones, then drop the burst.
    int pointers = 0;
    libinput_dispatch(li);
    while (libinput_event *ev = libinput_get_event(li)) {
        if (libinput_event_get_type(ev) == LIBINPUT_EVENT_DEVICE_ADDED) {
            libinput_device *dev = libinput_event_get_device(ev);
            if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER))
                ++pointers;
        }
        libinput_event_destroy(ev);
    }
    libinput_unref(li);
    udev_unref(ud);
    return pointers > 0 ? Available : NoPermission;
#endif
}

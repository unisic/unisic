// Windows stand-in for <wayland-client.h>: just the calls LayerShellNotifier.cpp
// makes. There is no Wayland display, so wl_display_connect() returns nullptr
// and the caller takes its existing "no compositor" path; nothing past that
// check ever runs.
#pragma once

#include <cstdint>

struct wl_display;
struct wl_registry;
struct wl_interface;

struct wl_registry_listener {
    void (*global)(void *data, wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version);
    void (*global_remove)(void *data, wl_registry *registry, uint32_t name);
};

inline wl_display *wl_display_connect(const char *) { return nullptr; }
inline wl_registry *wl_display_get_registry(wl_display *) { return nullptr; }
inline int wl_registry_add_listener(wl_registry *, const wl_registry_listener *, void *) { return -1; }
inline int wl_display_roundtrip(wl_display *) { return -1; }
inline void wl_registry_destroy(wl_registry *) {}
inline void wl_display_disconnect(wl_display *) {}

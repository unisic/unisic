// The platform half of LayerShellNotifier: what makes the card window stay on
// top, and where it goes. Here, on Linux, the compositor does both through
// wlr-layer-shell. CardSurface_win.cpp is the Windows sibling.
#include "LayerShellNotifier.h"

#include <QMargins>
#include <QQuickWindow>
#include <QString>

#include <LayerShellQt/window.h>
#include <wayland-client.h>
#include <cstring>

bool LayerShellNotifier::compositorSupportsLayerShell()
{
    // A tiny throwaway connection to the same compositor (WAYLAND_DISPLAY),
    // isolated from Qt's own wl_display so the registry roundtrip can't disturb
    // Qt's event dispatching. Enumerate globals; look for the layer-shell one.
    struct wl_display *display = wl_display_connect(nullptr);
    if (!display)
        return false;

    bool found = false;
    struct wl_registry *registry = wl_display_get_registry(display);
    static const wl_registry_listener listener = {
        [](void *data, wl_registry *, uint32_t, const char *iface, uint32_t) {
            if (std::strcmp(iface, "zwlr_layer_shell_v1") == 0)
                *static_cast<bool *>(data) = true;
        },
        [](void *, wl_registry *, uint32_t) {},
    };
    wl_registry_add_listener(registry, &listener, &found);
    wl_display_roundtrip(display); // one roundtrip delivers the global advertisements
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return found;
}

bool LayerShellNotifier::placesCardItself()
{
    // A Wayland client cannot place its own window, and on X11 an in-process
    // transparent toplevel renders black: the card needs layer-shell or the
    // override-redirect helper.
    return false;
}

void LayerShellNotifier::place(QQuickWindow *win, QScreen *, const QString &position, int edge)
{
    // Make it a layer-shell overlay surface BEFORE it is shown (get() attaches
    // the integration; configuration is committed on the first show()).
    if (auto *ls = LayerShellQt::Window::get(win)) {
        using LW = LayerShellQt::Window;
        ls->setLayer(LW::LayerOverlay);
        ls->setScope(QStringLiteral("unisic-notification"));
        ls->setExclusiveZone(0); // don't reserve space / push other windows
        ls->setKeyboardInteractivity(LW::KeyboardInteractivityNone); // never steal keyboard

        const bool top = position.startsWith(QLatin1String("top"));
        const bool left = position.endsWith(QLatin1String("left"));
        const bool right = position.endsWith(QLatin1String("right"));
        LW::Anchors anchors = top ? LW::AnchorTop : LW::AnchorBottom;
        int mL = 0, mT = 0, mR = 0, mB = 0;
        if (left)  { anchors |= LW::AnchorLeft;  mL = edge; }
        if (right) { anchors |= LW::AnchorRight; mR = edge; }
        if (top) mT = edge; else mB = edge;
        ls->setAnchors(anchors);
        ls->setMargins(QMargins(mL, mT, mR, mB));
    }
}

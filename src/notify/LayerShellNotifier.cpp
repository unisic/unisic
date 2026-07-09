#include "LayerShellNotifier.h"
#include "CaptureNotification.h"
#include "AppContext.h"
#include "Settings.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QMargins>
#include <QDebug>

#include <LayerShellQt/window.h>
#include <wayland-client.h>
#include <cstring>

LayerShellNotifier::LayerShellNotifier(AppContext *app, QObject *parent)
    : QObject(parent), m_app(app)
{
}

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

void LayerShellNotifier::show(CaptureNotification *n)
{
    if (!n)
        return;
    QQmlEngine *engine = m_app->qmlEngine();
    if (!engine) {
        n->deleteLater();
        return;
    }
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/NotificationPopup.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        n->deleteLater();
        return;
    }

    // Card + a small transparent pad so the drop shadow isn't clipped.
    const int cardW = 400, cardH = 150, pad = 16, edge = 8;

    auto *ctx = new QQmlContext(engine->rootContext(), this);
    ctx->setContextProperty(QStringLiteral("notif"), n);
    ctx->setContextProperty(QStringLiteral("popupX"), pad);
    ctx->setContextProperty(QStringLiteral("popupY"), pad);
    ctx->setContextProperty(QStringLiteral("popupW"), cardW);
    ctx->setContextProperty(QStringLiteral("popupH"), cardH);

    QObject *obj = component.create(ctx);
    auto *win = qobject_cast<QQuickWindow *>(obj);
    if (!win) {
        delete obj;
        delete ctx;
        n->deleteLater();
        return;
    }
    ctx->setParent(win);
    n->setParent(win); // window owns the backing object for its lifetime

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen)
        win->setScreen(screen);
    win->resize(cardW + 2 * pad, cardH + 2 * pad);

    // Make it a layer-shell overlay surface BEFORE it is shown (get() attaches
    // the integration; configuration is committed on the first show()).
    if (auto *ls = LayerShellQt::Window::get(win)) {
        using LW = LayerShellQt::Window;
        ls->setLayer(LW::LayerOverlay);
        ls->setScope(QStringLiteral("unisic-notification"));
        ls->setExclusiveZone(0); // don't reserve space / push other windows
        ls->setKeyboardInteractivity(LW::KeyboardInteractivityNone); // never steal keyboard

        const QString posName = m_app->settings()->capturePopupPosition();
        const bool top = posName.startsWith(QLatin1String("top"));
        const bool left = posName.endsWith(QLatin1String("left"));
        const bool right = posName.endsWith(QLatin1String("right"));
        LW::Anchors anchors = top ? LW::AnchorTop : LW::AnchorBottom;
        int mL = 0, mT = 0, mR = 0, mB = 0;
        if (left)  { anchors |= LW::AnchorLeft;  mL = edge; }
        if (right) { anchors |= LW::AnchorRight; mR = edge; }
        if (top) mT = edge; else mB = edge;
        ls->setAnchors(anchors);
        ls->setMargins(QMargins(mL, mT, mR, mB));
    }

    QObject::connect(n, &CaptureNotification::closeRequested, win, &QQuickWindow::close);
    QObject::connect(win, &QQuickWindow::visibleChanged, win, [win](bool visible) {
        if (!visible)
            win->deleteLater(); // takes its child ctx + CaptureNotification with it
    });

    win->show();
    // Route pointer input only to the card; the transparent shadow pad around it
    // stays click-through so it doesn't eat clicks on windows in that corner.
    win->setMask(QRegion(pad, pad, cardW, cardH));
}

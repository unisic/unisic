#include "OverlayController.h"
#include "AppContext.h"
#include "capture/CaptureManager.h"
#include "overlay/WindowRects.h"
#include "editor/AnnotationCanvas.h"
#include <QGuiApplication>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QPointer>
#include <QDebug>
#include <memory>
#ifdef HAVE_LAYERSHELL
#include <LayerShellQt/window.h>
#include <QMargins>
#endif

OverlayController::OverlayController(AppContext *app, QObject *parent)
    : QObject(parent), m_app(app)
{
    // m_screens caches raw QScreen* across the async freeze round-trip;
    // a monitor unplug would leave dangling pointers, so just cancel.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        if (active())
            cancel();
    });
}

void OverlayController::pickAnnotatedImage(ImageCallback cb)
{
    if (active()) return;
    m_imageCb = std::move(cb);
    m_regionCb = nullptr;
    begin(true);
}

void OverlayController::pickRegion(RegionCallback cb)
{
    if (active()) return;
    m_regionCb = std::move(cb);
    m_imageCb = nullptr;
    begin(false);
}

void OverlayController::begin(bool annotationTools)
{
    m_starting = true;
    m_annotationTools = annotationTools;
    m_screens = QGuiApplication::screens().toVector();
    m_frozen.clear();
    m_frozen.resize(m_screens.size());

    // Stale-callback guard: a cancel()+retrigger must not let the previous
    // freeze capture overwrite state and spawn a second set of windows.
    const int gen = ++m_generation;

    // System first: ask the compositor for the REAL window frames (async,
    // KDE-only, empty elsewhere). Whichever finishes last — this query or the
    // freeze capture — pushes the rects into the canvases via
    // applyWindowRects(); querying BEFORE our own overlay windows exist also
    // keeps them out of the list.
    m_windowRects.clear();
    WindowRects::query(this, [this, gen](const QVector<QRect> &rects) {
        if (gen != m_generation)
            return;
        m_windowRects = rects;
        applyWindowRects();
    });
    m_app->captureManager()->captureAllScreens(m_screens,
        [this, gen](const QVector<QImage> &imgs, const QString &err) {
            if (gen != m_generation)
                return;
            if (!err.isEmpty()) {
                qWarning() << "Freeze capture failed:" << err;
                if (err != QLatin1String("cancelled"))
                    m_app->showToast(m_app->captureErrorGuidance(err), true);
                cancel();
                return;
            }
            m_frozen = imgs;
            createWindows();
        });
}

void OverlayController::createWindows()
{
    for (int i = 0; i < m_frozen.size(); ++i) {
        if (m_frozen[i].isNull()) {
            const QString name = (i < m_screens.size() && m_screens[i])
                                     ? m_screens[i]->name()
                                     : QString::number(i);
            qWarning() << "Freeze capture returned a null image for screen" << name;
            m_app->showToast(tr("Screen capture failed for screen %1").arg(name));
            cancel();
            return;
        }
    }

    QQmlEngine *engine = m_app->qmlEngine();
    QQmlComponent component(engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/OverlayWindow.qml")));
    if (component.isError()) {
        qWarning() << component.errorString();
        cancel();
        return;
    }

    for (int i = 0; i < m_screens.size(); ++i) {
        QScreen *screen = m_screens[i];
        // Parented to the window below — a context parented to this
        // app-lifetime controller would leak one per screen per invocation.
        auto *ctx = new QQmlContext(engine->rootContext(), this);
        ctx->setContextProperty(QStringLiteral("overlayController"), this);
        ctx->setContextProperty(QStringLiteral("annotationToolsEnabled"), m_annotationTools);

        QObject *obj = component.create(ctx);
        auto *win = qobject_cast<QQuickWindow *>(obj);
        if (!win) {
            delete obj;
            delete ctx;
            continue;
        }
        ctx->setParent(win);
        win->setScreen(screen);
        win->setGeometry(screen->geometry());

        if (auto *canvas = win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas"))) {
            QImage img = m_frozen[i];
            canvas->setImage(img);
        }

        bool shown = false;
#ifdef HAVE_LAYERSHELL
        // A fullscreen toplevel can't sit above a fullscreen application on
        // Wayland, so region/window selection over a fullscreen game/video
        // failed. A layer-shell OVERLAY surface can — with exclusive keyboard so
        // Escape/Enter and the text tool still receive input.
        if (m_app->layerShellAvailable()) {
            win->resize(screen->geometry().size());
            if (auto *ls = LayerShellQt::Window::get(win)) {
                using LW = LayerShellQt::Window;
                ls->setLayer(LW::LayerOverlay);
                ls->setScope(QStringLiteral("unisic-overlay"));
                ls->setExclusiveZone(-1);
                ls->setKeyboardInteractivity(LW::KeyboardInteractivityExclusive);
                ls->setAnchors(LW::Anchors(LW::AnchorTop | LW::AnchorBottom
                                           | LW::AnchorLeft | LW::AnchorRight));
                ls->setMargins(QMargins(0, 0, 0, 0));
            }
            win->show();
            shown = true;
        }
#endif
        if (!shown)
            win->showFullScreen();
        m_windows.append(win);
    }
    if (!m_windows.isEmpty()) {
        m_starting = false;
        applyWindowRects(); // no-op if the compositor hasn't answered yet
        m_windows.first()->requestActivate();
        // Each canvas took its own copy — don't pin a second full-resolution
        // image per monitor for the overlay's whole lifetime.
        m_frozen.clear();
    } else {
        cancel();
    }
}

void OverlayController::confirmFromWindow(QQuickWindow *win)
{
    auto *canvas = win ? win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas")) : nullptr;
    if (!canvas || !canvas->hasSelection())
        return;

    // Object pick: a confirm arriving while the cutout is still being computed
    // (or a nudge re-run is pending) must WAIT for the mask — otherwise the
    // raw rectangle, background included, would be exported with no warning.
    // One armed wait per window; repeated confirms while armed are dropped.
    if (canvas->tool() == AnnotationCanvas::ObjectPick && canvas->segmenting()) {
        if (win->property("confirmArmed").toBool())
            return;
        win->setProperty("confirmArmed", true);
        QPointer<QQuickWindow> wp(win);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(canvas, &AnnotationCanvas::segmentingChanged, this,
                        [this, wp, canvas, conn] {
            if (canvas->segmenting())
                return; // another run started (nudge burst) — keep waiting
            QObject::disconnect(*conn);
            // m_windows guard: a cancelled/replaced session must not let a
            // stale armed confirm fire into the new session's callback.
            if (wp && m_windows.contains(wp.data())) {
                wp->setProperty("confirmArmed", false);
                // Only auto-fire when the awaited cutout actually exists; a
                // failed segmentation or a tool switch must not silently
                // export the raw rectangle the user never saw.
                if (canvas->tool() == AnnotationCanvas::ObjectPick && canvas->hasObjectMask())
                    confirmFromWindow(wp);
            }
        });
        return;
    }

    const int idx = m_windows.indexOf(win);
    QScreen *screen = (idx >= 0 && idx < m_screens.size()) ? m_screens[idx] : nullptr;

    if (m_imageCb) {
        const QImage result = canvas->renderedSelection();
        auto cb = std::move(m_imageCb);
        closeAll();
        cb(result);
    } else if (m_regionCb) {
        // Canvas works in physical image pixels already (frozen image is native res).
        const QRect phys = canvas->selectionRect().toAlignedRect();
        auto cb = std::move(m_regionCb);
        closeAll();
        cb(phys, screen);
    }
}

void OverlayController::applyWindowRects()
{
    if (m_windowRects.isEmpty() || m_windows.isEmpty())
        return;
    // Global LOGICAL rects -> each screen's IMAGE pixels: shift by the
    // screen's origin, scale by its DPR (the frozen capture is physical px).
    for (QQuickWindow *win : std::as_const(m_windows)) {
        QScreen *screen = win->screen();
        auto *canvas = win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas"));
        if (!screen || !canvas)
            continue;
        const QRect sg = screen->geometry();
        const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        QVector<QRect> local;
        local.reserve(m_windowRects.size());
        for (const QRect &r : std::as_const(m_windowRects)) {
            const QRect on = r.intersected(sg);
            if (on.width() < 8 || on.height() < 8)
                continue;
            local.append(QRect(int((on.x() - sg.x()) * dpr), int((on.y() - sg.y()) * dpr),
                               int(on.width() * dpr), int(on.height() * dpr)));
        }
        canvas->setWindowCandidates(local);
    }
}

void OverlayController::cancel()
{
    ++m_generation;
    closeAll();
    m_imageCb = nullptr;
    m_regionCb = nullptr;
}

void OverlayController::closeAll()
{
    // The QML text editor's onVisibleChanged never fires on window teardown, so the
    // flag would latch true forever (killing hover-based requestActivate) if the
    // session ends with the in-place text box still open. Clear it here — closeAll()
    // is the single choke point for every session teardown.
    setTextEditing(false);
    m_starting = false;
    const auto windows = m_windows;
    m_windows.clear();
    for (QQuickWindow *w : windows) {
        w->close();
        w->deleteLater();
    }
    m_frozen.clear();
}

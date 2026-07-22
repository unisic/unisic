#include "OverlayController.h"
#include "AppContext.h"
#include "capture/CaptureManager.h"
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

void OverlayController::pickAnnotatedImage(ImageCallback cb, int initialTool)
{
    if (active()) return;
    m_imageCb = std::move(cb);
    m_regionCb = nullptr;
    m_initialTool = initialTool;
    begin(true);
}

void OverlayController::pickRegion(RegionCallback cb)
{
    if (active()) return;
    m_regionCb = std::move(cb);
    m_imageCb = nullptr;
    m_initialTool = AnnotationCanvas::None;
    begin(false);
}

void OverlayController::begin(bool annotationTools)
{
    m_starting = true;
    m_annotationTools = annotationTools;
    m_copyRequested = false; // never inherit a Ctrl+C from a previous session
    m_screens = QGuiApplication::screens().toVector();
    m_frozen.clear();
    m_frozen.resize(m_screens.size());

    // Stale-callback guard: a cancel()+retrigger must not let the previous
    // freeze capture overwrite state and spawn a second set of windows.
    const int gen = ++m_generation;
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
            if (m_initialTool != AnnotationCanvas::None) {
                // Dedicated full-screen tool modes start with the complete
                // frozen output selected, so the first drag measures/draws
                // immediately and Enter can export without a crop gesture.
                canvas->selectAll();
                canvas->setTool(m_initialTool);
            } else if (m_annotationTools && m_app->settings()->rememberRegion()) {
                // Remember-region preference: open with the last confirmed
                // region already selected on its screen — adjust or just
                // confirm. Stored as "<screen>|<x>,<y>,<w>,<h>" in LOGICAL px
                // of that screen (same format recaptureLastRegion parses);
                // the frozen image can be scaled, so rescale into image px.
                const QString stored = m_app->settings()->lastCaptureRegion();
                const int bar = stored.indexOf(QLatin1Char('|'));
                if (bar > 0 && stored.left(bar) == screen->name()) {
                    const QStringList parts = stored.mid(bar + 1).split(QLatin1Char(','));
                    bool ok = parts.size() == 4;
                    int v[4] = {};
                    for (int k = 0; k < 4 && ok; ++k)
                        v[k] = parts[k].toInt(&ok);
                    if (ok && v[2] > 2 && v[3] > 2 && screen->geometry().width() > 0) {
                        const double s = double(img.width()) / screen->geometry().width();
                        canvas->setSelectionRect(QRectF(v[0] * s, v[1] * s,
                                                        v[2] * s, v[3] * s));
                    }
                }
            }
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
        m_windowScreens.append(screen);
    }
    if (!m_windows.isEmpty()) {
        m_starting = false;
        m_windows.first()->requestActivate();
        // Each canvas took its own copy — don't pin a second full-resolution
        // image per monitor for the overlay's whole lifetime.
        m_frozen.clear();
    } else {
        cancel();
    }
}

// Spectacle CaptureWindow parity: Ctrl+C on the overlay accepts the selection
// and forces a clipboard copy (even when auto-copy is off). The flag is
// consumed by the capture callback via takeCopyRequested().
void OverlayController::confirmAndCopy(QQuickWindow *win)
{
    // Same no-selection guard as confirmFromWindow — a bare Ctrl+C must not
    // leave the flag armed for an unrelated Enter-confirm later in the session.
    auto *canvas = win ? win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas")) : nullptr;
    if (!canvas || !canvas->hasSelection())
        return;
    m_copyRequested = true;
    confirmFromWindow(win);
}

void OverlayController::confirmFromWindow(QQuickWindow *win)
{
    auto *canvas = win ? win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas")) : nullptr;
    if (!canvas || !canvas->hasSelection())
        return;

    const int idx = m_windows.indexOf(win);
    QScreen *screen = (idx >= 0 && idx < m_windowScreens.size()) ? m_windowScreens[idx] : nullptr;

    if (m_imageCb) {
        const QImage result = canvas->renderedSelection();
        // Remember for re-capture: frozen-image px -> LOGICAL screen px (the
        // portal freeze can be uniformly scaled; per-screen factor).
        const QRectF sel = canvas->selectionRect();
        const QSize imgSize = canvas->image().size();
        const double ls = (screen && !imgSize.isEmpty())
            ? double(screen->geometry().width()) / imgSize.width() : 1.0;
        m_lastRegionLogical = QRectF(sel.x() * ls, sel.y() * ls,
                                     sel.width() * ls, sel.height() * ls).toAlignedRect();
        m_lastRegionScreen = screen ? screen->name() : QString();
        auto cb = std::move(m_imageCb);
        closeAll();
        cb(result);
    } else if (m_regionCb) {
        // The selection is in frozen-IMAGE pixels; the region callback contract
        // is PHYSICAL pixels (screen geometry * DPR). On the KWin path the
        // frozen image is already native res so the two match and this is a
        // no-op, but the portal workspace-crop fallback can hand back a
        // logical / uniformly-scaled image — rescale so the recorder crops the
        // right area instead of a mis-scaled quadrant.
        const QRectF sel = canvas->selectionRect();
        const QSize imgSize = canvas->image().size();
        const QSize physSize = screen
            ? QSize(qRound(screen->geometry().width() * screen->devicePixelRatio()),
                    qRound(screen->geometry().height() * screen->devicePixelRatio()))
            : QSize();
        QRect phys;
        if (screen && !imgSize.isEmpty() && physSize != imgSize) {
            const double sx = double(physSize.width()) / imgSize.width();
            const double sy = double(physSize.height()) / imgSize.height();
            phys = QRectF(sel.x() * sx, sel.y() * sy,
                          sel.width() * sx, sel.height() * sy).toAlignedRect();
        } else {
            phys = sel.toAlignedRect();
        }
        auto cb = std::move(m_regionCb);
        closeAll();
        cb(phys, screen);
    }
}

void OverlayController::cancel()
{
    ++m_generation;
    // Notify the pending callback of cancellation (empty result) BEFORE dropping
    // it. The caller (AppContext) clears its capture-in-flight guard, ends
    // Do-Not-Disturb and answers any CLI request from inside that callback.
    // Dropping the callback WITHOUT calling it — the old behaviour — leaked
    // m_captureInFlight = true after every Esc / Cancel / failed freeze, which
    // then blocked every subsequent capture ("Another capture is already
    // active"). The region consumers already treat an empty QRect as "no-op".
    auto imageCb = std::move(m_imageCb);
    auto regionCb = std::move(m_regionCb);
    m_imageCb = nullptr;
    m_regionCb = nullptr;
    closeAll();
    if (imageCb)
        imageCb(QImage());
    else if (regionCb)
        regionCb(QRect(), nullptr);
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
    m_windowScreens.clear();
    for (QQuickWindow *w : windows) {
        w->close();
        w->deleteLater();
    }
    m_frozen.clear();
}

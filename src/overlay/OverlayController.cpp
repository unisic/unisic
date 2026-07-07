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
#include <QDebug>

OverlayController::OverlayController(AppContext *app, QObject *parent)
    : QObject(parent), m_app(app)
{
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

    m_app->captureManager()->captureAllScreens(m_screens,
        [this](const QVector<QImage> &imgs, const QString &err) {
            if (!err.isEmpty()) {
                qWarning() << "Freeze capture failed:" << err;
                if (err != QLatin1String("cancelled"))
                    m_app->showToast(tr("Screen capture failed: %1. Install Unisic "
                                        "(sudo cmake --install build) and launch it from the "
                                        "application menu so KDE authorizes it — and make sure "
                                        "xdg-desktop-portal-kde is running.").arg(err));
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
        auto *ctx = new QQmlContext(engine->rootContext(), this);
        ctx->setContextProperty(QStringLiteral("overlayController"), this);
        ctx->setContextProperty(QStringLiteral("annotationToolsEnabled"), m_annotationTools);

        QObject *obj = component.create(ctx);
        auto *win = qobject_cast<QQuickWindow *>(obj);
        if (!win) {
            delete obj;
            continue;
        }
        win->setScreen(screen);
        win->setGeometry(screen->geometry());

        if (auto *canvas = win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas"))) {
            QImage img = m_frozen[i];
            canvas->setImage(img);
        }
        win->showFullScreen();
        m_windows.append(win);
    }
    if (!m_windows.isEmpty()) {
        m_starting = false;
        m_windows.first()->requestActivate();
    } else {
        cancel();
    }
}

void OverlayController::confirmFromWindow(QQuickWindow *win)
{
    auto *canvas = win ? win->findChild<AnnotationCanvas *>(QStringLiteral("overlayCanvas")) : nullptr;
    if (!canvas || !canvas->hasSelection())
        return;

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

void OverlayController::cancel()
{
    closeAll();
    m_imageCb = nullptr;
    m_regionCb = nullptr;
}

void OverlayController::closeAll()
{
    m_starting = false;
    const auto windows = m_windows;
    m_windows.clear();
    for (QQuickWindow *w : windows) {
        w->close();
        w->deleteLater();
    }
    m_frozen.clear();
}

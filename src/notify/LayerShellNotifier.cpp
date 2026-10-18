#include "LayerShellNotifier.h"
#include "NotifCard.h"
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
#include <QDebug>

LayerShellNotifier::LayerShellNotifier(AppContext *app, QObject *parent)
    : QObject(parent), m_app(app)
{
}

void LayerShellNotifier::show(CaptureNotification *n, const QVariantMap &overrides)
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

    // One snapshot drives everything below AND the QML — the same call the xcb
    // helper's config is built from, so the two hosts cannot drift, and the
    // settings preview's unsaved overrides reach both.
    const QVariantMap eff = NotifCard::effectiveSettings(m_app->settings(), overrides);
    const QString style = NotifCard::normalizeStyle(eff.value(QStringLiteral("capturePopupStyle")).toString());
    const QSize card = NotifCard::sizeForStyle(style);
    const int cardW = card.width(), cardH = card.height();
    // `edge` is the user-set gap to the screen edge. Layer-shell measures margins
    // from the anchored edge AFTER other clients' exclusive zones, so this adds
    // to the panel clearance the compositor already grants — it never fights it.
    const int pad = NotifCard::kPad;
    const int edge = qMax(0, eff.value(QStringLiteral("capturePopupMargin")).toInt());

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    auto *ctx = new QQmlContext(engine->rootContext(), this);
    ctx->setContextProperty(QStringLiteral("notif"), n);
    ctx->setContextProperty(QStringLiteral("popupX"), pad);
    ctx->setContextProperty(QStringLiteral("popupY"), pad);
    ctx->setContextProperty(QStringLiteral("popupW"), cardW);
    ctx->setContextProperty(QStringLiteral("popupH"), cardH);
    ctx->setContextProperty(QStringLiteral("popupStyle"), style);
    ctx->setContextProperty(QStringLiteral("popupAutoHideSec"),
                            qMax(0, eff.value(QStringLiteral("capturePopupDurationSec")).toInt()));
    ctx->setContextProperty(QStringLiteral("popupHiddenActions"),
                            eff.value(QStringLiteral("hiddenNotifActions")).toString());
    ctx->setContextProperty(QStringLiteral("popupActionOrder"),
                            eff.value(QStringLiteral("notificationActionOrder")).toString());

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

    if (screen)
        win->setScreen(screen);
    win->resize(cardW + 2 * pad, cardH + 2 * pad);
    place(win, screen, eff.value(QStringLiteral("capturePopupPosition")).toString(), edge);

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

#include "PortalRequest.h"
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>

static int s_tokenCounter = 0;

QString PortalRequest::nextToken()
{
    return QStringLiteral("unisic_%1_%2").arg(QCoreApplication::applicationPid()).arg(++s_tokenCounter);
}

QString PortalRequest::expectedPath(const QString &token, const QDBusConnection &bus)
{
    QString sender = bus.baseService().mid(1); // strip ':'
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

PortalRequest::PortalRequest(const QString &token, Callback cb, QObject *parent,
                             const QDBusConnection &bus)
    : QObject(parent), m_bus(bus), m_cb(std::move(cb))
{
    subscribe(expectedPath(token, m_bus));
    // If xdg-desktop-portal dies before emitting Response (crash/restart while
    // an interactive dialog is open), no signal ever arrives: the request — and
    // the capture-callback chain it captured — would sit on its long-lived
    // parent forever, and the capture flow would hang silently. Complete with
    // an error instead. Routed through onResponse: m_cb is moved out on first
    // completion, so a race with a real Response/late error is harmless.
    // No timeout here — interactive portal dialogs legitimately stay open long.
    auto *w = new QDBusServiceWatcher(QStringLiteral("org.freedesktop.portal.Desktop"),
                                      m_bus,
                                      QDBusServiceWatcher::WatchForUnregistration, this);
    connect(w, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        onResponse(2, {{QStringLiteral("error"),
                        QStringLiteral("xdg-desktop-portal exited while the request was pending")}});
    });
}

void PortalRequest::subscribe(const QString &path)
{
    if (!m_path.isEmpty()) {
        m_bus.disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(onResponse(uint, QVariantMap)));
    }
    m_path = path;
    m_bus.connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(onResponse(uint, QVariantMap)));
}

void PortalRequest::send(QDBusMessage msg, const QString &handleToken, Callback cb, QObject *parent,
                         int timeoutMs, const QDBusConnection &bus)
{
    auto *req = new PortalRequest(handleToken, std::move(cb), parent, bus);
    if (timeoutMs > 0) {
        // Watchdog for NON-interactive requests: a hung (but not dead) portal
        // backend keeps its bus name and never emits Response, so neither the
        // service watcher nor the DBus reply timeout fires and the capture
        // callback is lost forever. Route a synthetic error through onResponse
        // (m_cb is moved out on the first completion, so a race with a real or
        // late Response is harmless). The timer is bound to req's lifetime, so
        // a real completion's deleteLater cancels it.
        QTimer::singleShot(timeoutMs, req, [req] {
            req->onResponse(2, {{QStringLiteral("error"),
                                 QStringLiteral("portal request timed out")}});
        });
    }
    QDBusPendingCall call = req->m_bus.asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(call, req);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, req, [req](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            qWarning() << "Portal call failed:" << reply.error().message();
            auto cb = std::move(req->m_cb);
            req->deleteLater();
            QVariantMap results;
            results.insert(QStringLiteral("error"),
                           reply.error().name() + QStringLiteral(": ") + reply.error().message());
            if (cb) cb(2, results);
            return;
        }
        const QString actual = reply.value().path();
        if (actual != req->m_path)
            req->subscribe(actual); // older portals return a different handle
    });
}

void PortalRequest::onResponse(uint code, const QVariantMap &results)
{
    auto cb = std::move(m_cb);
    deleteLater();
    if (cb) cb(code, results);
}

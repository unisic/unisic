#include "PortalRequest.h"
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QCoreApplication>
#include <QDebug>

static int s_tokenCounter = 0;

QString PortalRequest::nextToken()
{
    return QStringLiteral("unisic_%1_%2").arg(QCoreApplication::applicationPid()).arg(++s_tokenCounter);
}

QString PortalRequest::expectedPath(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService().mid(1); // strip ':'
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

PortalRequest::PortalRequest(const QString &token, Callback cb, QObject *parent)
    : QObject(parent), m_cb(std::move(cb))
{
    subscribe(expectedPath(token));
}

void PortalRequest::subscribe(const QString &path)
{
    if (!m_path.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(onResponse(uint, QVariantMap)));
    }
    m_path = path;
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_path,
        QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        this, SLOT(onResponse(uint, QVariantMap)));
}

void PortalRequest::send(QDBusMessage msg, const QString &handleToken, Callback cb, QObject *parent)
{
    auto *req = new PortalRequest(handleToken, std::move(cb), parent);
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
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

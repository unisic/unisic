#include "NotificationInhibitor.h"

#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

namespace {
constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";
constexpr auto kInterface = "org.freedesktop.Notifications";
}

NotificationInhibitor::NotificationInhibitor(QObject *parent)
    : QObject(parent)
{
}

NotificationInhibitor::~NotificationInhibitor()
{
    if (m_cookie != 0) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QString::fromLatin1(kService), QString::fromLatin1(kPath),
            QString::fromLatin1(kInterface), QStringLiteral("UnInhibit"));
        msg << m_cookie;
        QDBusConnection::sessionBus().send(msg);
    }
}

bool NotificationInhibitor::supportedDesktop()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP")
        .contains(QLatin1String("KDE"), Qt::CaseInsensitive);
}

void NotificationInhibitor::acquire()
{
    ++m_depth;
    if (!supportedDesktop() || m_cookie != 0 || m_pending)
        return;
    m_pending = true;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("Inhibit"));
    msg << QGuiApplication::desktopFileName()
        << tr("Screen capture or recording in progress") << QVariantMap{};
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *done) {
        QDBusPendingReply<uint> reply = *done;
        done->deleteLater();
        m_pending = false;
        if (!reply.isError())
            m_cookie = reply.value();
        if (m_depth == 0)
            sendRelease();
    });
}

void NotificationInhibitor::release()
{
    if (m_depth > 0)
        --m_depth;
    if (m_depth == 0 && !m_pending)
        sendRelease();
}

void NotificationInhibitor::sendRelease()
{
    if (m_cookie == 0)
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("UnInhibit"));
    msg << m_cookie;
    m_cookie = 0;
    QDBusConnection::sessionBus().asyncCall(msg);
}

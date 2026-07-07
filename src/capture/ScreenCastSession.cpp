#include "ScreenCastSession.h"
#include "PortalRequest.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QCoreApplication>
#include <QDebug>
#include <unistd.h>
#include <fcntl.h>

static const auto PORTAL_SERVICE = QStringLiteral("org.freedesktop.portal.Desktop");
static const auto PORTAL_PATH = QStringLiteral("/org/freedesktop/portal/desktop");
static const auto SCREENCAST_IFACE = QStringLiteral("org.freedesktop.portal.ScreenCast");

ScreenCastSession::ScreenCastSession(QObject *parent) : QObject(parent) {}

static uint screenCastPortalVersion()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PORTAL_SERVICE, PORTAL_PATH,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << SCREENCAST_IFACE << QStringLiteral("version");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return 0;
    const QVariant value = reply.arguments().constFirst();
    if (value.canConvert<QDBusVariant>())
        return value.value<QDBusVariant>().variant().toUInt();
    return value.toUInt();
}

ScreenCastSession::~ScreenCastSession()
{
    if (!m_sessionHandle.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            PORTAL_SERVICE, m_sessionHandle,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
        QDBusConnection::sessionBus().asyncCall(msg);
    }
}

void ScreenCastSession::start(bool includeCursor, uint sourceTypes, const QString &restoreToken)
{
    m_sourceTypes = sourceTypes ? sourceTypes : 1;
    m_restoreToken = restoreToken;
    createSession(includeCursor);
}

void ScreenCastSession::createSession(bool includeCursor)
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                                                      QStringLiteral("CreateSession"));
    msg << QVariantMap{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("session_handle_token"), QStringLiteral("unisic_%1").arg(QCoreApplication::applicationPid())},
    };
    PortalRequest::send(msg, token, [this, includeCursor](uint code, const QVariantMap &results) {
        if (code != 0) {
            emit failed(QStringLiteral("ScreenCast CreateSession failed (code %1)").arg(code));
            return;
        }
        m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
        if (m_sessionHandle.isEmpty()) {
            emit failed(QStringLiteral("Portal returned empty session handle"));
            return;
        }
        selectSources(includeCursor);
    }, this);
}

void ScreenCastSession::selectSources(bool includeCursor)
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                                                       QStringLiteral("SelectSources"));
    QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("types"), m_sourceTypes},                   // MONITOR / WINDOW
        {QStringLiteral("multiple"), false},
        {QStringLiteral("cursor_mode"), uint(includeCursor ? 2 : 1)}, // EMBEDDED : HIDDEN
    };

    m_restoreTokensSupported = screenCastPortalVersion() >= 4;
    if (m_restoreTokensSupported) {
        options.insert(QStringLiteral("persist_mode"), uint(2));
        if (!m_restoreToken.isEmpty())
            options.insert(QStringLiteral("restore_token"), m_restoreToken);
    }

    msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
        << options;
    PortalRequest::send(msg, token, [this](uint code, const QVariantMap &) {
        if (code != 0) {
            emit failed(code == 1 ? QStringLiteral("cancelled")
                                  : QStringLiteral("ScreenCast SelectSources failed"));
            return;
        }
        startCast();
    }, this);
}

void ScreenCastSession::startCast()
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                                                      QStringLiteral("Start"));
    msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
        << QString() // parent_window
        << QVariantMap{{QStringLiteral("handle_token"), token}};
    PortalRequest::send(msg, token, [this](uint code, const QVariantMap &results) {
        if (code != 0) {
            emit failed(code == 1 ? QStringLiteral("cancelled")
                                  : QStringLiteral("ScreenCast Start failed"));
            return;
        }
        if (m_restoreTokensSupported)
            emit restoreTokenChanged(results.value(QStringLiteral("restore_token")).toString());
        // streams: a(ua{sv})
        uint nodeId = 0;
        QSize size;
        QPoint pos;
        const QVariant streamsVar = results.value(QStringLiteral("streams"));
        const QDBusArgument arg = streamsVar.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            arg.beginStructure();
            uint node = 0;
            QVariantMap props;
            arg >> node >> props;
            arg.endStructure();
            if (!nodeId) {
                nodeId = node;
                const QVariant sz = props.value(QStringLiteral("size"));
                const QVariant p = props.value(QStringLiteral("position"));
                if (sz.isValid()) {
                    const QDBusArgument sa = sz.value<QDBusArgument>();
                    int w = 0, h = 0;
                    sa.beginStructure(); sa >> w >> h; sa.endStructure();
                    size = QSize(w, h);
                }
                if (p.isValid()) {
                    const QDBusArgument pa = p.value<QDBusArgument>();
                    int x = 0, y = 0;
                    pa.beginStructure(); pa >> x >> y; pa.endStructure();
                    pos = QPoint(x, y);
                }
            }
        }
        arg.endArray();
        if (!nodeId) {
            emit failed(QStringLiteral("No PipeWire stream returned by portal"));
            return;
        }
        openRemote(nodeId, size, pos);
    }, this);
}

void ScreenCastSession::openRemote(uint nodeId, const QSize &size, const QPoint &pos)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                                                      QStringLiteral("OpenPipeWireRemote"));
    msg << QVariant::fromValue(QDBusObjectPath(m_sessionHandle)) << QVariantMap{};
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, nodeId, size, pos](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusUnixFileDescriptor> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            emit failed(QStringLiteral("OpenPipeWireRemote failed: %1").arg(reply.error().message()));
            return;
        }
        // Dup: QDBusUnixFileDescriptor closes its fd when the reply is destroyed.
        int fd = fcntl(reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 3);
        if (fd < 0) {
            emit failed(QStringLiteral("Failed to dup PipeWire fd"));
            return;
        }
        emit ready(fd, nodeId, size, pos);
    });
}

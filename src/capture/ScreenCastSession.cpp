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
#include <QMetaMethod>
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

uint ScreenCastSession::availableCursorModes()
{
    // Cache only a successful answer: a transient failure (0) must not disable
    // the metadata path for the whole process lifetime.
    static uint cached = 0;
    if (cached != 0)
        return cached;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        PORTAL_SERVICE, PORTAL_PATH,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << SCREENCAST_IFACE << QStringLiteral("AvailableCursorModes");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return 0;
    const QVariant value = reply.arguments().constFirst();
    const uint modes = value.canConvert<QDBusVariant>()
                           ? value.value<QDBusVariant>().variant().toUInt()
                           : value.toUInt();
    cached = modes;
    return modes;
}

ScreenCastSession::~ScreenCastSession()
{
    if (!m_sessionHandle.isEmpty()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            PORTAL_SERVICE, m_sessionHandle,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
        // send(): fire-and-forget that still gets flushed at app shutdown,
        // when asyncCall's reply tracking would never run.
        QDBusConnection::sessionBus().send(msg);
    }
}

void ScreenCastSession::start(CursorMode cursor, uint sourceTypes, const QString &restoreToken)
{
    m_sourceTypes = sourceTypes ? sourceTypes : 1;
    m_restoreToken = restoreToken;
    createSession(cursor);
}

void ScreenCastSession::createSession(CursorMode cursor)
{
    const QString token = PortalRequest::nextToken();
    // Unique per session, not just per process: two sessions in one process
    // lifetime must not collide on the same portal session object path.
    static int s_sessionCounter = 0;
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                                                      QStringLiteral("CreateSession"));
    msg << QVariantMap{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("session_handle_token"),
         QStringLiteral("unisic_%1_%2").arg(QCoreApplication::applicationPid()).arg(++s_sessionCounter)},
    };
    PortalRequest::send(msg, token, [this, cursor](uint code, const QVariantMap &results) {
        if (code != 0) {
            const QString detail = results.value(QStringLiteral("error")).toString();
            emit failed(QStringLiteral("ScreenCast CreateSession failed (code %1)").arg(code)
                        + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
            return;
        }
        // Spec says `o` (object path); some portals return `s`.
        const QVariant sh = results.value(QStringLiteral("session_handle"));
        m_sessionHandle = sh.canConvert<QDBusObjectPath>() ? sh.value<QDBusObjectPath>().path()
                                                           : sh.toString();
        if (m_sessionHandle.isEmpty()) {
            emit failed(QStringLiteral("Portal returned empty session handle"));
            return;
        }
        // Surface "stopped sharing from the system UI" instead of silently
        // recording a frozen frame forever.
        QDBusConnection::sessionBus().connect(
            PORTAL_SERVICE, m_sessionHandle,
            QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Closed"),
            this, SIGNAL(sessionClosed()));
        selectSources(cursor);
    }, this);
}

void ScreenCastSession::selectSources(CursorMode cursor)
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENCAST_IFACE,
                                                       QStringLiteral("SelectSources"));
    QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("types"), m_sourceTypes},                   // MONITOR / WINDOW
        {QStringLiteral("multiple"), false},
        {QStringLiteral("cursor_mode"), uint(cursor)},
    };

    // Blocking Properties.Get — cache it (GUI thread), but only a successful
    // answer: caching a transient failure (0) would disable restore tokens
    // for the whole process lifetime.
    static uint portalVersion = 0;
    if (portalVersion == 0)
        portalVersion = screenCastPortalVersion();
    m_restoreTokensSupported = portalVersion >= 4;
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
        // Sentinel: the portal may omit "position" — (0,0) is a legit primary-
        // monitor origin, so absence must be distinguishable for the consumer.
        QPoint pos(INT_MIN, INT_MIN);
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
        // Recorder cancelled mid-flight (disconnect(this) + deleteLater): the
        // reply may still land before the deferred delete. Nobody would take
        // ownership of the dup'd fd — emitting to zero receivers leaked one fd
        // per cancel in a long-lived tray app. Compile-checked signal lookup.
        if (!isSignalConnected(QMetaMethod::fromSignal(&ScreenCastSession::ready)))
            return;
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

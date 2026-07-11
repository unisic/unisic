#pragma once
#include <QObject>
#include <QVariantMap>
#include <QDBusMessage>
#include <QDBusConnection>
#include <functional>

// Helper implementing the xdg-desktop-portal Request pattern:
// subscribe to org.freedesktop.portal.Request.Response on the expected
// request object path BEFORE issuing the call, then invoke the callback
// with (response_code, results). Self-deletes after firing.
class PortalRequest : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(uint code, const QVariantMap &results)>;

    // `msg` must be a fully prepared method call whose options vardict already
    // contains the handle_token returned by nextToken().
    // timeoutMs > 0 arms a watchdog: if no Response arrives in time the request
    // completes with an error. A hung (not dead) portal backend keeps its bus
    // name, so the service watcher never fires and the request-handle reply
    // already landed — nothing else would ever unwedge the callback. Pass 0 for
    // interactive dialogs, which legitimately stay open indefinitely.
    // `bus` lets a caller route the request over a PRIVATE bus connection (the
    // portal keys app identity per connection — see PortalGlobalShortcuts);
    // the Response subscription and request path derive from that connection.
    static void send(QDBusMessage msg, const QString &handleToken, Callback cb, QObject *parent,
                     int timeoutMs = 0,
                     const QDBusConnection &bus = QDBusConnection::sessionBus());

    // Generates a unique handle token and the request object path it maps to.
    static QString nextToken();
    static QString expectedPath(const QString &token,
                                const QDBusConnection &bus = QDBusConnection::sessionBus());

private slots:
    void onResponse(uint code, const QVariantMap &results);

private:
    explicit PortalRequest(const QString &token, Callback cb, QObject *parent,
                           const QDBusConnection &bus);
    void subscribe(const QString &path);

    QDBusConnection m_bus;
    QString m_path;
    Callback m_cb;
};

#include "DesktopNotifier.h"
#include "CaptureNotification.h"
#include "AppContext.h"
#include "Settings.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QVariantMap>

static const QString kService = QStringLiteral("org.freedesktop.Notifications");
static const QString kPath    = QStringLiteral("/org/freedesktop/Notifications");
static const QString kIface   = QStringLiteral("org.freedesktop.Notifications");

DesktopNotifier::DesktopNotifier(AppContext *app, QObject *parent)
    : QObject(parent), m_app(app)
{
    // The server broadcasts these; we filter by ids we actually sent.
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kService, kPath, kIface, QStringLiteral("ActionInvoked"),
                this, SLOT(onActionInvoked(uint,QString)));
    bus.connect(kService, kPath, kIface, QStringLiteral("NotificationClosed"),
                this, SLOT(onNotificationClosed(uint,uint)));
    // Track the server's Inhibited property (KDE: fullscreen / DND / screen share).
    bus.connect(kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    // Seed the initial value asynchronously — a blocking QDBusInterface ctor
    // (Introspect) + Get on the GUI thread would stall startup if the server is
    // slow. PropertiesChanged keeps it current thereafter.
    if (available()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            kService, kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Get"));
        msg << kIface << QStringLiteral("Inhibited");
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            const QDBusPendingReply<QVariant> reply = *w;
            if (!reply.isError())
                m_inhibited = reply.value().toBool();
        });
    }
}

void DesktopNotifier::onPropertiesChanged(const QString &interfaceName,
                                          const QVariantMap &changed, const QStringList &)
{
    if (interfaceName != kIface)
        return;
    const auto it = changed.find(QStringLiteral("Inhibited"));
    if (it != changed.end())
        m_inhibited = it.value().toBool();
}

bool DesktopNotifier::available()
{
    auto *bi = QDBusConnection::sessionBus().interface();
    return bi && bi->isServiceRegistered(kService);
}

void DesktopNotifier::show(CaptureNotification *n)
{
    if (!n)
        return;
    if (!available()) {
        n->deleteLater();
        return;
    }
    const uint id = sendNotify(n);
    if (id == 0) { // Notify failed — don't leak the backing object
        n->deleteLater();
        return;
    }
    m_active.insert(id, n);
}

uint DesktopNotifier::sendNotify(CaptureNotification *n)
{
    QDBusInterface iface(kService, kPath, kIface, QDBusConnection::sessionBus());

    // [key, label, key, label, …]. Keys are stable; labels localized. Servers
    // that advertise the "actions" capability render these as buttons.
    const QStringList actions{
        QStringLiteral("open"),   tr("Open"),
        QStringLiteral("copy"),   tr("Copy"),
        QStringLiteral("upload"), tr("Upload"),
        QStringLiteral("delete"), tr("Delete"),
    };

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("org.unisic.Unisic"));
    hints.insert(QStringLiteral("category"), QStringLiteral("transfer.complete"));
    const QString thumb = n->thumbFilePath();
    if (!thumb.isEmpty())
        hints.insert(QStringLiteral("image-path"),
                     QUrl::fromLocalFile(thumb).toString());   // inline preview
    // x-kde-urls is the *actual* capture file (KDE's copy/drag actions), not the
    // preview thumbnail — only when it's been saved to disk.
    if (!n->filePath().isEmpty())
        hints.insert(QStringLiteral("x-kde-urls"),
                     QStringList{QUrl::fromLocalFile(n->filePath()).toString()});

    const int durSec = m_app->settings()->capturePopupDurationSec();
    const int timeout = durSec > 0 ? durSec * 1000 : 0; // 0 = never expire

    const QString summary = tr("Capture ready");
    const QString body = n->fileName();

    QDBusReply<uint> reply = iface.call(
        QStringLiteral("Notify"),
        QStringLiteral("Unisic"),             // app_name
        0u,                                   // replaces_id (0 = new)
        QStringLiteral("org.unisic.Unisic"),  // app_icon
        summary, body, actions, hints, timeout);
    return reply.isValid() ? reply.value() : 0u;
}

void DesktopNotifier::onActionInvoked(uint id, const QString &key)
{
    CaptureNotification *n = m_active.value(id);
    if (!n)
        return; // not one of ours
    if (key == QLatin1String("open") || key == QLatin1String("default"))
        n->openCapture();
    else if (key == QLatin1String("copy"))
        n->copyImage();
    else if (key == QLatin1String("upload"))
        n->upload();
    else if (key == QLatin1String("delete"))
        n->deleteCapture();
    // The server closes a non-resident notification right after the action;
    // the object is retired in onNotificationClosed (which waits out an upload).
}

void DesktopNotifier::onNotificationClosed(uint id, uint)
{
    CaptureNotification *n = m_active.take(id);
    if (n)
        retire(n);
}

void DesktopNotifier::retire(CaptureNotification *n)
{
    // An "Upload" action started an async upload whose callback still writes
    // into `n` — keep it alive until that finishes, then drop it.
    if (n->uploading()) {
        connect(n, &CaptureNotification::stateChanged, n, [n] {
            if (!n->uploading())
                n->deleteLater();
        });
        return;
    }
    n->deleteLater();
}

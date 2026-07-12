#include "PortalScreenshot.h"
#include "PortalRequest.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QRegularExpression>
#include <QUrl>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QtConcurrentRun>
#include <QDebug>

// The app id xdg-desktop-portal derives for a host process from its systemd
// unit — the same "app[-<launcher>]-<ApplicationID>-<RANDOM>.scope" rule as
// the portal's xdp-app-info-host.c (unit read from /proc/self/cgroup; \xNN
// escapes don't occur in our ids, so no cunescape needed). Empty when the
// process runs in a non-app scope (terminal, ssh) — the portal sees "" then.
static QString scopeAppId()
{
    QFile f(QStringLiteral("/proc/self/cgroup"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString cgroup = QString::fromUtf8(f.readAll());
    const QString unit = cgroup.section(QLatin1Char('/'), -1).trimmed();
    static const QRegularExpression re(
        QStringLiteral("^app-(?:[[:alnum:]]+-)?(.+?)(?:-[[:alnum:]]*)(?:\\.scope|\\.slice)$"));
    const QRegularExpressionMatch m = re.match(unit);
    return m.hasMatch() ? m.captured(1) : QString();
}

QStringList PortalScreenshot::candidateAppIds()
{
    QStringList ids{QGuiApplication::desktopFileName(), QString()};
    const QString scoped = scopeAppId();
    if (!scoped.isEmpty() && !ids.contains(scoped))
        ids.append(scoped);
    return ids;
}

void PortalScreenshot::ensureSilentPermission(QObject *context, std::function<void()> then)
{
    const QStringList appIds = candidateAppIds();
    // Fire all writes, run `then` after the LAST one is acknowledged — the
    // silent Screenshot request must not race its own grant.
    auto remaining = std::make_shared<int>(appIds.size());
    auto fire = std::make_shared<std::function<void()>>(std::move(then));
    for (const QString &appId : appIds) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
            QStringLiteral("/org/freedesktop/impl/portal/PermissionStore"),
            QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
            QStringLiteral("SetPermission"));
        msg << QStringLiteral("screenshot") << true << QStringLiteral("screenshot")
            << appId << QStringList{QStringLiteral("yes")};
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(msg), context);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                         [watcher, remaining, fire](QDBusPendingCallWatcher *) {
            watcher->deleteLater();
            // Failure (no permission store on the bus) is not fatal — the
            // request itself may still pass; proceed either way.
            if (--*remaining == 0 && *fire)
                (*fire)();
        });
    }
    if (appIds.isEmpty() && *fire)
        (*fire)();
}

void PortalScreenshot::capture(bool interactive, Callback cb, bool allowInteractiveFallback)
{
    requestOnce(interactive, [this, interactive, allowInteractiveFallback, cb](const QImage &img, const QString &err) {
        if (err.isEmpty() || interactive || !allowInteractiveFallback || err == QLatin1String("cancelled")) {
            cb(img, err);
            return;
        }
        // Silent request denied (no stored permission / unresolvable app id,
        // e.g. running from a build tree). Fall back to the desktop's own
        // interactive screenshot dialog so capture still works.
        qInfo() << "Non-interactive screenshot denied — retrying with the interactive portal dialog";
        requestOnce(true, cb);
    });
}

void PortalScreenshot::requestOnce(bool interactive, Callback cb)
{
    if (!interactive) {
        // Re-assert the silent grant IMMEDIATELY before the request: a denied
        // GNOME access dialog leaves a sticky "no" in the permission store,
        // after which every non-interactive Screenshot fails with response
        // code 2 (no dialog, nothing to retry) — reported live on GNOME as
        // "region capture keeps giving Code 2". A single startup grant can't
        // repair that; a pre-request one always does.
        ensureSilentPermission(this, [this, cb = std::move(cb)]() mutable {
            sendRequest(false, std::move(cb));
        });
        return;
    }
    sendRequest(true, std::move(cb));
}

void PortalScreenshot::sendRequest(bool interactive, Callback cb)
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Screenshot"),
        QStringLiteral("Screenshot"));
    QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("interactive"), interactive},
    };
    msg << QString() << options; // parent_window: empty (no exported handle on Wayland)

    PortalRequest::send(msg, token, [cb](uint code, const QVariantMap &results) {
        if (code != 0) {
            QString err = code == 1 ? QStringLiteral("cancelled")
                                    : results.value(QStringLiteral("error")).toString();
            if (err.isEmpty())
                err = QStringLiteral("Screenshot portal request failed (code %1)").arg(code);
            cb({}, err);
            return;
        }
        const QUrl uri(results.value(QStringLiteral("uri")).toString());
        const QString file = uri.toLocalFile();
        // Decode off-thread: a 4K/multi-monitor PNG takes 50-200 ms — too long
        // for the GUI thread. Remove the portal's file (dropped in ~/Pictures)
        // on every path, or failures leave orphans behind.
        QPointer<QCoreApplication> application(qApp);
        (void)QtConcurrent::run([file, uriStr = uri.toString(), cb, application] {
            QImage img(file);
            QFile::remove(file);
            if (!application)
                return;
            QMetaObject::invokeMethod(application.data(), [img, uriStr, cb] {
                if (img.isNull())
                    cb({}, QStringLiteral("Could not load screenshot from %1").arg(uriStr));
                else
                    cb(img, {});
            }, Qt::QueuedConnection);
        });
    }, this, interactive ? 0 : 30000); // silent request: 30 s watchdog; interactive dialog: untimed
}

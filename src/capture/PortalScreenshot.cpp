#include "PortalScreenshot.h"
#include "PortalRequest.h"
#include <QDBusMessage>
#include <QUrl>
#include <QFile>
#include <QDebug>

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
        QImage img(uri.toLocalFile());
        if (img.isNull()) {
            cb({}, QStringLiteral("Could not load screenshot from %1").arg(uri.toString()));
            return;
        }
        QFile::remove(uri.toLocalFile()); // portal leaves the file in ~/Pictures; we own the pixels now
        cb(img, {});
    }, this);
}

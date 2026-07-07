#include "PortalScreenshot.h"
#include "PortalRequest.h"
#include <QDBusMessage>
#include <QUrl>
#include <QFile>
#include <QGuiApplication>
#include <QtConcurrentRun>
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
        const QString file = uri.toLocalFile();
        // Decode off-thread: a 4K/multi-monitor PNG takes 50-200 ms — too long
        // for the GUI thread. Remove the portal's file (dropped in ~/Pictures)
        // on every path, or failures leave orphans behind.
        (void)QtConcurrent::run([file, uriStr = uri.toString(), cb] {
            QImage img(file);
            QFile::remove(file);
            QMetaObject::invokeMethod(qApp, [img, uriStr, cb] {
                if (img.isNull())
                    cb({}, QStringLiteral("Could not load screenshot from %1").arg(uriStr));
                else
                    cb(img, {});
            }, Qt::QueuedConnection);
        });
    }, this);
}

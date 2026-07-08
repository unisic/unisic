#include "GnomeScreenshot.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDir>
#include <QFile>
#include <QUuid>

static const auto GS_SERVICE = QStringLiteral("org.gnome.Shell.Screenshot");
static const auto GS_PATH = QStringLiteral("/org/gnome/Shell/Screenshot");
static const auto GS_IFACE = QStringLiteral("org.gnome.Shell.Screenshot");

GnomeScreenshot::~GnomeScreenshot()
{
    // Reap temp PNGs whose D-Bus reply never came before shutdown.
    for (const QString &p : std::as_const(m_pending))
        QFile::remove(p);
}

bool GnomeScreenshot::isAvailable()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(GS_SERVICE);
}

bool GnomeScreenshot::isNiriSession()
{
    const auto has = [](const char *var) {
        return qEnvironmentVariable(var).contains(QLatin1String("niri"), Qt::CaseInsensitive);
    };
    return has("XDG_CURRENT_DESKTOP") || has("XDG_SESSION_DESKTOP");
}

struct LoadedShot {
    QImage image;
    QString error;
};

// Read + decode the PNG the compositor just wrote, then delete it. Runs off the
// GUI thread (screenshots can be multi-megabyte 4K PNGs).
static LoadedShot loadAndUnlink(const QString &path)
{
    LoadedShot r;
    QImage img;
    if (!img.load(path)) {
        r.error = QStringLiteral("GNOME Shell wrote no readable image at %1").arg(path);
    } else {
        r.image = img;
    }
    QFile::remove(path);
    return r;
}

void GnomeScreenshot::shoot(const QString &method, const QVariantList &leadingArgs, Callback cb)
{
    const QString path = QDir::tempPath() + QStringLiteral("/unisic-shot-")
                         + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".png");

    QDBusMessage msg = QDBusMessage::createMethodCall(GS_SERVICE, GS_PATH, GS_IFACE, method);
    QVariantList args = leadingArgs;
    args << path; // filename is always the last in-arg for all three methods
    msg.setArguments(args);

    m_pending.insert(path); // reaped by the finished handler or the destructor
    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, 30000);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, path, cb](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<bool, QString> reply = *w;
        w->deleteLater();
        m_pending.remove(path);
        if (reply.isError()) {
            QFile::remove(path);
            cb({}, reply.error().name() + QStringLiteral(": ") + reply.error().message());
            return;
        }
        if (!reply.argumentAt<0>()) {
            QFile::remove(path);
            cb({}, QStringLiteral("org.gnome.Shell.Screenshot reported failure"));
            return;
        }
        // filename_used may differ from what we asked for; prefer it if given.
        const QString used = reply.argumentAt<1>();
        const QString src = used.isEmpty() ? path : used;
        auto *fw = new QFutureWatcher<LoadedShot>(this);
        connect(fw, &QFutureWatcher<LoadedShot>::finished, this, [fw, cb]() {
            LoadedShot r = fw->result();
            fw->deleteLater();
            cb(r.image, r.error);
        });
        fw->setFuture(QtConcurrent::run(loadAndUnlink, src));
    });
}

void GnomeScreenshot::captureWorkspace(bool includeCursor, Callback cb)
{
    // Screenshot(include_cursor, flash, filename)
    shoot(QStringLiteral("Screenshot"), {includeCursor, false}, std::move(cb));
}

void GnomeScreenshot::captureArea(int x, int y, int w, int h, bool /*includeCursor*/, Callback cb)
{
    // ScreenshotArea(x, y, width, height, flash, filename) — no cursor arg upstream.
    shoot(QStringLiteral("ScreenshotArea"), {x, y, w, h, false}, std::move(cb));
}

void GnomeScreenshot::captureActiveWindow(bool includeCursor, Callback cb)
{
    // ScreenshotWindow(include_frame, include_cursor, flash, filename)
    shoot(QStringLiteral("ScreenshotWindow"), {true, includeCursor, false}, std::move(cb));
}

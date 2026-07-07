#include "CaptureManager.h"
#include "PortalScreenshot.h"
#include "KWinScreenShot2.h"
#include "Settings.h"
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>

// Inside Flatpak the KDE Screenshot portal denies non-interactive requests
// unless the permission store answers "yes" for this app id, and the overlay
// freeze must be silent (a dialog per capture is unusable). Self-grant the
// permission — the manifest allows it via
// --talk-name=org.freedesktop.impl.portal.PermissionStore.
static void grantSilentScreenshotPermission()
{
    const QString appId = qEnvironmentVariable("FLATPAK_ID");
    if (appId.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
        QStringLiteral("/org/freedesktop/impl/portal/PermissionStore"),
        QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
        QStringLiteral("SetPermission"));
    msg << QStringLiteral("screenshot") << true << QStringLiteral("screenshot")
        << appId << QStringList{QStringLiteral("yes")};
    QDBusConnection::sessionBus().asyncCall(msg);
}

static bool isKWinAuthError(const QString &err)
{
    return err.contains(QLatin1String("NoAuthorized"))
           || err.contains(QLatin1String("not authorized"), Qt::CaseInsensitive);
}

static QString combinedError(const QString &first, const QString &second)
{
    if (first.isEmpty()) return second;
    if (second.isEmpty()) return first;
    return QStringLiteral("KWin failed: %1; portal failed: %2").arg(first, second);
}

static QString screenLabel(QScreen *screen)
{
    if (!screen)
        return QStringLiteral("<null>");
    const QRect g = screen->geometry();
    return QStringLiteral("%1 geometry=%2 dpr=%3")
        .arg(screen->name(), QStringLiteral("%1,%2 %3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()))
        .arg(screen->devicePixelRatio());
}

CaptureManager::CaptureManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_portal(new PortalScreenshot(this))
    , m_kwin(new KWinScreenShot2(this))
{
    grantSilentScreenshotPermission();
}

void CaptureManager::portalFallback(Callback cb)
{
    m_portal->capture(false, std::move(cb));
}

void CaptureManager::captureWorkspace(Callback cb)
{
    if (!m_kwinDenied && KWinScreenShot2::isAvailable()) {
        m_kwin->captureWorkspace(m_settings->includeCursor(),
            [this, cb](const QImage &img, const QString &err) {
                if (!err.isEmpty()) {
                    qWarning() << "KWin capture failed, falling back to portal:" << err;
                    if (isKWinAuthError(err))
                        m_kwinDenied = true;
                    portalFallback([cb, err](const QImage &img, const QString &portalErr) {
                        if (!portalErr.isEmpty()) { cb({}, combinedError(err, portalErr)); return; }
                        cb(img, {});
                    });
                    return;
                }
                cb(img, {});
            });
        return;
    }
    portalFallback(std::move(cb));
}

void CaptureManager::captureScreen(QScreen *screen, Callback cb)
{
    // The portal dialog can stay open for seconds; the screen may be gone.
    QPointer<QScreen> sp(screen);
    if (!m_kwinDenied && KWinScreenShot2::isAvailable() && screen) {
        m_kwin->captureScreen(screen->name(), m_settings->includeCursor(),
            [this, sp, cb](const QImage &img, const QString &err) {
                if (!err.isEmpty()) {
                    qWarning() << "KWin captureScreen failed, portal fallback:" << err;
                    if (isKWinAuthError(err))
                        m_kwinDenied = true;
                    // Portal returns the whole workspace: crop to the screen.
                    portalFallback([sp, cb, err](const QImage &full, const QString &e2) {
                        if (!e2.isEmpty()) { cb({}, combinedError(err, e2)); return; }
                        if (!sp) { cb({}, QStringLiteral("screen disconnected during capture")); return; }
                        QImage crop = CaptureManager::cropForScreen(full, sp);
                        if (crop.isNull()) {
                            cb({}, combinedError(err, QStringLiteral("portal screenshot does not contain screen %1")
                                                 .arg(screenLabel(sp))));
                            return;
                        }
                        cb(crop, {});
                    });
                    return;
                }
                cb(img, {});
            });
        return;
    }
    // Portal path: capture all and crop.
    const bool hadScreen = screen != nullptr;
    portalFallback([sp, hadScreen, cb](const QImage &full, const QString &err) {
        if (!err.isEmpty() || !hadScreen) { cb(full, err); return; }
        if (!sp) { cb({}, QStringLiteral("screen disconnected during capture")); return; }
        QImage crop = cropForScreen(full, sp);
        if (crop.isNull()) {
            cb({}, QStringLiteral("portal screenshot does not contain screen %1").arg(screenLabel(sp)));
            return;
        }
        cb(crop, {});
    });
}

QImage CaptureManager::cropForScreen(const QImage &workspace, QScreen *screen)
{
    if (!screen || workspace.isNull())
        return {};

    const QRect virtualGeometry = screen->virtualGeometry();
    if (virtualGeometry.isEmpty())
        return {};

    QRectF logical = screen->geometry();
    logical.translate(-virtualGeometry.topLeft());
    const qreal sx = qreal(workspace.width()) / virtualGeometry.width();
    const qreal sy = qreal(workspace.height()) / virtualGeometry.height();
    const QRect phys(qRound(logical.x() * sx), qRound(logical.y() * sy),
                     qRound(logical.width() * sx), qRound(logical.height() * sy));
    const QRect bounded = phys.intersected(workspace.rect());
    if (bounded.width() < 2 || bounded.height() < 2) {
        qWarning() << "Portal screenshot crop is empty"
                   << "workspace" << workspace.size()
                   << "virtual" << virtualGeometry
                   << "screen" << screen->name() << screen->geometry()
                   << "crop" << phys;
        return {};
    }

    QImage crop = workspace.copy(bounded);
    crop.setDevicePixelRatio(screen->devicePixelRatio());
    return crop;
}

void CaptureManager::captureAllScreens(const QVector<QScreen *> &screens, MultiCallback cb)
{
    if (screens.isEmpty()) {
        cb({}, QStringLiteral("no screens"));
        return;
    }
    QVector<QPointer<QScreen>> guarded;
    guarded.reserve(screens.size());
    for (QScreen *s : screens)
        guarded.append(QPointer<QScreen>(s));
    if (!m_kwinDenied && KWinScreenShot2::isAvailable()) {
        kwinScreensSerial(std::move(guarded), 0, QVector<QImage>(screens.size()), std::move(cb));
        return;
    }
    portalAllScreens(std::move(guarded), std::move(cb));
}

void CaptureManager::kwinScreensSerial(QVector<QPointer<QScreen>> screens, int index,
                                       QVector<QImage> acc, MultiCallback cb)
{
    if (index >= screens.size()) {
        cb(acc, {});
        return;
    }
    if (!screens[index]) {
        cb({}, QStringLiteral("screen disconnected during capture"));
        return;
    }
    m_kwin->captureScreen(screens[index]->name(), m_settings->includeCursor(),
        [this, screens, index, acc, cb](const QImage &img, const QString &err) mutable {
            if (!err.isEmpty()) {
                qWarning() << "KWin captureScreen failed, portal fallback:" << err;
                if (isKWinAuthError(err))
                    m_kwinDenied = true;
                portalAllScreens(screens, std::move(cb), err);
                return;
            }
            acc[index] = img;
            kwinScreensSerial(screens, index + 1, std::move(acc), std::move(cb));
        });
}

void CaptureManager::portalAllScreens(QVector<QPointer<QScreen>> screens, MultiCallback cb,
                                      const QString &previousError)
{
    // ONE workspace request for all monitors (concurrent portal Screenshot
    // requests race and get denied; N-per-freeze also meant N dialogs).
    m_portal->capture(false, [screens, cb, previousError](const QImage &full, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, combinedError(previousError, err));
            return;
        }
        QVector<QImage> out(screens.size());
        for (int i = 0; i < screens.size(); ++i) {
            if (!screens[i]) {
                cb({}, QStringLiteral("screen disconnected during capture"));
                return;
            }
            out[i] = cropForScreen(full, screens[i]);
            if (out[i].isNull()) {
                cb({}, combinedError(previousError,
                                     QStringLiteral("portal screenshot does not contain screen %1")
                                         .arg(screenLabel(screens[i]))));
                return;
            }
        }
        cb(out, {});
    }, false);
}

void CaptureManager::captureActiveWindow(Callback cb)
{
    if (!m_kwinDenied && KWinScreenShot2::isAvailable()) {
        m_kwin->captureActiveWindow(m_settings->includeCursor(),
            [this, cb](const QImage &img, const QString &err) {
                if (!err.isEmpty()) {
                    qWarning() << "KWin captureActiveWindow failed, portal interactive fallback:" << err;
                    if (isKWinAuthError(err))
                        m_kwinDenied = true;
                    m_portal->capture(true, [cb, err](const QImage &img, const QString &portalErr) {
                        if (!portalErr.isEmpty()) { cb({}, combinedError(err, portalErr)); return; }
                        cb(img, {});
                    }); // interactive: user picks the window
                    return;
                }
                cb(img, {});
            });
        return;
    }
    m_portal->capture(true, std::move(cb));
}

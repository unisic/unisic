#include "CaptureManager.h"
#include "PortalScreenshot.h"
#include "KWinScreenShot2.h"
#include "GnomeScreenshot.h"
#include "GrimScreenshot.h"
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

static QString screenLabel(const CaptureManager::ScreenGeom &s)
{
    const QRect g = s.geometry;
    return QStringLiteral("%1 geometry=%2 dpr=%3")
        .arg(s.name, QStringLiteral("%1,%2 %3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()))
        .arg(s.dpr);
}

CaptureManager::CaptureManager(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_portal(new PortalScreenshot(this))
    , m_kwin(new KWinScreenShot2(this))
    , m_gnome(new GnomeScreenshot(this))
    , m_grim(new GrimScreenshot(this))
{
    grantSilentScreenshotPermission();
}

bool CaptureManager::preferGnome() const
{
    // Only on niri, where the portal is the broken path and niri leaves
    // org.gnome.Shell.Screenshot open. On GNOME proper the interface is locked
    // down, so there GNOME-direct stays a portal *fallback* (see workspaceFallback).
    return GnomeScreenshot::isNiriSession() && GnomeScreenshot::isAvailable();
}

// One whole-workspace capture, routing around whichever backend is broken.
//   niri + grim: grim first (wlr-screencopy — see below), GNOME/portal rescue.
//   niri:        GNOME-direct first, portal rescue, grim rescue.
//   other:       portal first, GNOME-direct rescue, grim rescue.
// grim matters on niri because niri's org.gnome.Shell.Screenshot — which the
// GNOME portal's Screenshot backend merely proxies — hard-fails with
// "internal error" whenever more than one monitor is connected (niri #117);
// wlr-screencopy is per-output and unaffected.
// allowInteractive lets the single-capture paths (fullscreen/one screen) show
// the desktop's own screenshot dialog when the SILENT portal request is denied —
// this is the safety net that keeps capture working on a fresh/unpackaged install
// on non-KDE desktops. The overlay-freeze path passes false (must not pop a dialog).
void CaptureManager::workspaceFallback(Callback cb, bool allowInteractive,
                                       const QString &previousError)
{
    const bool cursor = m_settings->includeCursor();
    const bool gnomeAvail = GnomeScreenshot::isAvailable();
    const bool niri = GnomeScreenshot::isNiriSession();

    // Deepest rescue: wlr-screencopy via grim. When it's absent on niri, say
    // so — installing grim is THE fix for the multi-monitor dead end there.
    auto grimRescue = [this, cursor, cb, niri](const QString &prevErr) {
        if (!GrimScreenshot::isAvailable()) {
            QString msg = prevErr;
            if (niri)
                msg += QStringLiteral("; install 'grim' — niri's own screenshot D-Bus API "
                                      "fails with more than one monitor (niri issue #117)");
            cb({}, msg);
            return;
        }
        m_grim->captureWorkspace(cursor, [cb, prevErr](const QImage &img, const QString &gerr) {
            if (gerr.isEmpty()) { cb(img, {}); return; }
            cb({}, combinedError(prevErr, gerr));
        });
    };

    auto portalThenGnome = [this, cursor, cb, previousError, gnomeAvail, allowInteractive,
                            grimRescue]() {
        m_portal->capture(false, [this, cursor, cb, previousError, gnomeAvail, grimRescue]
                          (const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            if (!gnomeAvail) { grimRescue(combinedError(previousError, err)); return; }
            qWarning() << "Portal workspace capture failed, trying org.gnome.Shell.Screenshot:" << err;
            m_gnome->captureWorkspace(cursor, [cb, previousError, err, grimRescue]
                                      (const QImage &gimg, const QString &gerr) {
                if (!gerr.isEmpty()) { grimRescue(combinedError(previousError, combinedError(err, gerr))); return; }
                cb(gimg, {});
            });
        }, allowInteractive);
    };

    auto gnomeFirst = [this, cursor, cb, portalThenGnome]() {
        m_gnome->captureWorkspace(cursor, [cb, portalThenGnome]
                                  (const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            qWarning() << "niri GNOME-direct capture failed, trying portal:" << err;
            portalThenGnome();
        });
    };

    // niri with grim installed: go straight to the backend that actually
    // works there (silent, no dialog, multi-monitor-safe).
    if (niri && GrimScreenshot::isAvailable()) {
        m_grim->captureWorkspace(cursor, [this, cb, gnomeFirst, portalThenGnome]
                                 (const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            qWarning() << "grim capture failed, trying GNOME/portal chain:" << err;
            if (preferGnome()) gnomeFirst(); else portalThenGnome();
        });
        return;
    }
    if (preferGnome()) {
        gnomeFirst();
        return;
    }
    portalThenGnome();
}

void CaptureManager::portalFallback(Callback cb)
{
    // Single capture (fullscreen / one screen): interactive portal dialog allowed.
    workspaceFallback(std::move(cb), /*allowInteractive=*/true);
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
    // Snapshot the geometry now; the QScreen* must not be dereferenced inside the
    // async callbacks below (the monitor may be gone by the time they run).
    const ScreenGeom geom = snapshotScreen(screen);
    const bool hasScreen = screen != nullptr;

    if (!m_kwinDenied && KWinScreenShot2::isAvailable() && screen) {
        m_kwin->captureScreen(screen->name(), m_settings->includeCursor(),
            [this, geom, cb](const QImage &img, const QString &err) {
                if (!err.isEmpty()) {
                    qWarning() << "KWin captureScreen failed, portal fallback:" << err;
                    if (isKWinAuthError(err))
                        m_kwinDenied = true;
                    // Portal returns the whole workspace: crop to the screen.
                    portalFallback([geom, cb, err](const QImage &full, const QString &e2) {
                        if (!e2.isEmpty()) { cb({}, combinedError(err, e2)); return; }
                        QImage crop = CaptureManager::cropForScreen(full, geom);
                        if (crop.isNull()) {
                            cb({}, combinedError(err, QStringLiteral("portal screenshot does not contain screen %1")
                                                 .arg(screenLabel(geom))));
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
    portalFallback([geom, hasScreen, cb](const QImage &full, const QString &err) {
        if (!err.isEmpty() || !hasScreen) { cb(full, err); return; }
        QImage crop = cropForScreen(full, geom);
        if (crop.isNull()) {
            cb({}, QStringLiteral("portal screenshot does not contain screen %1").arg(screenLabel(geom)));
            return;
        }
        cb(crop, {});
    });
}

CaptureManager::ScreenGeom CaptureManager::snapshotScreen(QScreen *screen)
{
    ScreenGeom g;
    if (screen) {
        g.geometry = screen->geometry();
        g.virtualGeometry = screen->virtualGeometry();
        g.dpr = screen->devicePixelRatio();
        g.name = screen->name();
    }
    return g;
}

QImage CaptureManager::cropForScreen(const QImage &workspace, const ScreenGeom &screen)
{
    if (workspace.isNull())
        return {};

    const QRect virtualGeometry = screen.virtualGeometry;
    if (virtualGeometry.isEmpty())
        return {};

    QRectF logical = screen.geometry;
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
                   << "screen" << screen.name << screen.geometry
                   << "crop" << phys;
        return {};
    }

    QImage crop = workspace.copy(bounded);
    crop.setDevicePixelRatio(screen.dpr);
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
    // Snapshot geometry up front — the QScreen*s must not be dereferenced inside
    // the async callback (a monitor may vanish during the round-trip).
    QVector<ScreenGeom> geoms;
    geoms.reserve(screens.size());
    for (QScreen *s : screens)
        geoms.append(snapshotScreen(s));

    // ONE workspace request for all monitors (concurrent portal Screenshot
    // requests race and get denied; N-per-freeze also meant N dialogs).
    // workspaceFallback picks portal or GNOME-direct (niri) as appropriate.
    // Overlay freeze: never pop an interactive dialog (allowInteractive=false).
    // previousError is prepended once here, so pass {} down to avoid double-nesting.
    workspaceFallback([geoms, cb, previousError](const QImage &full, const QString &err) {
        if (!err.isEmpty()) {
            cb({}, combinedError(previousError, err));
            return;
        }
        QVector<QImage> out(geoms.size());
        for (int i = 0; i < geoms.size(); ++i) {
            out[i] = cropForScreen(full, geoms[i]);
            if (out[i].isNull()) {
                cb({}, combinedError(previousError,
                                     QStringLiteral("screenshot does not contain screen %1")
                                         .arg(screenLabel(geoms[i]))));
                return;
            }
        }
        cb(out, {});
    }, /*allowInteractive=*/false, /*previousError=*/QString());
}

void CaptureManager::captureActiveWindow(Callback cb)
{
    const bool cursor = m_settings->includeCursor();

    // interactive portal (user picks the window) with a GNOME-direct rescue.
    auto portalInteractive = [this, cursor, cb](const QString &prevErr) {
        const bool gnomeAvail = GnomeScreenshot::isAvailable();
        m_portal->capture(true, [this, cursor, cb, prevErr, gnomeAvail]
                          (const QImage &img, const QString &portalErr) {
            if (portalErr.isEmpty()) { cb(img, {}); return; }
            if (!gnomeAvail) { cb({}, combinedError(prevErr, portalErr)); return; }
            m_gnome->captureActiveWindow(cursor, [cb, prevErr, portalErr]
                                         (const QImage &gimg, const QString &gerr) {
                if (!gerr.isEmpty()) { cb({}, combinedError(prevErr, combinedError(portalErr, gerr))); return; }
                cb(gimg, {});
            });
        });
    };

    // niri: the focused window straight from the compositor, portal as rescue.
    if (preferGnome()) {
        m_gnome->captureActiveWindow(cursor, [cb, portalInteractive](const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            qWarning() << "niri GNOME-direct window capture failed, portal fallback:" << err;
            portalInteractive(err);
        });
        return;
    }

    if (!m_kwinDenied && KWinScreenShot2::isAvailable()) {
        m_kwin->captureActiveWindow(cursor,
            [this, cb, portalInteractive](const QImage &img, const QString &err) {
                if (!err.isEmpty()) {
                    qWarning() << "KWin captureActiveWindow failed, portal interactive fallback:" << err;
                    if (isKWinAuthError(err))
                        m_kwinDenied = true;
                    portalInteractive(err);
                    return;
                }
                cb(img, {});
            });
        return;
    }
    portalInteractive({});
}

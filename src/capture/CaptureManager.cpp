#include "CaptureManager.h"
#include "PortalScreenshot.h"
#include "KWinScreenShot2.h"
#include "GnomeScreenshot.h"
#include "GrimScreenshot.h"
#include "Settings.h"
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDebug>
#include <memory>

// Host apps have no sandbox identity: xdg-desktop-portal resolves the app id
// from the systemd launch unit (.desktop launches) or "" (terminal/AppImage
// launches). Since xdg-desktop-portal 1.20 the Registry interface lets a host
// app self-assign its id BEFORE the first portal call, so permissions are
// keyed per-app instead of the anonymous "" bucket. Absent on older portals —
// the async call just fails, harmlessly.
static void registerHostAppId()
{
    if (!qEnvironmentVariable("FLATPAK_ID").isEmpty())
        return; // sandboxed: identity comes from the sandbox metadata
    // NOTE: only the INTERFACE name carries the "host" domain — the object
    // lives on the main portal path (verified live on xdg-desktop-portal
    // 1.22: the /org/freedesktop/host/... path returns "Object does not
    // exist").
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.host.portal.Registry"),
        QStringLiteral("Register"));
    msg << QStringLiteral("org.unisic.Unisic") << QVariantMap{};
    QDBusConnection::sessionBus().asyncCall(msg);
}

// The SILENT portal Screenshot path is gated on the permission store's
// "screenshot" table by BOTH GNOME (43+) and KDE (Plasma 6.4+) — without a
// stored "yes" the first request pops a consent dialog, which the overlay
// freeze must never do. Self-grant it up front (the store is talkable by host
// apps too; this is the documented flameshot recipe). For host launches the
// grant is written both for our registered id and for "" — the key the portal
// uses for unidentifiable launches (terminal, AppImage, xdg-desktop-portal
// < 1.20 without the Registry). Note the "" grant covers every unidentified
// HOST app, not just us — the user installed a screenshot tool; that is the
// established trade-off (flameshot does the same).
static void grantSilentScreenshotPermission()
{
    const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
    QStringList appIds;
    if (!flatpakId.isEmpty())
        appIds << flatpakId;
    else
        appIds << QStringLiteral("org.unisic.Unisic") << QString();
    for (const QString &appId : std::as_const(appIds)) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
            QStringLiteral("/org/freedesktop/impl/portal/PermissionStore"),
            QStringLiteral("org.freedesktop.impl.portal.PermissionStore"),
            QStringLiteral("SetPermission"));
        msg << QStringLiteral("screenshot") << true << QStringLiteral("screenshot")
            << appId << QStringList{QStringLiteral("yes")};
        QDBusConnection::sessionBus().asyncCall(msg);
    }
}

// Session family — chooses the capture chain. KDE has the KWin fast path
// (KWin implements NEITHER screencopy protocol, grim never works there);
// GNOME is portal-only (org.gnome.Shell.Screenshot is allowlisted since 41
// and removed in 49); everything else (sway/river/labwc/Wayfire/Hyprland/
// niri/COSMIC) speaks wlr-screencopy or its ext- successor, i.e. grim.
static bool sessionContains(const char *token)
{
    const auto has = [token](const char *var) {
        return qEnvironmentVariable(var).contains(QLatin1String(token), Qt::CaseInsensitive);
    };
    return has("XDG_CURRENT_DESKTOP") || has("XDG_SESSION_DESKTOP");
}
static bool serviceOnBus(const QString &name)
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(name);
}
// Env vars can be missing (systemd autostart, minimal launchers); the
// compositor's D-Bus name is the reliable signal, so treat either as proof.
// Matters here so grim is not wrongly chosen as PRIMARY on a KWin/Mutter
// session that merely lost XDG_CURRENT_DESKTOP (grim never works there —
// a doomed spawn before the real backend).
static bool isKdeSession()
{
    return sessionContains("KDE") || sessionContains("plasma")
           || serviceOnBus(QStringLiteral("org.kde.KWin"));
}
static bool isGnomeSession()
{
    return sessionContains("GNOME") || serviceOnBus(QStringLiteral("org.gnome.Shell"));
}

static bool isKWinAuthError(const QString &err)
{
    return err.contains(QLatin1String("NoAuthorized"))
           || err.contains(QLatin1String("not authorized"), Qt::CaseInsensitive);
}

// Backend-agnostic: with grim/GNOME/portal all feeding both slots, the old
// "KWin failed: %1; portal failed: %2" template mislabeled errors on desktops
// that have no KWin at all. Fragments self-identify via origin prefixes
// ("KWin:", "portal:", "GNOME Shell:", "grim:") added at their source.
static QString combinedError(const QString &first, const QString &second)
{
    if (first.isEmpty()) return second;
    if (second.isEmpty()) return first;
    return QStringLiteral("%1; then: %2").arg(first, second);
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
    // Order matters: Register must precede any other portal interaction on
    // this connection for the id to stick.
    registerHostAppId();
    grantSilentScreenshotPermission();
}

// A stale WAYLAND_DISPLAY in a genuine X11 session (systemd --user env
// surviving a Wayland→X11 relogin) must not route captures to a dead — or
// worse, ANOTHER session's — compositor. Empty/"tty" stay permissive (sway
// launched from a bare tty).
static bool isX11Session()
{
    return qEnvironmentVariable("XDG_SESSION_TYPE")
               .compare(QLatin1String("x11"), Qt::CaseInsensitive) == 0;
}

// grim as the PRIMARY backend: any Wayland session that isn't KDE (KWin fast
// path, no screencopy) or GNOME (portal-only) — the whole wlroots family plus
// niri and COSMIC. Silent, deterministic, no portal roundtrip.
bool CaptureManager::preferGrim() const
{
    return !isX11Session() && !isKdeSession() && !isGnomeSession()
           && GrimScreenshot::isAvailable();
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

    // Each stage takes the accumulated error chain and whether grim already
    // ran as the primary (re-running it in the rescue would just repeat the
    // same deterministic failure and lose the primary's error).
    auto grimRescue = [this, cursor, cb, niri](const QString &prevErr, bool grimTried) {
        // isX11Session() mirrors preferGrim(): a stale WAYLAND_DISPLAY in a
        // genuine X11 session must not let the rescue capture ANOTHER
        // session's compositor (see the comment above isX11Session()).
        if (grimTried || isX11Session() || !GrimScreenshot::isAvailable()) {
            QString msg = prevErr;
            if (!GrimScreenshot::isAvailable()) {
                // grim missing on a session where it is likely the ONLY
                // working backend — say so.
                if (niri)
                    msg += QStringLiteral("; install 'grim', because niri's own screenshot D-Bus "
                                          "API fails with more than one monitor (niri issue #117)");
                else if (!isX11Session() && !isKdeSession() && !isGnomeSession()
                         && !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty())
                    msg += QStringLiteral("; installing 'grim' usually fixes capture on this "
                                          "desktop (wlr-screencopy works where portals don't)");
            }
            cb({}, msg);
            return;
        }
        m_grim->captureWorkspace(cursor, [cb, prevErr](const QImage &img, const QString &gerr) {
            if (gerr.isEmpty()) { cb(img, {}); return; }
            cb({}, combinedError(prevErr, gerr));
        });
    };

    auto portalThenGnome = [this, cursor, cb, gnomeAvail, allowInteractive, grimRescue]
                           (const QString &prevErr, bool grimTried) {
        m_portal->capture(false, [this, cursor, cb, prevErr, gnomeAvail, grimRescue, grimTried]
                          (const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            // User cancelled the interactive dialog: stop the chain and keep
            // the bare "cancelled" so AppContext's toast suppression matches.
            if (err == QLatin1String("cancelled")) { cb({}, err); return; }
            const QString labeled = QStringLiteral("portal: %1").arg(err);
            if (!gnomeAvail) { grimRescue(combinedError(prevErr, labeled), grimTried); return; }
            qWarning() << "Portal workspace capture failed, trying org.gnome.Shell.Screenshot:" << err;
            m_gnome->captureWorkspace(cursor, [cb, prevErr, labeled, grimRescue, grimTried]
                                      (const QImage &gimg, const QString &gerr) {
                if (!gerr.isEmpty()) {
                    grimRescue(combinedError(prevErr,
                                             combinedError(labeled,
                                                           QStringLiteral("GNOME Shell: %1").arg(gerr))),
                               grimTried);
                    return;
                }
                cb(gimg, {});
            });
        }, allowInteractive);
    };

    auto gnomeFirst = [this, cursor, cb, portalThenGnome](const QString &prevErr, bool grimTried) {
        m_gnome->captureWorkspace(cursor, [cb, portalThenGnome, prevErr, grimTried]
                                  (const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            qWarning() << "niri GNOME-direct capture failed, trying portal:" << err;
            portalThenGnome(combinedError(prevErr, QStringLiteral("GNOME Shell: %1").arg(err)),
                            grimTried);
        });
    };

    // wlroots family / niri / COSMIC with grim installed: go straight to the
    // backend that actually works there (silent, no dialog, no portal).
    if (preferGrim()) {
        m_grim->captureWorkspace(cursor, [this, cb, gnomeFirst, portalThenGnome, previousError]
                                 (const QImage &img, const QString &err) {
            if (err.isEmpty()) { cb(img, {}); return; }
            qWarning() << "grim capture failed, trying GNOME/portal chain:" << err;
            const QString chain = combinedError(previousError, err);
            if (preferGnome()) gnomeFirst(chain, true); else portalThenGnome(chain, true);
        });
        return;
    }
    if (preferGnome()) {
        gnomeFirst(previousError, false);
        return;
    }
    portalThenGnome(previousError, false);
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
                    const QString kerr = QStringLiteral("KWin: %1").arg(err);
                    portalFallback([cb, kerr](const QImage &img, const QString &portalErr) {
                        // Bare "cancelled" must survive uncombined for the toast suppression.
                        if (portalErr == QLatin1String("cancelled")) { cb({}, portalErr); return; }
                        if (!portalErr.isEmpty()) { cb({}, combinedError(kerr, portalErr)); return; }
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
                    const QString kerr = QStringLiteral("KWin: %1").arg(err);
                    portalFallback([geom, cb, kerr](const QImage &full, const QString &e2) {
                        // Bare "cancelled" must survive uncombined for the toast suppression.
                        if (e2 == QLatin1String("cancelled")) { cb({}, e2); return; }
                        if (!e2.isEmpty()) { cb({}, combinedError(kerr, e2)); return; }
                        QImage crop = CaptureManager::cropForScreen(full, geom);
                        if (crop.isNull()) {
                            cb({}, combinedError(kerr, QStringLiteral("portal screenshot does not contain screen %1")
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
    // Per-output grim beats crop-from-workspace: native pixels per screen,
    // no mixed-DPI scale errors. Only useful when Qt runs natively on Wayland
    // (QScreen::name() must be the wl output name — under XWayland it isn't).
    if (preferGrim() && QGuiApplication::platformName() == QLatin1String("wayland")) {
        grimScreensSerial(std::move(guarded), 0, QVector<QImage>(screens.size()), std::move(cb));
        return;
    }
    portalAllScreens(std::move(guarded), std::move(cb));
}

void CaptureManager::grimScreensSerial(QVector<QPointer<QScreen>> screens, int index,
                                       QVector<QImage> acc, MultiCallback cb)
{
    Q_UNUSED(index) Q_UNUSED(acc)
    // All outputs captured in PARALLEL — the overlay freeze latency must not
    // scale with monitor count. Names snapshotted up front (QPointer validity
    // checked once; later unplugs are covered by OverlayController's
    // screenRemoved -> cancel + generation guard).
    QStringList names;
    names.reserve(screens.size());
    for (const QPointer<QScreen> &s : std::as_const(screens)) {
        if (!s) {
            cb({}, QStringLiteral("screen disconnected during capture"));
            return;
        }
        names.append(s->name());
    }
    struct State {
        QVector<QImage> acc;
        int remaining = 0;
        bool failed = false;
        MultiCallback cb;
    };
    auto state = std::make_shared<State>();
    state->acc.resize(screens.size());
    state->remaining = screens.size();
    state->cb = std::move(cb);
    for (int i = 0; i < names.size(); ++i) {
        m_grim->captureOutput(names[i], m_settings->includeCursor(),
            [this, screens, state, i](const QImage &img, const QString &err) {
                if (state->failed)
                    return; // a sibling already failed and triggered the fallback
                if (!err.isEmpty()) {
                    state->failed = true;
                    qWarning() << "grim -o failed, falling back to the workspace path:" << err;
                    portalAllScreens(screens, state->cb, err);
                    return;
                }
                state->acc[i] = img;
                if (--state->remaining == 0)
                    state->cb(state->acc, {});
            });
    }
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
                portalAllScreens(screens, std::move(cb), QStringLiteral("KWin: %1").arg(err));
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
            // User cancellation must reach the caller as the bare string —
            // the toast suppression matches "cancelled" exactly.
            if (err == QLatin1String("cancelled")) {
                cb({}, err);
                return;
            }
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
            // User cancelled the window picker: keep the bare "cancelled"
            // (exact match suppresses the error toast) and skip the rescue.
            if (portalErr == QLatin1String("cancelled")) { cb({}, portalErr); return; }
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

#include "AppContext.h"
#include "theme/ThemeController.h"
#include "theme/IconImageProvider.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QStyleHints>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QTimer>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

// SIGINT/SIGTERM/SIGHUP must run destructors (QSettings flush, temp-file
// cleanup, tray teardown) — the default handlers kill the process cold and
// every settings change since launch is lost. Self-pipe pattern: the handler
// only write()s (async-signal-safe); the notifier quits the event loop.
static int sigQuitFd[2] = {-1, -1};
static void unixSignalHandler(int)
{
    const char one = 1;
    (void)::write(sigQuitFd[1], &one, sizeof(one));
}
static void installSignalHandlers(QCoreApplication *app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sigQuitFd) != 0)
        return; // CLOEXEC: ffmpeg/curl/grim children must not inherit the quit pipe
    auto *notifier = new QSocketNotifier(sigQuitFd[0], QSocketNotifier::Read, app);
    QObject::connect(notifier, &QSocketNotifier::activated, app, [] {
        char tmp;
        (void)::read(sigQuitFd[0], &tmp, sizeof(tmp));
        QCoreApplication::quit();
    });
    struct sigaction sa{};
    sa.sa_handler = unixSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}

static QString singleInstanceServerName()
{
    // Key on UID ALONE — deliberately not on any session/display env var.
    // Those vary between the launch contexts of ONE graphical session
    // (systemd-autostart vs an interactive click vs a compositor keybind spawn
    // often disagree on WAYLAND_DISPLAY/DISPLAY/XDG_SESSION_ID), splitting the
    // app into duplicate instances: double KGlobalAccel dispatch (every hotkey
    // fires twice) and two QSettings writers racing the same config file. One
    // instance per user is the right guarantee; two concurrent graphical
    // sessions of the same user (rare) sharing one instance is the far smaller
    // cost. Dev builds use their own socket so stable and dev can run (and be
    // CLI-targeted) side by side.
#ifdef UNISIC_DEV_BUILD
    return QStringLiteral("app.unisic.UnisicDev.%1").arg(getuid());
#else
    return QStringLiteral("app.unisic.Unisic.%1").arg(getuid());
#endif
}

// Capture flag from argv, mapped to the command sent over the local socket —
// `unisic --region` with a running instance must trigger a capture there, not
// just raise its window.
static QByteArray cliCommand(const QStringList &args)
{
    if (args.contains(QLatin1String("--fullscreen"))) return "fullscreen";
    if (args.contains(QLatin1String("--region"))) return "region";
    if (args.contains(QLatin1String("--window"))) return "window";
    if (args.contains(QLatin1String("--gif"))) return "gif";
    // Autostart path: if an instance is somehow already running, do nothing
    // (never raise its window) — the flag only shapes a FRESH launch.
    if (args.contains(QLatin1String("--tray-only"))) return "tray";
    return "show";
}

static bool notifyExistingInstance(const QString &serverName, const QByteArray &command = "show")
{
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(200))
        return false;
    socket.write(command + "\n");
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

static QLocalServer *createSingleInstanceServer(const QString &serverName, QCoreApplication *app,
                                                const QByteArray &command, bool *handedOff)
{
    *handedOff = false;
    auto *server = new QLocalServer(app);
    if (server->listen(serverName))
        return server;

    // Serialize the recovery below across near-simultaneous spawns (autostart
    // plus a compositor-keybind `unisic --region`): unguarded, one process can
    // removeServer() — an unconditional unlink — the socket another just
    // bound, booting two full instances. QLockFile auto-breaks locks held by
    // dead PIDs, so a crash mid-recovery can't wedge future launches; a failed
    // tryLock (broken tmp) just degrades to the old unserialized behavior.
    QLockFile recoveryLock(QDir::temp().filePath(serverName + QStringLiteral(".lock")));
    recoveryLock.tryLock(2000);

    // If a peer appeared between our probe and listen(), hand off to it — with
    // the REAL command, and signal main() to exit: continuing would boot a
    // duplicate instance with duplicate hotkey registrations (every press
    // would then fire twice). Otherwise remove the stale socket left by a crash.
    if (notifyExistingInstance(serverName, command)) {
        *handedOff = true;
        delete server;
        return nullptr;
    }
    QLocalServer::removeServer(serverName);
    if (server->listen(serverName))
        return server;

    // Still can't bind — most likely a live peer took the name meanwhile, so
    // hand off instead of continuing as an unguarded duplicate. Only the
    // genuinely environmental no-peer case (broken runtime dir) falls through
    // to warn-and-continue.
    if (notifyExistingInstance(serverName, command)) {
        *handedOff = true;
        delete server;
        return nullptr;
    }
    qWarning() << "Could not create single-instance server:" << server->errorString();
    delete server;
    return nullptr;
}

// KWin only allows org.kde.KWin.ScreenShot2 for apps whose installed
// .desktop file declares X-KDE-DBUS-Restricted-Interfaces. When running
// from a build tree, drop the desktop file into ~/.local/share/applications
// so the silent KDE capture path works.
#ifdef UNISIC_DEV_BUILD
// Desaturate keeping alpha — Format_Grayscale8 would drop the alpha channel
// (see the project's Qt gotchas), so convert per-pixel instead.
static QPixmap grayscalePixmap(const QPixmap &src)
{
    QImage img = src.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int g = qGray(line[x]);
            line[x] = qRgba(g, g, g, qAlpha(line[x]));
        }
    }
    return QPixmap::fromImage(img);
}
#endif

static void ensureDesktopFile()
{
    // Dev-run icon: hicolor lookup needs it on disk, not just in qrc.
    const QString iconDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                            + QStringLiteral("/icons/hicolor/scalable/apps");
    const QString iconTarget = iconDir + QStringLiteral("/app.unisic.Unisic.svg");
    const QString legacyIconTarget = iconDir + QStringLiteral("/unisic.svg");
    QDir().mkpath(iconDir);
    if (!QFile::exists(iconTarget))
        QFile::copy(QStringLiteral(":/resources/icons/unisic.svg"), iconTarget);
    if (!QFile::exists(legacyIconTarget))
        QFile::copy(QStringLiteral(":/resources/icons/unisic.svg"), legacyIconTarget);
#ifdef UNISIC_DEV_BUILD
    // The dev app gets its own GRAY icon (menu + window + task switcher), so
    // the two flavors are distinguishable at a glance.
    const QString devIconDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                               + QStringLiteral("/icons/hicolor/256x256/apps");
    const QString devIconTarget = devIconDir + QStringLiteral("/app.unisic.UnisicDev.png");
    QDir().mkpath(devIconDir);
    if (!QFile::exists(devIconTarget)) {
        const QPixmap gray =
            grayscalePixmap(QIcon(QStringLiteral(":/resources/icons/unisic.svg")).pixmap(256));
        gray.save(devIconTarget, "PNG");
    }
#endif

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString target = dir + QLatin1Char('/') + QGuiApplication::desktopFileName()
                           + QStringLiteral(".desktop");
    // Pre-rename installs dropped org.unisic.Unisic.desktop here; left behind
    // it shows up as a second "Unisic" menu entry.
    QFile::remove(dir + QStringLiteral("/org.unisic.Unisic.desktop"));
#ifdef UNISIC_DEV_BUILD
    // Older dev builds wrote app.unisic.Unisic.desktop here with Exec pointing
    // into the build tree — since ~/.local shadows /usr/share, the menu's
    // "Unisic" then launched the DEV binary instead of the installed stable
    // one. Remove that shadow when its Exec points at this build (or at a
    // binary that no longer exists); a genuine user-made local entry pointing
    // at some other existing binary is left alone.
    {
        const QString shadow = dir + QStringLiteral("/app.unisic.Unisic.desktop");
        QFile sf(shadow);
        if (sf.exists() && sf.open(QIODevice::ReadOnly)) {
            QString exec;
            const QList<QByteArray> shadowLines = sf.readAll().split('\n');
            for (const QByteArray &line : shadowLines) {
                if (!line.startsWith("Exec="))
                    continue;
                exec = QString::fromUtf8(line.mid(5)).trimmed();
                if (exec.startsWith(QLatin1Char('"'))) {
                    const int end = exec.indexOf(QLatin1Char('"'), 1);
                    exec = end > 0 ? exec.mid(1, end - 1) : QString();
                } else {
                    exec = exec.section(QLatin1Char(' '), 0, 0);
                }
                break;
            }
            sf.close();
            if (exec == QCoreApplication::applicationFilePath()
                || (exec.startsWith(QLatin1Char('/')) && !QFile::exists(exec))) {
                QFile::remove(shadow);
                qInfo() << "Removed stale dev shadow of the stable desktop entry:" << shadow;
            }
        }
    }
#endif
    // Quote per the Desktop Entry spec — an unquoted build path with spaces
    // yields an invalid entry and silently breaks ScreenShot2 authorization.
    QString execPath = QCoreApplication::applicationFilePath();
    execPath.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
            .replace(QLatin1Char('"'), QLatin1String("\\\""));
    const QByteArray execLine = "Exec=\"" + execPath.toUtf8() + "\"\n";
#ifdef UNISIC_DEV_BUILD
    // ABSOLUTE path, not a theme name: themed lookup for the freshly-dropped
    // gray dev icon proved unreliable against stale icon caches
    // (icon-theme.cache / icon-cache.kcache) — an absolute Icon= bypasses
    // theme resolution entirely.
    const QByteArray iconLine = "Icon=" + devIconTarget.toUtf8() + "\n";
#else
    const QByteArray iconLine =
        "Icon=" + QGuiApplication::desktopFileName().toUtf8() + "\n";
#endif
    const QByteArray restrictedLine =
        "X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\n";

    // Rewrite when missing OR stale (Exec pointing at an old build path) —
    // KWin matches the caller's /proc/<pid>/exe against the desktop Exec, so a
    // stale path silently breaks ScreenShot2 authorization.
    if (QFile::exists(target)) {
        QFile existing(target);
        if (existing.open(QIODevice::ReadOnly)) {
            const QByteArray data = existing.readAll();
            if (data.contains(execLine) && data.contains(iconLine) && data.contains(restrictedLine))
                return;
        }
    }
    QDir().mkpath(dir);
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write("[Desktop Entry]\n"
            "Type=Application\n"
            "Name=" + QGuiApplication::applicationDisplayName().toUtf8() + "\n"
            "Comment=Screenshots, annotations, uploads and GIF recording\n"
            + execLine +
            iconLine +
            "Terminal=false\n"
            "Categories=Utility;Graphics;\n"
            + restrictedLine);
    f.close();
    qInfo() << "Installed" << target << "(enables silent KWin capture)";

    // KWin reads restricted-interface grants from KDE's service cache (ksycoca),
    // which doesn't index a just-written desktop file until re-login. Rebuild it
    // now so silent capture can work in this very session.
    const QString sycoca = QStandardPaths::findExecutable(QStringLiteral("kbuildsycoca6"));
    if (!sycoca.isEmpty()) {
        // Detached: blocking up to 5 s here stalls first-run startup; if the
        // rebuild isn't done for the very first capture, the KWin path just
        // falls back to the portal once.
        QProcess::startDetached(sycoca, {});
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv); // QApplication: needed for QSystemTrayIcon/QMenu
    // Dev builds are a SEPARATE app: own application name (which moves every
    // QStandardPaths location: cache, history, U-2-Net model), own desktop id,
    // own single-instance socket, own config dir (ConfigPath.h) and own
    // KGlobalAccel component (GlobalHotkeys.h) — so a build-tree binary never
    // shadows or fights the installed stable Unisic.
#ifdef UNISIC_DEV_BUILD
    app.setApplicationName(QStringLiteral("unisic-dev"));
    app.setApplicationDisplayName(QStringLiteral("Unisic (dev)"));
    app.setDesktopFileName(QStringLiteral("app.unisic.UnisicDev"));
#else
    app.setApplicationName(QStringLiteral("unisic"));
    app.setApplicationDisplayName(QStringLiteral("Unisic"));
    app.setDesktopFileName(QStringLiteral("app.unisic.Unisic"));
#endif
    app.setApplicationVersion(QStringLiteral(UNISIC_VERSION));
    app.setOrganizationName(QStringLiteral("Unisic"));
#ifdef UNISIC_DEV_BUILD
    // Gray window icon = dev build, at a glance.
    app.setWindowIcon(QIcon(
        grayscalePixmap(QIcon(QStringLiteral(":/resources/icons/unisic.svg")).pixmap(256))));
#else
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/icons/unisic.svg")));
#endif
    app.setQuitOnLastWindowClosed(false); // lives in the tray

    const QStringList args = app.arguments();
    const bool settingsBatchMode = args.contains(QLatin1String("--export-settings"))
                                   || args.contains(QLatin1String("--import-settings"));

    // Batch modes run headless and exit — never boot the tray/QML/hotkeys and
    // never race a running instance's single-instance socket.
    if (settingsBatchMode) {
        // Headless: the QML engine normally instantiates the ThemeController
        // singleton — without one, themeName would silently drop out of
        // export/import.
        ThemeController batchTheme;
        AppContext batchContext;
        const int i = args.indexOf(QLatin1String("--export-settings"));
        const int j = args.indexOf(QLatin1String("--import-settings"));
        QString err;
        if (i >= 0) {
            if (i + 1 >= args.size()) {
                qWarning() << "--export-settings requires a file path";
                return 2;
            }
            err = batchContext.exportSettings(QUrl::fromLocalFile(args[i + 1]));
        } else if (j >= 0) {
            if (j + 1 >= args.size()) {
                qWarning() << "--import-settings requires a file path";
                return 2;
            }
            err = batchContext.importSettings(QUrl::fromLocalFile(args[j + 1]));
            if (err.isEmpty())
                qInfo() << "Settings imported — restart a running Unisic instance to apply them.";
        }
        if (!err.isEmpty()) {
            qWarning() << err;
            return 1;
        }
        return 0;
    }

    // Batch mode above runs without an event loop — the self-pipe notifier
    // would never fire there; default signal dispositions are correct for it.
    installSignalHandlers(&app);

    const QString serverName = singleInstanceServerName();
    if (notifyExistingInstance(serverName, cliCommand(args)))
        return 0;
    bool handedOff = false;
    QLocalServer *singleInstanceServer =
        createSingleInstanceServer(serverName, &app, cliCommand(args), &handedOff);
    if (handedOff)
        return 0;
    if (singleInstanceServer) {
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                         [serverName] { QLocalServer::removeServer(serverName); });
    }

    QQuickStyle::setStyle(QStringLiteral("Basic")); // fully custom look, no platform theme

    // Resolve missing draw-* names against Breeze so the "System" theme still
    // gets tool icons under desktop themes that lack them.
    const bool dark = QGuiApplication::styleHints()
                      && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QIcon::setFallbackThemeName(dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze"));
    // Re-pin on runtime light/dark flips — a fallback frozen at startup keeps
    // serving the old scheme's glyphs (dark-on-dark = invisible icons) even
    // though ThemeController bumps rev and every icon re-fetches. Direct
    // connection runs during signal emission, BEFORE ThemeController's queued
    // rev bump, so the re-fetch resolves against the corrected fallback
    // (setFallbackThemeName also flushes Qt's icon-loader cache).
    if (auto *hints = QGuiApplication::styleHints()) {
        QObject::connect(hints, &QStyleHints::colorSchemeChanged, &app,
                         [](Qt::ColorScheme scheme) {
            QIcon::setFallbackThemeName(scheme == Qt::ColorScheme::Dark
                                            ? QStringLiteral("breeze-dark")
                                            : QStringLiteral("breeze"));
        });
    }

    // The desktop file exists solely for KWin's ScreenShot2 authorization —
    // skip the app-grid pollution (with an Exec that goes stale on every
    // build-tree move / AppImage remount) on other desktops. Detect KWin by
    // its D-Bus name, not XDG_CURRENT_DESKTOP — the env var is missing on
    // systemd-autostart, where gating on it would skip the authz setup on a
    // real KDE session.
    // NOT when running as an AppImage: the user manages desktop integration
    // with a dedicated tool (AppImageLauncher, LeverGear, Gear Lever), and the
    // app dropping its own .desktop fights that — a duplicate menu entry whose
    // Exec points at the transient FUSE mount path, stale the moment the
    // AppImage moves or the mount changes. (It only ever bought silent KWin
    // ScreenShot2 authz, which was already unreliable for an AppImage since
    // KWin matches /proc/pid/exe against the .desktop Exec and the mount path
    // differs every run; the portal path still works.)
    if (qEnvironmentVariable("APPIMAGE").isEmpty()) {
        auto *bi = QDBusConnection::sessionBus().interface();
        const bool kwin = bi && bi->isServiceRegistered(QStringLiteral("org.kde.KWin"));
        if (kwin
            || qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("KDE"),
                                                                    Qt::CaseInsensitive))
            ensureDesktopFile();
    }

    // ThemeController is a module QML singleton (engine-created); the icon
    // provider shares that instance lazily via ThemeController::instance().
    AppContext context;
    // Install the UI-language translators BEFORE the engine loads, so every
    // qsTr in the QML is resolved against the chosen language on first paint.
    context.applyLanguage();
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider(nullptr));
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &context);
    // Autostart path: `unisic --tray-only` boots straight into the tray with no
    // main window (Main.qml binds `visible: !startHidden`). A manual `unisic`
    // still shows the window. Set BEFORE load so there is no visible flash.
    const bool trayOnly = args.contains(QLatin1String("--tray-only"));
    engine.rootContext()->setContextProperty(QStringLiteral("startHidden"), trayOnly);
    context.initialize(&engine);

    if (singleInstanceServer) {
        QObject::connect(singleInstanceServer, &QLocalServer::newConnection, &context,
                         [singleInstanceServer, &context] {
            while (QLocalSocket *socket = singleInstanceServer->nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                // Dispatch the forwarded CLI action (`unisic --region` with a
                // running instance must capture, not just raise the window).
                auto dispatch = [socket, &context] {
                    const QByteArray cmd = socket->readAll().trimmed();
                    if (cmd.isEmpty())
                        return; // second delivery (readyRead after manual call)
                    if (cmd == "fullscreen") context.captureFullScreen();
                    else if (cmd == "region") context.captureRegion();
                    else if (cmd == "window") context.captureWindow();
                    else if (cmd == "gif") context.startGifRegion();
                    else if (cmd == "tray") { /* already running — stay in tray */ }
                    else QMetaObject::invokeMethod(&context, "showMainWindowRequested",
                                                   Qt::QueuedConnection);
                    socket->disconnectFromServer();
                };
                QObject::connect(socket, &QLocalSocket::readyRead, &context, dispatch);
                if (socket->bytesAvailable() > 0)
                    dispatch();
            }
        });
    }

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/Main.qml")));

    // ShareX-style CLI triggers: unisic --region | --fullscreen | --window | --gif
    QTimer::singleShot(300, &context, [&context, args] {
        if (args.contains(QLatin1String("--fullscreen"))) context.captureFullScreen();
        else if (args.contains(QLatin1String("--region"))) context.captureRegion();
        else if (args.contains(QLatin1String("--window"))) context.captureWindow();
        else if (args.contains(QLatin1String("--gif"))) context.startGifRegion();
    });

    // Safety net for --tray-only: if this desktop has no system-tray host AT ALL
    // (GNOME without AppIndicator, bare wlroots), a hidden start would be
    // unreachable, so reveal the window. But a cold-boot login — exactly when
    // autostart fires — can take many seconds for plasmashell/waybar to register
    // the StatusNotifier host, and setupTray()'s watcher waits for it. So DON'T
    // reveal on a short fixed delay (that popped the window mid-login); use a
    // generous deadline AND cancel it the moment a tray actually appears.
    if (trayOnly && !context.trayAvailable()) {
        auto *reveal = new QTimer(&context);
        reveal->setSingleShot(true);
        reveal->setInterval(30000);
        QObject::connect(reveal, &QTimer::timeout, &context, [&context, reveal] {
            reveal->deleteLater();
            if (!context.trayAvailable())
                QMetaObject::invokeMethod(&context, "showMainWindowRequested",
                                          Qt::QueuedConnection);
        });
        // Tray showed up in time — no reveal needed.
        QObject::connect(&context, &AppContext::trayAvailableChanged, reveal, [reveal] {
            reveal->stop();
            reveal->deleteLater();
        });
        reveal->start();
    }

    return app.exec();
}

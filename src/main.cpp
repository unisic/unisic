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
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QTimer>
#include <QDebug>
#include <unistd.h>

static QString singleInstanceServerName()
{
    return QStringLiteral("org.unisic.Unisic.%1").arg(getuid());
}

static bool notifyExistingInstance(const QString &serverName)
{
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(200))
        return false;
    socket.write("show\n");
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

static QLocalServer *createSingleInstanceServer(const QString &serverName, QCoreApplication *app)
{
    auto *server = new QLocalServer(app);
    if (server->listen(serverName))
        return server;

    // If a peer appeared between our probe and listen(), hand off to it instead
    // of deleting a live socket. Otherwise remove the stale socket left by a crash.
    if (notifyExistingInstance(serverName)) {
        delete server;
        return nullptr;
    }
    QLocalServer::removeServer(serverName);
    if (server->listen(serverName))
        return server;

    qWarning() << "Could not create single-instance server:" << server->errorString();
    delete server;
    return nullptr;
}

// KWin only allows org.kde.KWin.ScreenShot2 for apps whose installed
// .desktop file declares X-KDE-DBUS-Restricted-Interfaces. When running
// from a build tree, drop the desktop file into ~/.local/share/applications
// so the silent KDE capture path works.
static void ensureDesktopFile()
{
    // Dev-run icon: hicolor lookup needs it on disk, not just in qrc.
    const QString iconDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                            + QStringLiteral("/icons/hicolor/scalable/apps");
    const QString iconTarget = iconDir + QStringLiteral("/org.unisic.Unisic.svg");
    const QString legacyIconTarget = iconDir + QStringLiteral("/unisic.svg");
    QDir().mkpath(iconDir);
    if (!QFile::exists(iconTarget))
        QFile::copy(QStringLiteral(":/resources/icons/unisic.svg"), iconTarget);
    if (!QFile::exists(legacyIconTarget))
        QFile::copy(QStringLiteral(":/resources/icons/unisic.svg"), legacyIconTarget);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString target = dir + QStringLiteral("/org.unisic.Unisic.desktop");
    const QByteArray execLine = "Exec=" + QCoreApplication::applicationFilePath().toUtf8() + "\n";
    const QByteArray iconLine = "Icon=org.unisic.Unisic\n";
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
            "Name=Unisic\n"
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
        QProcess p;
        p.start(sycoca, {});
        p.waitForFinished(5000);
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv); // QApplication: needed for QSystemTrayIcon/QMenu
    app.setApplicationName(QStringLiteral("unisic"));
    app.setApplicationVersion(QStringLiteral(UNISIC_VERSION));
    app.setOrganizationName(QStringLiteral("Unisic"));
    app.setApplicationDisplayName(QStringLiteral("Unisic"));
    app.setDesktopFileName(QStringLiteral("org.unisic.Unisic"));
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/icons/unisic.svg")));
    app.setQuitOnLastWindowClosed(false); // lives in the tray

    const QStringList args = app.arguments();
    const bool settingsBatchMode = args.contains(QLatin1String("--export-settings"))
                                   || args.contains(QLatin1String("--import-settings"));
    const QString serverName = singleInstanceServerName();
    if (!settingsBatchMode && notifyExistingInstance(serverName))
        return 0;
    QLocalServer *singleInstanceServer = settingsBatchMode
                                             ? nullptr
                                             : createSingleInstanceServer(serverName, &app);
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

    ensureDesktopFile();

    // ThemeController is a module QML singleton (engine-created); the icon
    // provider shares that instance lazily via ThemeController::instance().
    AppContext context;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider(nullptr));
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &context);
    context.initialize(&engine);

    if (singleInstanceServer) {
        QObject::connect(singleInstanceServer, &QLocalServer::newConnection, &context,
                         [singleInstanceServer, &context] {
            while (QLocalSocket *socket = singleInstanceServer->nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                socket->readAll();
                QMetaObject::invokeMethod(&context, "showMainWindowRequested", Qt::QueuedConnection);
                socket->disconnectFromServer();
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
        else if (int i = args.indexOf(QLatin1String("--export-settings")); i >= 0 && i + 1 < args.size()) {
            const QString err = context.exportSettings(QUrl::fromLocalFile(args[i + 1]));
            if (!err.isEmpty()) qWarning() << err;
            QCoreApplication::exit(err.isEmpty() ? 0 : 1);
        } else if (int j = args.indexOf(QLatin1String("--import-settings")); j >= 0 && j + 1 < args.size()) {
            const QString err = context.importSettings(QUrl::fromLocalFile(args[j + 1]));
            if (!err.isEmpty()) qWarning() << err;
            QCoreApplication::exit(err.isEmpty() ? 0 : 1);
        }
    });

    return app.exec();
}

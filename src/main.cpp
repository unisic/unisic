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
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QTimer>
#include <QDebug>

// KWin only allows org.kde.KWin.ScreenShot2 for apps whose installed
// .desktop file declares X-KDE-DBUS-Restricted-Interfaces. When running
// from a build tree, drop the desktop file into ~/.local/share/applications
// so the silent KDE capture path works.
static void ensureDesktopFile()
{
    // Dev-run icon: hicolor lookup needs it on disk, not just in qrc.
    const QString iconDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                            + QStringLiteral("/icons/hicolor/scalable/apps");
    const QString iconTarget = iconDir + QStringLiteral("/unisic.svg");
    if (!QFile::exists(iconTarget)) {
        QDir().mkpath(iconDir);
        QFile::copy(QStringLiteral(":/resources/icons/unisic.svg"), iconTarget);
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    const QString target = dir + QStringLiteral("/org.unisic.Unisic.desktop");
    const QByteArray execLine = "Exec=" + QCoreApplication::applicationFilePath().toUtf8() + "\n";
    const QByteArray restrictedLine =
        "X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2\n";

    // Rewrite when missing OR stale (Exec pointing at an old build path) —
    // KWin matches the caller's /proc/<pid>/exe against the desktop Exec, so a
    // stale path silently breaks ScreenShot2 authorization.
    if (QFile::exists(target)) {
        QFile existing(target);
        if (existing.open(QIODevice::ReadOnly)) {
            const QByteArray data = existing.readAll();
            if (data.contains(execLine) && data.contains(restrictedLine))
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
            "Icon=unisic\n"
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
    app.setOrganizationName(QStringLiteral("Unisic"));
    app.setApplicationDisplayName(QStringLiteral("Unisic"));
    app.setDesktopFileName(QStringLiteral("org.unisic.Unisic"));
    app.setWindowIcon(QIcon(QStringLiteral(":/resources/icons/unisic.svg")));
    app.setQuitOnLastWindowClosed(false); // lives in the tray

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

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/Main.qml")));

    // ShareX-style CLI triggers: unisic --region | --fullscreen | --window | --gif
    const QStringList args = app.arguments();
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

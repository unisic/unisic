#include "KWinForceBlur.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>

bool KWinForceBlur::loaded()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(QStringLiteral("org.kde.KWin")))
        return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
        QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("isEffectLoaded"));
    msg << QStringLiteral("forceblur");
    const QDBusReply<bool> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 500);
    return reply.isValid() && reply.value();
}

QString KWinForceBlur::verdict(const QStringList &kwinrcFiles, const QString &appId)
{
    // The effect's own kcfg: group [Effect-blurplus], BlurMatching=true (its
    // default) blurs the classes in WindowClasses, false blurs every window
    // NOT listed. The list is newline-separated; KConfig stores the newline
    // as the two characters "\n". First file that has a key wins.
    bool whitelist = true;
    QStringList classes;
    bool haveMode = false, haveList = false;
    for (const QString &file : kwinrcFiles) {
        QSettings rc(file, QSettings::IniFormat);
        rc.beginGroup(QStringLiteral("Effect-blurplus"));
        if (!haveMode && rc.contains(QStringLiteral("BlurMatching"))) {
            whitelist = rc.value(QStringLiteral("BlurMatching")).toBool();
            haveMode = true;
        }
        if (!haveList && rc.contains(QStringLiteral("WindowClasses"))) {
            QString raw = rc.value(QStringLiteral("WindowClasses")).toString();
            raw.replace(QLatin1String("\\n"), QLatin1String("\n"));
            for (const QString &line : raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                classes << line.trimmed();
            haveList = true;
        }
    }

    // KWin names a Wayland window after the executable ("unisic") and, once
    // the app id arrives, after that too; the effect matches either, exactly.
    const bool listed = classes.contains(QStringLiteral("unisic")) || classes.contains(appId);
    if (whitelist && listed)
        return QStringLiteral("Better Blur lists Unisic among the windows it blurs");
    if (!whitelist && !listed)
        return QStringLiteral("Better Blur blurs every window not on its list, and Unisic is not on it");
    return {};
}

QString KWinForceBlur::blursThisApp()
{
    if (!loaded())
        return {};
    // User kwinrc first, then the distro's defaults.
    return verdict(QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                             QStringLiteral("kwinrc")),
                   QGuiApplication::desktopFileName());
}

#include "KWinForceBlur.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

QString KWinForceBlur::loadedGroup()
{
    auto *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(QStringLiteral("org.kde.KWin")))
        return {};
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.kde.kwin.Effects") << QStringLiteral("loadedEffects");
    const QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 500);
    if (!reply.isValid())
        return {};
    // DX has no Id in its metadata, so KWin names it after the .so.
    const QStringList loaded = reply.value().variant().toStringList();
    if (loaded.contains(QStringLiteral("better_blur_dx")))
        return QStringLiteral("Effect-better-blur-dx");
    if (loaded.contains(QStringLiteral("forceblur")))
        return QStringLiteral("Effect-blurplus");
    return {};
}

QString KWinForceBlur::verdict(const QStringList &kwinrcFiles, const QString &group,
                               const QStringList &names, bool dock)
{
    // Both forks' kcfg: BlurMatching=true (the default) blurs the listed
    // classes, false every window NOT listed; BlurDocks=false (the default)
    // never blurs a dock. The list is newline-separated (KConfig stores the
    // newline as the two characters "\n"), and DX reads a /line/ as a regex.
    const auto value = [&](const QString &key, const QVariant &fallback) {
        for (const QString &file : kwinrcFiles) {
            QSettings rc(file, QSettings::IniFormat);
            rc.beginGroup(group);
            if (rc.contains(key))
                return rc.value(key);
        }
        return fallback;
    };
    if (dock && !value(QStringLiteral("BlurDocks"), false).toBool())
        return {};
    const bool whitelist = value(QStringLiteral("BlurMatching"), true).toBool();
    QString raw = value(QStringLiteral("WindowClasses"), QString()).toString();
    raw.replace(QLatin1String("\\n"), QLatin1String("\n"));

    const auto matches = [&names](const QString &line) {
        if (line.size() > 2 && line.startsWith(QLatin1Char('/')) && line.endsWith(QLatin1Char('/'))) {
            const QRegularExpression re(line.mid(1, line.size() - 2));
            return re.isValid() && std::any_of(names.cbegin(), names.cend(), [&re](const QString &n) {
                return re.match(n).hasMatch();
            });
        }
        return names.contains(line);
    };
    const QStringList classes = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const bool listed = std::any_of(classes.cbegin(), classes.cend(), [&matches](const QString &line) {
        return matches(line.trimmed());
    });
    const QString docks = dock ? QStringLiteral(" (docks included)") : QString();
    if (whitelist && listed)
        return QStringLiteral("Better Blur lists Unisic among the windows it blurs") + docks;
    if (!whitelist && !listed)
        return QStringLiteral("Better Blur blurs every window not on its list, and Unisic is not on it") + docks;
    return {};
}

QString KWinForceBlur::blursThisApp(bool dock)
{
    const QString group = loadedGroup();
    if (group.isEmpty())
        return {};
    // KWin names a layer surface after the client's executable only; a
    // toplevel also goes by its class (the app id).
    QStringList names{QFileInfo(QCoreApplication::applicationFilePath()).fileName()};
    if (!dock)
        names << QGuiApplication::desktopFileName();
    // User kwinrc first, then the distro's defaults.
    return verdict(QStandardPaths::locateAll(QStandardPaths::GenericConfigLocation,
                                             QStringLiteral("kwinrc")),
                   group, names, dock);
}

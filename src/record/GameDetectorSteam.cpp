#include "GameDetector.h"
#include "SteamLaunch.h"

#include <QDir>
#include <QFile>

// Steam starts every game, native or Proton, as "reaper SteamLaunch
// AppId=<id> -- <game>", and the reaper lives exactly as long as the game.
// Only a process named "reaper" has its cmdline read.
bool GameDetector::steamGameRunning()
{
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (!pid.front().isDigit())
            continue;
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        if (!comm.open(QIODevice::ReadOnly) || comm.readAll().trimmed() != "reaper")
            continue;
        QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (cmdline.open(QIODevice::ReadOnly) && SteamLaunch::appId(cmdline.readAll()) > 0)
            return true;
    }
    return false;
}

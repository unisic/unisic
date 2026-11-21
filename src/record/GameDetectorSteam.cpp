#include "GameDetector.h"
#include "GameMatch.h"
#include "SteamLaunch.h"

#include <QDir>
#include <QFile>
#include <QHash>

namespace {
QByteArray readSmall(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(64 * 1024) : QByteArray();
}

// A Steam AppId's name from its appmanifest, in whichever library holds it.
// Cached: the same game is asked for every 5 s while it runs.
QString steamName(int appId)
{
    static QHash<int, QString> cache; // shortcut: grows by one entry per game played this session
    if (const auto it = cache.constFind(appId); it != cache.cend())
        return *it;
    const QString home = QDir::homePath();
    QStringList roots{home + QStringLiteral("/.local/share/Steam"), home + QStringLiteral("/.steam/steam"),
                      home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")};
    for (const QString &root : QStringList(roots))
        roots << GameMatch::libraryPaths(readSmall(root + QStringLiteral("/steamapps/libraryfolders.vdf")));
    QString name;
    for (const QString &root : std::as_const(roots)) {
        name = GameMatch::manifestName(
            readSmall(root + QStringLiteral("/steamapps/appmanifest_%1.acf").arg(appId)));
        if (!name.isEmpty())
            break;
    }
    cache.insert(appId, name);
    return name;
}
}

// Steam starts every game, native or Proton, as "reaper SteamLaunch
// AppId=<id> -- <game>", and the reaper lives exactly as long as the game.
// Only processes whose comm names a candidate have their cmdline read.
GameDetector::Probe GameDetector::probe()
{
    Probe found;
    QString cloud;
    qint64 cloudPid = 0;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (!pid.front().isDigit())
            continue;
        const QByteArray comm = readSmall(QStringLiteral("/proc/%1/comm").arg(pid)).trimmed();
        if (comm == "reaper") {
            const int appId = SteamLaunch::appId(readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid)));
            if (appId > 0)
                return {true, steamName(appId), pid.toLongLong()}; // Steam knows best: stop here
        } else if (comm.startsWith("python") && found.name.isEmpty()) {
            const QString exe = GameMatch::protonGame(readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid)));
            if (!exe.isEmpty())
                found = {true, exe, pid.toLongLong()};
        } else if (cloud.isEmpty()) {
            cloud = GameMatch::cloudClient(comm);
            if (cloud.isEmpty() && GameMatch::isBrowser(comm))
                cloud = GameMatch::cloudPage(readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid)));
            if (!cloud.isEmpty())
                cloudPid = pid.toLongLong();
        }
    }
    if (!found.running && !cloud.isEmpty())
        found = {true, cloud, cloudPid};
    return found;
}

// root and every process under it, from the ppid in each /proc/<pid>/stat
// (field 4, after the parenthesised comm, which may itself hold spaces).
QList<qint64> GameDetector::withDescendants(qint64 root)
{
    QMultiHash<qint64, qint64> children;
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (!pid.front().isDigit())
            continue;
        const QByteArray stat = readSmall(QStringLiteral("/proc/%1/stat").arg(pid));
        const QList<QByteArray> rest = stat.mid(stat.lastIndexOf(')') + 2).split(' ');
        if (rest.size() > 1)
            children.insert(rest.at(1).toLongLong(), pid.toLongLong());
    }
    QList<qint64> out{root};
    for (int i = 0; i < out.size() && out.size() < 4096; ++i)
        out << children.values(out.at(i));
    return out;
}

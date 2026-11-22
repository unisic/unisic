#include "GameDetector.h"
#include "GameMatch.h"
#include "SteamLaunch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace {
QByteArray readSmall(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(64 * 1024) : QByteArray();
}

// A Steam AppId's name from its appmanifest, in whichever library holds it,
// and its art from the client's cache. Cached: the same game is asked for
// every 5 s while it runs.
struct SteamApp
{
    QString name;
    QString cover;
};
SteamApp steamApp(int appId)
{
    static QHash<int, SteamApp> cache; // shortcut: grows by one entry per game played this session
    if (const auto it = cache.constFind(appId); it != cache.cend())
        return *it;
    const QString home = QDir::homePath();
    const QStringList clients{home + QStringLiteral("/.local/share/Steam"), home + QStringLiteral("/.steam/steam"),
                              home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")};
    QStringList roots = clients;
    for (const QString &root : clients)
        roots << GameMatch::libraryPaths(readSmall(root + QStringLiteral("/steamapps/libraryfolders.vdf")));
    SteamApp app;
    for (const QString &root : std::as_const(roots)) {
        app.name = GameMatch::manifestName(
            readSmall(root + QStringLiteral("/steamapps/appmanifest_%1.acf").arg(appId)));
        if (!app.name.isEmpty())
            break;
    }
    for (const QString &root : clients) {
        app.cover = GameMatch::steamCover(root, appId);
        if (!app.cover.isEmpty())
            break;
    }
    cache.insert(appId, app);
    return app;
}

// The game GeForce NOW's native client streams, from its own log (GameMatch::
// gfnGame), read incrementally: only what was appended since the last poll.
// Sets `known` false when there is no log to ask (another install, a client
// that stopped writing it); the client then counts as the game, as before.
QString gfnStreaming(bool &known)
{
    struct State
    {
        QString path;
        QByteArray head; // the first line: a new client run starts a new log
        qint64 offset = 0;
        QString game;
    };
    static State state;
    const QString home = QDir::homePath();
    QString path;
    for (const QString &candidate :
         {home + QStringLiteral("/.var/app/com.nvidia.geforcenow/.local/state/NVIDIA/GeForceNOW/CxNative_GeForceNOW.log"),
          home + QStringLiteral("/.local/state/NVIDIA/GeForceNOW/CxNative_GeForceNOW.log")}) {
        if (QFileInfo::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    QFile log(path);
    known = !path.isEmpty() && log.open(QIODevice::ReadOnly);
    if (!known)
        return {};
    const QByteArray head = log.read(64);
    if (path != state.path || head != state.head || log.size() < state.offset)
        state = {path, head, 0, {}};
    // At most 4 MB per poll: a huge log is caught up over a few polls.
    log.seek(state.offset);
    QByteArray chunk = log.read(qMin<qint64>(log.size() - state.offset, 4 << 20));
    const qsizetype end = chunk.lastIndexOf('\n');
    if (end < 0)
        return state.game;
    chunk.truncate(end + 1); // a half-written last line waits for the next poll
    state.offset += end + 1;
    state.game = GameMatch::gfnGame(chunk, state.game);
    return state.game;
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
    bool nativeClient = false; // cloud came from a client's comm, not a browser page
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        if (!pid.front().isDigit())
            continue;
        const QByteArray comm = readSmall(QStringLiteral("/proc/%1/comm").arg(pid)).trimmed();
        if (comm == "reaper") {
            const int appId = SteamLaunch::appId(readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid)));
            if (appId > 0) {
                const SteamApp app = steamApp(appId);
                return {true, app.name, pid.toLongLong(), app.cover}; // Steam knows best: stop here
            }
        } else if (comm.startsWith("python") && found.name.isEmpty()) {
            const QString exe = GameMatch::protonGame(readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid)));
            if (!exe.isEmpty())
                found = {true, exe, pid.toLongLong()};
        } else if (cloud.isEmpty()) {
            cloud = GameMatch::cloudClient(comm);
            nativeClient = !cloud.isEmpty();
            if (cloud.isEmpty() && GameMatch::isBrowser(comm))
                cloud = GameMatch::cloudPage(readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid)));
            if (!cloud.isEmpty())
                cloudPid = pid.toLongLong();
        }
    }
    // Only the native client writes the log; a browser tab on the service
    // must not inherit what an earlier client run left in it.
    if (nativeClient && cloud == QLatin1String("GeForce NOW")) {
        bool known = false;
        const QString streamed = gfnStreaming(known);
        if (known)
            cloud = streamed; // "" while it only shows its library or the queue
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

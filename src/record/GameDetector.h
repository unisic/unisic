#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class QDBusVariant;
class QDBusMessage;

// Is a game running, and which one? Drives instant replay's "while a game is
// running" mode, names the clips and fills "Recently played". Sources, none
// of which asks a game to cooperate:
//   - Steam: every game it starts runs under "reaper SteamLaunch AppId=<id>"
//     on Linux, and its AppId sits in the registry on Windows; the name comes
//     from the app's manifest (GameDetectorSteam.cpp / _win.cpp).
//   - Proton outside Steam (Heroic, Lutris, Bottles via umu-launcher), named
//     after the exe it runs.
//   - Cloud gaming and game streaming (Linux): the GeForce NOW, Steam Link,
//     Moonlight, Boosteroid and Greenlight clients, and a browser app window
//     on a known service's page (GameMatch.h has the rules and their limit).
//   - GameMode (Linux): games that register with the daemon (gamemoderun,
//     libgamemode), named after the executable it reports.
// Not Discord Rich Presence: reading it means serving Discord's own IPC
// socket, which either loses to a running Discord or takes its place.
// Exists only while that mode is selected or instant replay is running.
class GameDetector : public QObject
{
    Q_OBJECT
public:
    explicit GameDetector(QObject *parent = nullptr);
    bool running() const { return m_probe.running || m_gameModeClients > 0; }
    // The game's display name, empty when none runs or it has no name.
    QString game() const { return m_probe.running ? m_probe.name : m_gameModeName; }

    // The game's processes: the one that found it (Steam's reaper, Proton,
    // the cloud client, the GameMode client) and everything under it, read
    // from /proc when asked. Empty on Windows. For finding the game's window.
    QList<qint64> gamePids() const;

    struct Probe
    {
        bool running = false;
        QString name;
        qint64 pid = 0;
    };

signals:
    void runningChanged(bool running);
    void gameChanged(const QString &name);

private slots:
    void onGameModeProperties(const QString &iface, const QVariantMap &changed, const QStringList &);
    void onGameModeCount(const QDBusVariant &count);
    void onGameModeGames(const QDBusMessage &reply);

private:
    static Probe probe(); // per platform
    static QList<qint64> withDescendants(qint64 root); // per platform
    void poll();
    void update(const Probe &probe, int gameModeClients);
    void askGameModeGames();

    QTimer m_poll;
    Probe m_probe;
    int m_gameModeClients = 0;
    QString m_gameModeName;
    qint64 m_gameModePid = 0;
};

#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class QDBusVariant;

// Is a game running? Drives instant replay's "while a game is running" mode.
// Two sources, neither of which asks a game to cooperate:
//   - Steam: every game it starts runs under "reaper SteamLaunch AppId=<id>"
//     on Linux, and its AppId sits in the registry on Windows
//     (GameDetectorSteam.cpp / GameDetectorSteam_win.cpp).
//   - GameMode (Linux): Lutris, Heroic, gamemoderun and games that link
//     libgamemode register with the daemon, which announces its client count.
// Not Discord Rich Presence: reading it means serving Discord's own IPC
// socket, which either loses to a running Discord or takes its place.
// Exists only while that mode is selected.
class GameDetector : public QObject
{
    Q_OBJECT
public:
    explicit GameDetector(QObject *parent = nullptr);
    bool running() const { return m_steam || m_gameModeClients > 0; }


signals:
    void runningChanged(bool running);

private slots:
    void onGameModeProperties(const QString &iface, const QVariantMap &changed, const QStringList &);
    void onGameModeCount(const QDBusVariant &count);

private:
    static bool steamGameRunning(); // per platform
    void poll();
    void update(bool steam, int gameModeClients);

    QTimer m_poll;
    bool m_steam = false;
    int m_gameModeClients = 0;
};

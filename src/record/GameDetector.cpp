#include "GameDetector.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QFile>
#include <QGuiApplication>

namespace {
const QString kGameMode = QStringLiteral("com.feralinteractive.GameMode");
const QString kGameModePath = QStringLiteral("/com/feralinteractive/GameMode");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");
}

GameDetector::GameDetector(QObject *parent)
    : QObject(parent)
{
    if (QGuiApplication::platformName() != QLatin1String("windows")) {
        // Signals only, no polling. The first read does not auto-start the
        // daemon: a desktop that never uses GameMode keeps not running it.
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.connect(kGameMode, kGameModePath, kProperties, QStringLiteral("PropertiesChanged"), this,
                    SLOT(onGameModeProperties(QString,QVariantMap,QStringList)));
        QDBusMessage get = QDBusMessage::createMethodCall(kGameMode, kGameModePath, kProperties,
                                                          QStringLiteral("Get"));
        get << kGameMode << QStringLiteral("ClientCount");
        get.setAutoStartService(false);
        bus.callWithCallback(get, this, SLOT(onGameModeCount(QDBusVariant))); // no daemon: no reply
    }
    // Neither Steam nor a cloud client announces anything, so look every 5 s:
    // a comm read per process and a cmdline read only for the few named in
    // GameDetectorSteam.cpp, about 1 ms for 500 processes.
    m_poll.setInterval(5000);
    connect(&m_poll, &QTimer::timeout, this, &GameDetector::poll);
    m_poll.start();
    poll();
}

QList<qint64> GameDetector::gamePids() const
{
    const qint64 root = m_probe.running ? m_probe.pid : m_gameModePid;
    return root > 0 ? withDescendants(root) : QList<qint64>();
}

void GameDetector::poll()
{
    update(probe(), m_gameModeClients);
}

void GameDetector::onGameModeProperties(const QString &iface, const QVariantMap &changed,
                                        const QStringList &)
{
    if (iface == kGameMode && changed.contains(QStringLiteral("ClientCount")))
        update(m_probe, changed.value(QStringLiteral("ClientCount")).toInt());
}

void GameDetector::onGameModeCount(const QDBusVariant &count)
{
    update(m_probe, count.variant().toInt());
}

// ListGames answers (pid, object path) pairs; the name is the first game's
// process name, read from /proc instead of a second D-Bus call per game.
void GameDetector::askGameModeGames()
{
    QDBusMessage list = QDBusMessage::createMethodCall(kGameMode, kGameModePath, kGameMode,
                                                       QStringLiteral("ListGames"));
    list.setAutoStartService(false);
    QDBusConnection::sessionBus().callWithCallback(list, this, SLOT(onGameModeGames(QDBusMessage)));
}

void GameDetector::onGameModeGames(const QDBusMessage &reply)
{
    QString name;
    qint64 gamePid = 0;
    if (m_gameModeClients > 0 && !reply.arguments().isEmpty()) {
        const QDBusArgument games = reply.arguments().constFirst().value<QDBusArgument>();
        games.beginArray();
        while (!games.atEnd() && name.isEmpty()) {
            int pid = 0;
            QDBusObjectPath path;
            games.beginStructure();
            games >> pid >> path;
            games.endStructure();
            QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
            if (comm.open(QIODevice::ReadOnly)) {
                name = QString::fromUtf8(comm.readAll().trimmed());
                gamePid = pid;
            }
        }
        games.endArray();
        if (name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
            name.chop(4);
    }
    const QString was = game();
    m_gameModeName = name;
    m_gameModePid = gamePid;
    if (game() != was)
        emit gameChanged(game());
}

void GameDetector::update(const Probe &probe, int gameModeClients)
{
    const bool wasRunning = running();
    const QString wasGame = game();
    const bool newClients = gameModeClients != m_gameModeClients;
    m_probe = probe;
    m_gameModeClients = gameModeClients;
    if (gameModeClients <= 0) {
        m_gameModeName.clear();
        m_gameModePid = 0;
    }
    else if (newClients)
        askGameModeGames();
    if (game() != wasGame)
        emit gameChanged(game());
    if (running() != wasRunning)
        emit runningChanged(running());
}

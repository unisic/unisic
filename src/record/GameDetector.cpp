#include "GameDetector.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
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
    // Steam announces nothing, so look every 5 s while this mode is on: one
    // small read per process (see GameDetectorSteam.cpp), well under 1 ms.
    m_poll.setInterval(5000);
    connect(&m_poll, &QTimer::timeout, this, &GameDetector::poll);
    m_poll.start();
    poll();
}

void GameDetector::poll()
{
    update(steamGameRunning(), m_gameModeClients);
}

void GameDetector::onGameModeProperties(const QString &iface, const QVariantMap &changed,
                                        const QStringList &)
{
    if (iface == kGameMode && changed.contains(QStringLiteral("ClientCount")))
        update(m_steam, changed.value(QStringLiteral("ClientCount")).toInt());
}

void GameDetector::onGameModeCount(const QDBusVariant &count)
{
    update(m_steam, count.variant().toInt());
}

void GameDetector::update(bool steam, int gameModeClients)
{
    const bool was = running();
    m_steam = steam;
    m_gameModeClients = gameModeClients;
    if (running() != was)
        emit runningChanged(running());
}

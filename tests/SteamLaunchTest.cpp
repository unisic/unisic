#include <QtTest>
#include "record/SteamLaunch.h"

// A literal with its embedded NULs (QByteArray(const char*) stops at the first).
template <size_t N>
static QByteArray raw(const char (&s)[N]) { return QByteArray(s, N - 1); }

// Instant replay's "while a game is running" mode keys on Steam's reaper.
class SteamLaunchTest : public QObject
{
    Q_OBJECT
private slots:
    void readsTheAppId()
    {
        QCOMPARE(SteamLaunch::appId(raw("/home/u/.local/share/Steam/ubuntu12_32/reaper\0SteamLaunch\0AppId=1091500\0--\0/game/bin\0")), 1091500);
    }
    void ignoresOtherReapers()
    {
        QCOMPARE(SteamLaunch::appId(raw("reaper\0--version\0")), 0);
    }
    void ignoresTheGamesOwnArguments()
    {
        QCOMPARE(SteamLaunch::appId(raw("reaper\0SteamLaunch\0--\0game\0AppId=5\0")), 0);
    }
};

QTEST_APPLESS_MAIN(SteamLaunchTest)
#include "SteamLaunchTest.moc"

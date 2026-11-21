#include <QTest>
#include "record/GameMatch.h"

namespace {
template<int N> QByteArray raw(const char (&s)[N]) { return QByteArray(s, N - 1); }
}

class GameMatchTest : public QObject
{
    Q_OBJECT
private slots:
    void protonOutsideSteam()
    {
        QCOMPARE(GameMatch::protonGame(raw("python3\0/home/u/.local/share/umu/proton\0waitforexitandrun\0/games/Hades II/Hades2.exe\0")),
                 QStringLiteral("Hades2"));
        QCOMPARE(GameMatch::protonGame(raw("python3\0/x/Proton 9.0/proton\0waitforexitandrun\0Z:\\Games\\Foo\\Foo.EXE\0")),
                 QStringLiteral("Foo"));
        // Other verbs (the prefix setup Proton runs first) are not the game.
        QCOMPARE(GameMatch::protonGame(raw("python3\0/x/proton\0run\0/g/setup.exe\0")), QString());
        QCOMPARE(GameMatch::protonGame(raw("python3\0/usr/bin/http.server\0")), QString());
    }
    void cloudClients()
    {
        QCOMPARE(GameMatch::cloudClient("GeForceNOW\n"), QStringLiteral("GeForce NOW"));
        QCOMPARE(GameMatch::cloudClient("moonlight"), QStringLiteral("Moonlight"));
        QCOMPARE(GameMatch::cloudClient("steamlink"), QStringLiteral("Steam Link"));
        QCOMPARE(GameMatch::cloudClient("steam"), QString());
        QCOMPARE(GameMatch::cloudClient("moonlight-x"), QString());
    }
    void browserAppWindows()
    {
        QVERIFY(GameMatch::isBrowser("chrome"));
        QVERIFY(GameMatch::isBrowser("msedge"));
        QVERIFY(!GameMatch::isBrowser("firefox"));
        QCOMPARE(GameMatch::cloudPage(raw("/opt/google/chrome/chrome\0--app=https://www.xbox.com/pl-PL/play\0")),
                 QStringLiteral("Xbox Cloud Gaming"));
        QCOMPARE(GameMatch::cloudPage(raw("chromium\0--app=https://play.geforcenow.com/mall/\0")),
                 QStringLiteral("GeForce NOW"));
        QCOMPARE(GameMatch::cloudPage(raw("chromium\0--app=https://luna.amazon.com/\0")),
                 QStringLiteral("Amazon Luna"));
        // A page open in an ordinary window is not an app window.
        QCOMPARE(GameMatch::cloudPage(raw("chromium\0https://www.xbox.com/play\0")), QString());
        QCOMPARE(GameMatch::cloudPage(raw("chromium\0--app=https://www.xbox.com/pl-PL/games\0")), QString());
    }
    void steamFiles()
    {
        QCOMPARE(GameMatch::manifestName("\"AppState\"\n{\n\t\"appid\"\t\t\"1091500\"\n\t\"name\"\t\t\"Cyberpunk 2077\"\n"),
                 QStringLiteral("Cyberpunk 2077"));
        QCOMPARE(GameMatch::manifestName("garbage"), QString());
        QCOMPARE(GameMatch::libraryPaths("\"0\"\n{\n\t\t\"path\"\t\t\"/home/u/.local/share/Steam\"\n}\n\"1\"\n{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n}"),
                 QStringList({QStringLiteral("/home/u/.local/share/Steam"), QStringLiteral("D:\\SteamLibrary")}));
    }
};

QTEST_APPLESS_MAIN(GameMatchTest)
#include "GameMatchTest.moc"

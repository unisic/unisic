#pragma once
#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// What a running process says about a game, from its /proc comm and cmdline
// alone. Pure functions, so tests/GameMatchTest.cpp covers every rule.
namespace GameMatch {

// The exe a Proton launch runs ("python3\0/x/Proton 9.0/proton\0
// waitforexitandrun\0/games/Foo/Foo.exe"), as "Foo"; empty when it is not one.
// Steam starts Proton that way, and so do Heroic, Lutris and Bottles through
// umu-launcher, so this catches the games Steam's reaper does not.
inline QString protonGame(const QByteArray &cmdline)
{
    const QList<QByteArray> args = cmdline.split('\0');
    for (int i = 0; i + 2 < args.size(); ++i) {
        if (!args.at(i).endsWith("/proton") && args.at(i) != "proton")
            continue;
        if (args.at(i + 1) != "waitforexitandrun")
            return {};
        QString exe = QString::fromUtf8(args.at(i + 2));
        exe = exe.mid(exe.lastIndexOf(QRegularExpression(QStringLiteral("[/\\\\]"))) + 1);
        if (exe.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
            exe.chop(4);
        return exe;
    }
    return {};
}

// A native cloud-gaming or game-streaming client, by process name (comm,
// at most 15 bytes). The comm is what the client's own binary is called.
inline QString cloudClient(const QByteArray &comm)
{
    const QByteArray c = comm.trimmed().toLower();
    if (c.contains("geforcenow"))
        return QStringLiteral("GeForce NOW");
    if (c == "steamlink")
        return QStringLiteral("Steam Link");
    if (c == "moonlight")
        return QStringLiteral("Moonlight");
    if (c.contains("boosteroid"))
        return QStringLiteral("Boosteroid");
    if (c == "greenlight") // the open-source Xbox Cloud Gaming client
        return QStringLiteral("Xbox Cloud Gaming");
    return {};
}

// Chromium-family browsers, whose cmdline names the page of an app window
// (`--app=<url>`, what "install as app" shortcuts and the services' own
// launchers use). Other processes' cmdlines are never read.
inline bool isBrowser(const QByteArray &comm)
{
    const QByteArray c = comm.trimmed().toLower();
    for (const char *name : {"chrome", "chromium", "msedge", "brave", "vivaldi", "opera", "thorium"}) {
        if (c.startsWith(name))
            return true;
    }
    return false;
}

// shortcut: a service played in an ordinary browser TAB names no URL in any
// cmdline, and Wayland lets no client read another one's window title; only
// an app window (--app=) is seen. Upgrade: a compositor window list (KWin).
inline QString cloudPage(const QByteArray &cmdline)
{
    QByteArray c = cmdline.toLower();
    c.replace('\0', ' ');
    if (!c.contains("--app="))
        return {};
    if (c.contains("play.geforcenow.com"))
        return QStringLiteral("GeForce NOW");
    static const QRegularExpression xbox(QStringLiteral("xbox\\.com/([a-z]{2}-[a-z]{2}/)?play"));
    if (xbox.match(QString::fromUtf8(c)).hasMatch())
        return QStringLiteral("Xbox Cloud Gaming");
    if (c.contains("luna.amazon."))
        return QStringLiteral("Amazon Luna");
    if (c.contains("cloud.boosteroid.com"))
        return QStringLiteral("Boosteroid");
    return {};
}

// The "name" of a Steam appmanifest_<id>.acf.
inline QString manifestName(const QByteArray &acf)
{
    static const QRegularExpression name(QStringLiteral("\"name\"\\s+\"([^\"]*)\""));
    return name.match(QString::fromUtf8(acf)).captured(1);
}

// Every library "path" in Steam's libraryfolders.vdf (backslashes doubled).
inline QStringList libraryPaths(const QByteArray &vdf)
{
    static const QRegularExpression path(QStringLiteral("\"path\"\\s+\"([^\"]*)\""));
    QStringList out;
    for (auto it = path.globalMatch(QString::fromUtf8(vdf)); it.hasNext();)
        out << it.next().captured(1).replace(QLatin1String("\\\\"), QLatin1String("\\"));
    return out;
}
}

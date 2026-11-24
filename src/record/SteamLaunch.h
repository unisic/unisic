#pragma once
#include <QByteArray>
#include <QList>

namespace SteamLaunch {
// The AppId of a Steam launch in a NUL-separated /proc cmdline
// ("reaper\0SteamLaunch\0AppId=570\0--\0<game>..."), 0 when it is not one.
inline int appId(const QByteArray &cmdline)
{
    const QList<QByteArray> args = cmdline.split('\0');
    if (!args.contains(QByteArrayLiteral("SteamLaunch")))
        return 0;
    for (const QByteArray &arg : args) {
        if (arg == "--")
            break; // the game's own arguments follow
        if (arg.startsWith("AppId="))
            return arg.mid(6).toInt();
    }
    return 0;
}
}

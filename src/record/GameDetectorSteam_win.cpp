#include "GameDetector.h"

#include <QSettings>

// Steam writes the AppId of the game it is running here, 0 when none, and
// keeps each installed app's name under Apps\<id>.
// shortcut: Steam only on Windows; cloud clients and other launchers are not
// looked for there (no /proc), a process snapshot would add them.
GameDetector::Probe GameDetector::probe()
{
    const QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                          QSettings::NativeFormat);
    const int appId = steam.value(QStringLiteral("RunningAppID")).toInt();
    if (appId <= 0)
        return {};
    return {true, steam.value(QStringLiteral("Apps/%1/Name").arg(appId)).toString()};
}

QList<qint64> GameDetector::withDescendants(qint64)
{
    return {};
}

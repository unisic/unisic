#include "GameDetector.h"

#include <QSettings>

// Steam writes the AppId of the game it is running here, 0 when none.
bool GameDetector::steamGameRunning()
{
    const QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
                          QSettings::NativeFormat);
    return steam.value(QStringLiteral("RunningAppID")).toInt() > 0;
}

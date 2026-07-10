#pragma once
#include <QString>
#include <QStandardPaths>
#include <QDir>

// Single source of the settings file location, shared by Settings and
// ThemeController so they write ONE file instead of two.
//
// Everything now lives under ~/.config/unisic/ (lowercase) — the same dir as
// destinations.json — instead of QSettings' default ~/.config/Unisic/
// (capitalised from the org name). The split dirs were confusing ("where are
// my settings?") and, on a case-insensitive home, could even collide.
// Migration from the old path is done once in Settings' constructor (a
// key-by-key QSettings copy, not a raw file copy, to keep the INI format and
// group casing intact).
namespace UnisicConfig {

inline QString filePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/unisic");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/unisic.conf");
}

inline QString legacyFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/Unisic/unisic.conf");
}

// User-provided capture-sound files. Lives in the config dir (next to
// destinations.json) so dropping a .wav in manually is a documented way to
// extend the default set.
inline QString soundsDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/unisic/sounds");
    QDir().mkpath(dir);
    return dir;
}

} // namespace UnisicConfig

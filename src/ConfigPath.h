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

// Dev builds are a SEPARATE app ("unisic-dev"): own config dir, own
// single-instance socket, own KGlobalAccel component and own desktop id —
// so a build-tree binary never shadows or fights the installed stable
// Unisic. The other identity gates live in main.cpp / GlobalHotkeys.h.
inline QString dirName()
{
#ifdef UNISIC_DEV_BUILD
    return QStringLiteral("unisic-dev");
#else
    return QStringLiteral("unisic");
#endif
}

inline QString configDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QLatin1Char('/') + dirName();
    QDir().mkpath(dir);
    return dir;
}

inline QString filePath()
{
    return configDir() + QStringLiteral("/unisic.conf");
}

// The STABLE app's config — a fresh dev config is seeded from it (Settings
// constructor) so testing starts from the user's familiar settings.
inline QString stableConfigDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/unisic");
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
    const QString dir = configDir() + QStringLiteral("/sounds");
    QDir().mkpath(dir);
    return dir;
}

} // namespace UnisicConfig

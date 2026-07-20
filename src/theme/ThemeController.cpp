#include "ThemeController.h"
#include "ThemeJson.h"
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QPalette>
#include <QTimer>
#include <QUrl>
#include <algorithm>

static ThemeController *s_instance = nullptr;

ThemeController::ThemeController(QObject *parent) : QObject(parent)
{
    m_themeName = m_settings.value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    if (!s_instance)
        s_instance = this;

    // Follow live system scheme/palette changes so the "System" theme updates
    // without a restart. A scheme flip fires BOTH notifications — coalesce into
    // one rev bump per event-loop turn, or every icon re-renders twice.
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged,
                this, &ThemeController::scheduleSystemBump);
    }
    // Palette changes arrive as QEvent::ApplicationPaletteChange on qApp
    // (QGuiApplication::paletteChanged is deprecated since Qt 6.0).
    qApp->installEventFilter(this);

    // Decorative built-in palettes are shipped as REAL, editable JSON files in
    // the user's themes folder (seeded once from qrc), not baked into the menu:
    // the user can open, tweak or delete them like any theme. Core system/
    // unisic/dark/light stay hardcoded in Theme.qml.
    seedThemesFolder();
    // Community themes (incl. the seeded decoratives): scan once, then
    // hot-reload on any change so a theme author sees each save instantly. The
    // watcher only exists when the folder does — watchThemesFolder() re-arms
    // after openThemesFolder() creates it.
    reloadCustomThemes();
}

void ThemeController::seedThemesFolder()
{
    // Copy the shipped decorative themes into the user folder ONCE (a flag, not
    // a per-file existence check — so deleting a seeded theme makes it stay
    // gone, it is not recreated every launch). qrc is only the seed SOURCE; the
    // files the app actually reads are the loose ones on disk.
    if (m_settings.value(QStringLiteral("ui/themesSeeded")).toBool())
        return;
    const QString dir = themesFolder();
    QDir().mkpath(dir);
    const QDir qrc(QStringLiteral(":/resources/themes"));
    const QStringList shipped = qrc.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : shipped) {
        const QString dest = dir + QLatin1Char('/') + name;
        if (QFile::exists(dest))
            continue; // never clobber a file the user already has
        QFile::copy(qrc.filePath(name), dest);
        QFile::setPermissions(dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
    m_settings.setValue(QStringLiteral("ui/themesSeeded"), true);
}

bool ThemeController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        scheduleSystemBump();
    return QObject::eventFilter(watched, event);
}

void ThemeController::scheduleSystemBump()
{
    if (m_bumpQueued)
        return;
    m_bumpQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_bumpQueued = false;
        bump();
        emit systemChanged();
    }, Qt::QueuedConnection);
}

ThemeController::~ThemeController()
{
    if (s_instance == this)
        s_instance = nullptr;
}

ThemeController *ThemeController::instance()
{
    return s_instance;
}

void ThemeController::setThemeName(const QString &name)
{
    if (m_themeName == name)
        return;
    m_themeName = name;
    m_settings.setValue(QStringLiteral("ui/theme"), name);
    emit themeNameChanged();
    bump();
}

void ThemeController::bump()
{
    ++m_rev;
    emit revChanged();
}

bool ThemeController::systemDark() const
{
    // Unknown is common on non-KDE/wlroots compositors — fall back to the
    // palette lightness heuristic there instead of always claiming light.
    if (auto *hints = QGuiApplication::styleHints()) {
        const auto scheme = hints->colorScheme();
        if (scheme != Qt::ColorScheme::Unknown)
            return scheme == Qt::ColorScheme::Dark;
    }
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

QString ThemeController::themesFolder() const
{
    return QFileInfo(UnisicConfig::filePath()).absolutePath() + QStringLiteral("/themes");
}

// The example a first-time visitor finds in the empty folder: the stock Unisic
// palette under a new name, with the full key reference in "_comment" keys
// (unknown keys are ignored by the parser — JSON has no comments).
static QByteArray exampleThemeJson()
{
    return QByteArrayLiteral(
        "{\n"
        "  \"_comment\": \"Unisic custom theme. Save as <anything>.json in this folder;\",\n"
        "  \"_comment2\": \"the app reloads it live. Colors are #RRGGBB or #AARRGGBB.\",\n"
        "  \"_comment3\": \"Optional override keys: backgroundDeep, surfaceTop, surfaceBottom,\",\n"
        "  \"_comment4\": \"surfaceHi, surfaceHiTop, divider, edgeLight, shadow, danger, success,\",\n"
        "  \"_comment5\": \"dangerText, tooltipBg, tooltipText, thumbTop, thumbBottom, swatches,\",\n"
        "  \"_comment6\": \"recBadgeBg, recBadgeText, recDot, countdownBg, countdownNumber,\",\n"
        "  \"_comment7\": \"keystrokeBg, keystrokeText (recording overlays: REC badge, countdown, keys).\",\n"
        "  \"name\": \"My Theme\",\n"
        "  \"isDark\": true,\n"
        "  \"primary\": \"#17153B\",\n"
        "  \"secondary\": \"#2E236C\",\n"
        "  \"tertiary\": \"#433D8B\",\n"
        "  \"accent\": \"#C8ACD6\",\n"
        "  \"bg\": \"#100E2C\",\n"
        "  \"surface\": \"#1E1B4A\",\n"
        "  \"text\": \"#F3F0FA\",\n"
        "  \"textOnAccent\": \"#1B1834\"\n"
        "}\n");
}

void ThemeController::openThemesFolder()
{
    const QString dir = themesFolder();
    QDir().mkpath(dir);
    // Seed the example only into a folder with no themes at all — never
    // recreate one the user deleted while other themes exist.
    if (QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty())
        {
            QFile f(dir + QStringLiteral("/my-theme.json"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(exampleThemeJson());
        }
    reloadCustomThemes();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void ThemeController::scheduleCustomReload()
{
    // Editors fire bursts (truncate, write, rename); coalesce to one rescan.
    if (m_customReloadQueued)
        return;
    m_customReloadQueued = true;
    QTimer::singleShot(150, this, [this] {
        m_customReloadQueued = false;
        reloadCustomThemes();
    });
}

void ThemeController::watchThemesFolder()
{
    const QString dir = themesFolder();
    if (!QDir(dir).exists()) {
        delete m_watcher;
        m_watcher = nullptr;
        return;
    }
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged,
                this, &ThemeController::scheduleCustomReload);
        connect(m_watcher, &QFileSystemWatcher::fileChanged,
                this, &ThemeController::scheduleCustomReload);
    }
    // Re-arm from scratch each scan: the watcher silently drops deleted paths,
    // and in-place edits only fire fileChanged when the FILE is watched.
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    m_watcher->addPath(dir);
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &f : files)
        m_watcher->addPath(dir + QLatin1Char('/') + f);
}

void ThemeController::reloadCustomThemes()
{
    QVariantList themes;
    QVariantMap defs;
    QStringList errors;
    const QDir dir(themesFolder());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")},
                                                  QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            errors << QStringLiteral("%1: %2").arg(fi.fileName(), f.errorString());
            continue;
        }
        QString err;
        const QVariantMap def = ThemeJson::parse(f.readAll(), fi.completeBaseName(), &err);
        if (def.isEmpty()) {
            errors << QStringLiteral("%1: %2").arg(fi.fileName(), err);
            continue;
        }
        const QString id = QStringLiteral("custom:") + fi.completeBaseName();
        defs.insert(id, def);
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("name"), def.value(QStringLiteral("name")).toString());
        themes.append(entry);
    }
    std::sort(themes.begin(), themes.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString()
                   .localeAwareCompare(b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });

    const bool changed = themes != m_customThemes || defs != m_customDefs
                         || errors != m_customErrors;
    m_customThemes = themes;
    m_customDefs = defs;
    m_customErrors = errors;
    watchThemesFolder();
    if (!changed)
        return;
    emit customThemesChanged();
    // Live-edit of the ACTIVE theme must also recolor every provider-drawn
    // icon; rev is what UIcon keys its cache on.
    if (m_themeName.startsWith(QLatin1String("custom:")))
        bump();
}

QColor ThemeController::sysWindow() const { return qApp->palette().color(QPalette::Active, QPalette::Window); }
QColor ThemeController::sysBase() const { return qApp->palette().color(QPalette::Active, QPalette::Base); }
QColor ThemeController::sysAlternateBase() const { return qApp->palette().color(QPalette::Active, QPalette::AlternateBase); }
QColor ThemeController::sysButton() const { return qApp->palette().color(QPalette::Active, QPalette::Button); }
QColor ThemeController::sysText() const { return qApp->palette().color(QPalette::Active, QPalette::WindowText); }
QColor ThemeController::sysMidText() const
{
    // A muted secondary text colour: blend window text toward window bg.
    const QColor t = sysText();
    const QColor w = sysWindow();
    return QColor((t.red() + w.red()) / 2, (t.green() + w.green()) / 2, (t.blue() + w.blue()) / 2);
}
QColor ThemeController::sysAccent() const { return qApp->palette().color(QPalette::Active, QPalette::Highlight); }
QColor ThemeController::sysAccentText() const { return qApp->palette().color(QPalette::Active, QPalette::HighlightedText); }
QColor ThemeController::sysTooltipBase() const { return qApp->palette().color(QPalette::Active, QPalette::ToolTipBase); }
QColor ThemeController::sysTooltipText() const { return qApp->palette().color(QPalette::Active, QPalette::ToolTipText); }

// QPalette carries no positive/negative/hover-decoration roles; on KDE they
// live in kdeglobals (the same file plasma-integration builds the palette
// from), stored as "r,g,b[,a]". Alpha-0 sentinel when absent — Theme.qml then
// keeps its theme-neutral defaults. Read per call: values are only pulled on
// a rev bump, and the palette-change event that triggers the bump fires after
// Plasma rewrites the file, so a fresh QSettings sees the new scheme.
static QColor kdeGlobalsColor(const char *group, const char *key)
{
    static const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kdeglobals");
    const QStringList desktops =
        qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (!desktops.contains(QLatin1String("KDE"), Qt::CaseInsensitive))
        return QColor(0, 0, 0, 0); // stale kdeglobals on a non-KDE desktop must not win
    QSettings kdeglobals(path, QSettings::IniFormat);
    kdeglobals.beginGroup(QString::fromLatin1(group));
    // QSettings' INI parser already splits a comma-separated value ("39,174,96")
    // into a QStringList — reading it back through toString() yields "" for a
    // multi-item list, so pull the list directly (a single uncommaed value still
    // arrives as a one-element list, caught by the size guard below).
    const QStringList parts = kdeglobals.value(QString::fromLatin1(key)).toStringList();
    if (parts.size() < 3)
        return QColor(0, 0, 0, 0);
    return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(),
                  parts.size() > 3 ? parts[3].toInt() : 255);
}

QColor ThemeController::sysPositive() const { return kdeGlobalsColor("Colors:View", "ForegroundPositive"); }
QColor ThemeController::sysNegative() const { return kdeGlobalsColor("Colors:View", "ForegroundNegative"); }
QColor ThemeController::sysHover() const { return kdeGlobalsColor("Colors:Button", "DecorationHover"); }

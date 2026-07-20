#pragma once
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QRegularExpression>

// Filename template expansion for saved captures ("Unisic_%date%_%time%").
// Header-only (like hotkeys/ShortcutFormat.h) so the unit test can exercise
// it without constructing AppContext, which spins up capture backends,
// hotkeys and uploads.
namespace FilenameTemplate {

// Expands the template tokens against `now`. %i% = the monotonic counter,
// read-only here — the actual increment happens once per saved capture in
// AppContext::finishCapture, so preview and real save agree on the number.
inline QString expand(QString t, int counter, const QDateTime &now)
{
    t = t.trimmed();
    if (t.isEmpty())
        t = QStringLiteral("Unisic_%date%_%time%");
    t.replace(QLatin1String("%date%"), now.toString(QStringLiteral("yyyy-MM-dd")));
    t.replace(QLatin1String("%time%"), now.toString(QStringLiteral("HH-mm-ss")));
    t.replace(QLatin1String("%datetime%"), now.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    t.replace(QLatin1String("%unix%"), QString::number(now.toSecsSinceEpoch()));
    t.replace(QLatin1String("%rand%"),
              QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    t.replace(QLatin1String("%i%"), QString::number(counter));
    static const QRegularExpression illegal(QStringLiteral("[/\\\\:*?\"<>|]"));
    t.replace(illegal, QStringLiteral("_"));
    return t;
}

// Maps the imageFormat setting to the file extension; anything the encoder
// does not support falls back to png.
inline QString extensionFor(const QString &imageFormat)
{
    QString ext = imageFormat.toLower();
    if (ext == QLatin1String("jpeg")) ext = QStringLiteral("jpg");
    if (ext != QLatin1String("jpg") && ext != QLatin1String("webp")) ext = QStringLiteral("png");
    return ext;
}

} // namespace FilenameTemplate

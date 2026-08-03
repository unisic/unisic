#pragma once
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QVariantMap>

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
    // A leading dot makes the capture a hidden file: saved without a word of
    // warning, then invisible in every file manager the user would look in.
    // Trailing dots and spaces go too, because they break the round trip
    // through a Samba or FAT share.
    while (!t.isEmpty()
           && (t.startsWith(QLatin1Char('.')) || t.startsWith(QLatin1Char(' '))))
        t.remove(0, 1);
    while (!t.isEmpty()
           && (t.endsWith(QLatin1Char('.')) || t.endsWith(QLatin1Char(' '))))
        t.chop(1);
    if (t.isEmpty())
        t = QStringLiteral("Unisic");
    return t;
}

// The tokens expand() understands, for the settings field to offer as chips and
// to draw as pills. Deliberately one screen below expand() and not in the QML:
// a chip is a promise that the token does something, and the only thing that can
// keep that promise is the function right above. Same shape as
// UploadManager::templateHelp - { "pattern": <regex matching every token>,
// "vars": [ { "token", "label", "description", "caretBack" } ] } - so one QML
// component draws both. caretBack is 0 throughout: none of these take an
// argument the user still has to type.
inline QVariantMap help()
{
    const auto var = [](const char *token, const char *label, const char *description) {
        return QVariantMap{
            {QStringLiteral("token"), QString::fromLatin1(token)},
            {QStringLiteral("label"), QCoreApplication::translate("FilenameTemplate", label)},
            {QStringLiteral("description"),
             QCoreApplication::translate("FilenameTemplate", description)},
            {QStringLiteral("caretBack"), 0},
        };
    };
    return QVariantMap{
        // Longest alternative first: %date% would otherwise eat the start of
        // %datetime% and leave the rest unmatched.
        {QStringLiteral("pattern"), QStringLiteral("%(?:datetime|date|time|unix|rand|i)%")},
        {QStringLiteral("vars"), QVariantList{
            var("%date%", QT_TRANSLATE_NOOP("FilenameTemplate", "Date"),
                QT_TRANSLATE_NOOP("FilenameTemplate", "The day of the capture, as 2026-08-03.")),
            var("%time%", QT_TRANSLATE_NOOP("FilenameTemplate", "Time"),
                QT_TRANSLATE_NOOP("FilenameTemplate", "The time of the capture, as 18-53-37.")),
            var("%datetime%", QT_TRANSLATE_NOOP("FilenameTemplate", "Date and time"),
                QT_TRANSLATE_NOOP("FilenameTemplate", "Both at once, as 2026-08-03_18-53-37.")),
            var("%unix%", QT_TRANSLATE_NOOP("FilenameTemplate", "Unix time"),
                QT_TRANSLATE_NOOP("FilenameTemplate", "Seconds since 1970, as one number.")),
            var("%rand%", QT_TRANSLATE_NOOP("FilenameTemplate", "Random"),
                QT_TRANSLATE_NOOP("FilenameTemplate", "Eight random characters, new every capture.")),
            var("%i%", QT_TRANSLATE_NOOP("FilenameTemplate", "Counter"),
                QT_TRANSLATE_NOOP("FilenameTemplate", "A number that goes up by one with each saved capture.")),
        }},
    };
}

// Maps the imageFormat setting to the file extension; anything the encoder
// does not support falls back to png. gif belongs here even though Qt cannot
// write one: AppContext routes it through ffmpeg, and renames the file back to
// .png if that encode fails, so the name is never a lie about the bytes.
inline QString extensionFor(const QString &imageFormat)
{
    QString ext = imageFormat.toLower();
    if (ext == QLatin1String("jpeg")) ext = QStringLiteral("jpg");
    if (ext != QLatin1String("jpg") && ext != QLatin1String("webp")
        && ext != QLatin1String("gif"))
        ext = QStringLiteral("png");
    return ext;
}

// Re-points a file NAME (never a path - callers keep the directory) at another
// extension, adding one when there is none. Used wherever the bytes turn out to
// be a different format than the name promised: the JPEG-with-alpha switch, a
// GIF encode with no ffmpeg, an upload in its own format, and the over-size
// auto-conversion. `ext` is taken as given (already through extensionFor).
inline QString withExtension(const QString &fileName, const QString &ext)
{
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    // A dot before the last '/' would belong to a directory, not to the name -
    // but names are what this takes, so only a leading dot ("...") is guarded:
    // stripping it would leave an empty base name.
    const QString base = dot > 0 ? fileName.left(dot) : fileName;
    return base + QLatin1Char('.') + ext;
}

// "Is this file already in that format?" - the question every convert or
// re-encode path asks before it does any work. `have` is a suffix as it stands
// on disk, `want` an extension already through extensionFor. Its own function
// because .jpeg and .jpg are the same bytes under two names, and each call site
// that spelled that pair out by hand was one edit away from forgetting it (the
// history upload path already had). Note `have` is NOT run through
// extensionFor: that maps every unknown suffix to png, which would make a .bmp
// count as already-PNG and refuse the conversion the user asked for.
inline bool sameFormat(const QString &have, const QString &want)
{
    const QString h = have.toLower();
    const QString w = extensionFor(want);
    return h == w || (w == QLatin1String("jpg") && h == QLatin1String("jpeg"));
}

// The extension the BYTES earned, from the mime type the encoder reported.
// Asking for a format is not getting one - a transparent JPEG comes back PNG
// and a GIF without ffmpeg does too - so anything that names a file after an
// async encode has to name it from this, not from what it requested. A .gif
// name on PNG bytes is what an "unsupported file type" from an upload server
// looks like an hour later. An empty mime (a failed encode) answers png, which
// is harmless: those bytes are never sent.
inline QString extensionForMime(const QString &mime)
{
    const int slash = mime.indexOf(QLatin1Char('/'));
    return extensionFor(slash < 0 ? mime : mime.mid(slash + 1));
}

} // namespace FilenameTemplate

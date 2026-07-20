#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Community theme files: parsing + validation for <config>/themes/*.json.
// Header-only pure logic (same pattern as FilenameTemplate.h) so the unit test
// compiles it without dragging in ThemeController's QML registration.
//
// The schema mirrors Theme.qml's _defs seed entries: a handful of required
// colors + isDark, and every other token _expand() knows as an OPTIONAL
// override — an omitted optional falls through to _expand()'s derivation, so a
// minimal theme is 8 colors and still looks complete. Unknown keys are ignored
// (forward compatibility; also allows "_comment" fields — JSON has none).
namespace ThemeJson {

inline QStringList requiredColorKeys()
{
    return {QStringLiteral("primary"), QStringLiteral("secondary"),
            QStringLiteral("tertiary"), QStringLiteral("accent"),
            QStringLiteral("bg"), QStringLiteral("surface"),
            QStringLiteral("text"), QStringLiteral("textOnAccent")};
}

inline QStringList optionalColorKeys()
{
    return {QStringLiteral("backgroundDeep"), QStringLiteral("surfaceTop"),
            QStringLiteral("surfaceBottom"), QStringLiteral("surfaceHi"),
            QStringLiteral("surfaceHiTop"), QStringLiteral("divider"),
            QStringLiteral("edgeLight"), QStringLiteral("shadow"),
            QStringLiteral("danger"), QStringLiteral("success"),
            QStringLiteral("dangerText"), QStringLiteral("tooltipBg"),
            QStringLiteral("tooltipText"), QStringLiteral("thumbTop"),
            QStringLiteral("thumbBottom"),
            // Recording-overlay surfaces (REC badge, countdown, keystroke badge).
            QStringLiteral("recBadgeBg"), QStringLiteral("recBadgeText"),
            QStringLiteral("recDot"), QStringLiteral("countdownBg"),
            QStringLiteral("countdownNumber"), QStringLiteral("keystrokeBg"),
            QStringLiteral("keystrokeText")};
}

// "#RGB", "#RRGGBB", "#AARRGGBB" or an SVG color name — whatever QColor's
// string constructor takes (Theme.qml hands the strings to Qt.color unchanged).
inline bool isValidColor(const QString &s)
{
    return QColor::isValidColorName(s);
}

// Parses one theme file's bytes. On any problem returns an empty map and puts
// a one-line reason into *error (file name is the caller's to prepend). On
// success the map holds "name" (from the file, else fallbackName), "isDark",
// every present color key as its original string, and an optional "swatches"
// QStringList — ready for Theme.qml's _expand() unchanged.
inline QVariantMap parse(const QByteArray &bytes, const QString &fallbackName, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QVariantMap();
    };
    QJsonParseError jsonErr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &jsonErr);
    if (doc.isNull())
        return fail(QStringLiteral("not valid JSON: %1").arg(jsonErr.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("top level must be a JSON object"));
    const QJsonObject o = doc.object();

    QVariantMap out;
    for (const QString &key : requiredColorKeys()) {
        const QJsonValue v = o.value(key);
        if (!v.isString())
            return fail(QStringLiteral("missing required color \"%1\"").arg(key));
        if (!isValidColor(v.toString()))
            return fail(QStringLiteral("\"%1\" is not a color: \"%2\"").arg(key, v.toString()));
        out.insert(key, v.toString());
    }
    for (const QString &key : optionalColorKeys()) {
        const QJsonValue v = o.value(key);
        if (v.isUndefined())
            continue;
        if (!v.isString() || !isValidColor(v.toString()))
            return fail(QStringLiteral("\"%1\" is not a color").arg(key));
        out.insert(key, v.toString());
    }
    const QJsonValue dark = o.value(QStringLiteral("isDark"));
    if (!dark.isBool())
        return fail(QStringLiteral("missing required boolean \"isDark\""));
    out.insert(QStringLiteral("isDark"), dark.toBool());

    if (o.contains(QStringLiteral("swatches"))) {
        const QJsonValue v = o.value(QStringLiteral("swatches"));
        if (!v.isArray())
            return fail(QStringLiteral("\"swatches\" must be an array of colors"));
        QStringList swatches;
        const QJsonArray arr = v.toArray();
        for (const QJsonValue &c : arr) {
            if (!c.isString() || !isValidColor(c.toString()))
                return fail(QStringLiteral("\"swatches\" entry is not a color"));
            swatches << c.toString();
        }
        if (!swatches.isEmpty())
            out.insert(QStringLiteral("swatches"), swatches);
    }

    const QString name = o.value(QStringLiteral("name")).toString().trimmed();
    out.insert(QStringLiteral("name"), name.isEmpty() ? fallbackName : name);
    return out;
}

} // namespace ThemeJson

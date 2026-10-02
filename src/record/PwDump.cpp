#include "PwDump.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

namespace PwDump {

QJsonArray nodes()
{
    const QString helper = QStandardPaths::findExecutable(QStringLiteral("pw-dump"));
    if (helper.isEmpty())
        return {};
    QProcess process;
    process.start(helper, {});
    if (!process.waitForFinished(2500)) {
        process.kill();
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    return doc.isArray() ? doc.array() : QJsonArray();
}

static QJsonObject nodeProps(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("type")).toString() != QLatin1String("PipeWire:Interface:Node"))
        return {};
    return object.value(QStringLiteral("info")).toObject()
        .value(QStringLiteral("props")).toObject();
}

static QString firstOf(const QJsonObject &props, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        const QString v = props.value(QLatin1String(k)).toString();
        if (!v.isEmpty())
            return v;
    }
    return {};
}

QList<AppStream> appStreams(const QJsonArray &nodes)
{
    QList<AppStream> out;
    for (const QJsonValue &value : nodes) {
        const QJsonObject props = nodeProps(value);
        if (props.value(QStringLiteral("media.class")).toString() != QLatin1String("Stream/Output/Audio"))
            continue;
        AppStream s;
        s.serial = props.value(QStringLiteral("object.serial")).toVariant().toString();
        s.app = firstOf(props, {"application.process.binary", "application.name", "node.name"});
        if (s.serial.isEmpty() || s.app.isEmpty())
            continue;
        s.label = firstOf(props, {"application.name", "node.description", "node.name"});
        s.icon = firstOf(props, {"application.icon-name", "application.process.binary"});
        out.append(s);
    }
    return out;
}

QVariantList applications(const QList<AppStream> &streams)
{
    QVariantList out;
    QHash<QString, int> at;
    for (const AppStream &s : streams) {
        if (const auto it = at.constFind(s.app); it != at.constEnd()) {
            QVariantMap m = out[*it].toMap();
            m[QStringLiteral("streams")] = m.value(QStringLiteral("streams")).toInt() + 1;
            out[*it] = m;
            continue;
        }
        at.insert(s.app, int(out.size()));
        out.append(QVariantMap{{QStringLiteral("id"), s.app},
                               {QStringLiteral("label"), s.label},
                               {QStringLiteral("icon"), s.icon},
                               {QStringLiteral("streams"), 1}});
    }
    return out;
}

// Real mics (Audio/Source) and virtual sources such as an EasyEffects
// processed mic (Audio/Source/Virtual). Monitors never appear - PipeWire
// models them as sink ports, not nodes. The id is node.name, which is also
// the source's pipewire-pulse name, i.e. exactly what ffmpeg's pulse input
// takes - and unlike object.serial it survives a reboot in the setting.
QVariantList inputDevices(const QJsonArray &nodes)
{
    QVariantList result;
    for (const QJsonValue &value : nodes) {
        const QJsonObject props = nodeProps(value);
        const QString mediaClass = props.value(QStringLiteral("media.class")).toString();
        if (mediaClass != QLatin1String("Audio/Source")
            && mediaClass != QLatin1String("Audio/Source/Virtual"))
            continue;
        const QString id = props.value(QStringLiteral("node.name")).toString();
        if (id.isEmpty())
            continue;
        QString label = firstOf(props, {"node.description", "node.nick"});
        if (label.isEmpty())
            label = id;
        result.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("label"), label}});
    }
    return result;
}

} // namespace PwDump

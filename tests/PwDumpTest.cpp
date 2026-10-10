#include <QJsonDocument>
#include <QtTest>
#include "PwDump.h"

// The parser the picker and the recorder share: if these two disagree on what
// an "application" is, a ticked app records nothing (or the wrong thing).
class PwDumpTest : public QObject
{
    Q_OBJECT
private slots:
    void parsesAndGroups()
    {
        const QByteArray json = R"([
          {"type":"PipeWire:Interface:Node","info":{"props":{"media.class":"Stream/Output/Audio",
            "object.serial":101,"application.name":"Firefox","application.process.binary":"firefox",
            "application.icon-name":"firefox"}}},
          {"type":"PipeWire:Interface:Node","info":{"props":{"media.class":"Stream/Output/Audio",
            "object.serial":102,"application.name":"Firefox","application.process.binary":"firefox"}}},
          {"type":"PipeWire:Interface:Node","info":{"props":{"media.class":"Stream/Output/Audio",
            "object.serial":200,"node.name":"mpv"}}},
          {"type":"PipeWire:Interface:Node","info":{"props":{"media.class":"Stream/Input/Audio",
            "object.serial":300,"application.process.binary":"obs"}}},
          {"type":"PipeWire:Interface:Node","info":{"props":{"media.class":"Audio/Source",
            "node.name":"alsa_input.mic","node.description":"Built-in Mic"}}},
          {"type":"PipeWire:Interface:Port","info":{"props":{"media.class":"Stream/Output/Audio",
            "object.serial":400,"application.process.binary":"ghost"}}}
        ])";
        const QJsonArray nodes = QJsonDocument::fromJson(json).array();

        const QList<PwDump::AppStream> streams = PwDump::appStreams(nodes);
        QCOMPARE(streams.size(), 3);
        QCOMPARE(streams[0].serial, QStringLiteral("101"));
        QCOMPARE(streams[0].app, QStringLiteral("firefox"));
        QCOMPARE(streams[0].label, QStringLiteral("Firefox"));
        QCOMPARE(streams[0].icon, QStringLiteral("firefox"));
        QCOMPARE(streams[1].icon, QStringLiteral("firefox")); // binary fallback
        QCOMPARE(streams[2].app, QStringLiteral("mpv"));
        QCOMPARE(streams[2].label, QStringLiteral("mpv"));

        const QVariantList apps = PwDump::applications(streams);
        QCOMPARE(apps.size(), 2);
        QCOMPARE(apps[0].toMap().value("id").toString(), QStringLiteral("firefox"));
        QCOMPARE(apps[0].toMap().value("streams").toInt(), 2);
        QCOMPARE(apps[1].toMap().value("streams").toInt(), 1);

        const QVariantList inputs = PwDump::inputDevices(nodes);
        QCOMPARE(inputs.size(), 1);
        QCOMPARE(inputs[0].toMap().value("label").toString(), QStringLiteral("Built-in Mic"));
    }
};

QTEST_GUILESS_MAIN(PwDumpTest)
#include "PwDumpTest.moc"

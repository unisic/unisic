#include <QtTest>
#include <QStandardPaths>
#include <QFile>
#include <QDir>

#include "ShortcutKeyMap.h"
#include "ShortcutBinder.h"

using namespace ShortcutBinder;

// The gsettings/xfconf backends shell out to real desktop tools and would
// mutate the tester's own shortcuts, so only the pure key mapper and the COSMIC
// RON file logic (fully offline, redirected to a temp dir) are exercised here.
class ShortcutBinderTest : public QObject
{
    Q_OBJECT

    QString cosmicPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + QStringLiteral("/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom");
    }
    QString readCosmic() const
    {
        QFile f(cosmicPath());
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    void seedCosmic(const QString &text) const
    {
        QDir().mkpath(QFileInfo(cosmicPath()).path());
        QFile f(cosmicPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    }
    QList<Binding> sample() const
    {
        return {
            {QStringLiteral("capture-region"), QStringLiteral("Region"),
             QStringLiteral("Meta+Shift+S"), QStringLiteral("/usr/bin/unisic --hotkey capture-region")},
            {QStringLiteral("capture-fullscreen"), QStringLiteral("Full screen"),
             QStringLiteral("Print"), QStringLiteral("/usr/bin/unisic --hotkey capture-fullscreen")},
        };
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cleanup() { QFile::remove(cosmicPath()); }

    // -------- key mapper --------
    void mapsModifiedLetter()
    {
        const auto c = ShortcutKeyMap::parseChord(QStringLiteral("Meta+Shift+S"));
        QVERIFY(c.ok);
        QCOMPARE(c.key, QStringLiteral("s"));
        QVERIFY(c.mods.contains(QStringLiteral("Super")));
        QVERIFY(c.mods.contains(QStringLiteral("Shift")));
        QCOMPARE(ShortcutKeyMap::toGtkAccel(c), QStringLiteral("<Super><Shift>s"));
    }
    void mapsNamedAndFunctionKeys()
    {
        QCOMPARE(ShortcutKeyMap::parseChord(QStringLiteral("Print")).key, QStringLiteral("Print"));
        QCOMPARE(ShortcutKeyMap::parseChord(QStringLiteral("F5")).key, QStringLiteral("F5"));
        QCOMPARE(ShortcutKeyMap::parseChord(QStringLiteral("Ctrl+PgUp")).key, QStringLiteral("Prior"));
        QCOMPARE(ShortcutKeyMap::toGtkAccel(ShortcutKeyMap::parseChord(QStringLiteral("Ctrl+Alt+G"))),
                 QStringLiteral("<Control><Alt>g"));
    }
    void modifierOnlyIsCosmicOkButNotGtk()
    {
        const auto c = ShortcutKeyMap::parseChord(QStringLiteral("Meta"));
        QVERIFY(c.ok);
        QVERIFY(c.key.isEmpty());
        QVERIFY(ShortcutKeyMap::toGtkAccel(c).isEmpty()); // gsettings/xfce can't bind it
    }
    void unmappableKeyReportsNotOk()
    {
        QVERIFY(!ShortcutKeyMap::parseChord(QStringLiteral("Meta+@")).ok);
        QVERIFY(!ShortcutKeyMap::parseChord(QString()).ok);
    }
    void parsesAlternates()
    {
        const auto all = ShortcutKeyMap::parseAll(QStringLiteral("Meta+Shift+S, Print"));
        QCOMPARE(all.size(), 2);
        QVERIFY(all[0].ok && all[1].ok);
    }

    // -------- COSMIC RON writer --------
    void cosmicWritesSpawnEntries()
    {
        const Result r = install(Backend::Cosmic, sample());
        QVERIFY(r.ok);
        QCOMPARE(r.written, 2);
        const QString text = readCosmic();
        QVERIFY(text.contains(QStringLiteral("Spawn(\"/usr/bin/unisic --hotkey capture-region\")")));
        QVERIFY(text.contains(QStringLiteral("key: \"s\"")));
        QVERIFY(text.contains(QStringLiteral("key: \"Print\"")));
        QVERIFY(text.trimmed().startsWith(QLatin1Char('{')));
        QVERIFY(text.trimmed().endsWith(QLatin1Char('}')));
    }
    void cosmicPreservesUserEntriesAndIsIdempotent()
    {
        seedCosmic(QStringLiteral(
            "{\n    (\n        modifiers: [\n            Super,\n        ],\n"
            "        key: \"t\",\n    ): System(Terminal),\n}\n"));
        QVERIFY(install(Backend::Cosmic, sample()).ok);
        QVERIFY(install(Backend::Cosmic, sample()).ok); // twice
        const QString text = readCosmic();
        QVERIFY(text.contains(QStringLiteral("System(Terminal)")));           // user kept
        QCOMPARE(text.count(QStringLiteral("--hotkey capture-region")), 1);    // no dupes
        QCOMPARE(text.count(QStringLiteral("--hotkey capture-fullscreen")), 1);
    }
    void cosmicStripsMultilineOursOnReinstall()
    {
        // Simulate COSMIC's settings UI having rewritten our entry to multi-line.
        seedCosmic(QStringLiteral(
            "{\n    (\n        modifiers: [\n            Super,\n            Shift,\n        ],\n"
            "        key: \"s\",\n    ): Spawn(\"/usr/bin/unisic --hotkey capture-region\"),\n}\n"));
        QVERIFY(install(Backend::Cosmic, sample()).ok);
        QCOMPARE(readCosmic().count(QStringLiteral("--hotkey capture-region")), 1);
    }
    void cosmicRemoveClearsOursKeepsUser()
    {
        seedCosmic(QStringLiteral(
            "{\n    (modifiers: [Super], key: \"t\"): System(Terminal),\n}\n"));
        QVERIFY(install(Backend::Cosmic, sample()).ok);
        QVERIFY(remove(Backend::Cosmic).ok);
        const QString text = readCosmic();
        QVERIFY(text.contains(QStringLiteral("System(Terminal)")));
        QVERIFY(!text.contains(QStringLiteral("--hotkey")));
    }
    void cosmicSkipsUnmappableButWritesRest()
    {
        QList<Binding> b = {
            {QStringLiteral("x"), QStringLiteral("Bad"), QStringLiteral("Meta+@"),
             QStringLiteral("/usr/bin/unisic --hotkey x")},
            {QStringLiteral("capture-region"), QStringLiteral("Region"),
             QStringLiteral("Print"), QStringLiteral("/usr/bin/unisic --hotkey capture-region")},
        };
        const Result r = install(Backend::Cosmic, b);
        QVERIFY(r.ok);
        QCOMPARE(r.written, 1);
        QVERIFY(r.skipped.contains(QStringLiteral("Bad")));
    }
};

QTEST_MAIN(ShortcutBinderTest)
#include "ShortcutBinderTest.moc"

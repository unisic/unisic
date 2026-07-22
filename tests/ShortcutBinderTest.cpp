#include <QtTest>
#include <QStandardPaths>
#include <QFile>
#include <QDir>

#include "ShortcutKeyMap.h"
#include "ShortcutBinder.h"

using namespace ShortcutBinder;

// The gsettings/xfconf backends shell out to real desktop tools and would
// mutate the tester's own shortcuts, so only the pure key mapper and the two
// file-store backends — COSMIC RON and Singularity labwc rc.xml — (fully
// offline, redirected to a temp dir) are exercised here.
class ShortcutBinderTest : public QObject
{
    Q_OBJECT

    QString labwcPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + QStringLiteral("/labwc/rc.xml");
    }
    QString readLabwc() const
    {
        QFile f(labwcPath());
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    void seedLabwc(const QString &text) const
    {
        QDir().mkpath(QFileInfo(labwcPath()).path());
        QFile f(labwcPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text.toUtf8());
    }
    // The shape Singularity's ShortcutManager generates: DE keybinds inside
    // <keyboard>, dispatching over D-Bus.
    QString labwcSeedDoc() const
    {
        return QStringLiteral(
            "<labwc_config>\n  <keyboard>\n"
            "    <keybind key=\"A-F4\"><action name=\"Close\" /></keybind>\n"
            "    <keybind key=\"Print\"><action name=\"Execute\"><command>gdbus call --session"
            " --dest dev.sinty.desktop --method dev.sinty.desktop.Shortcuts.ExecuteAction"
            " screenshot_tool</command></action></keybind>\n"
            "  </keyboard>\n</labwc_config>\n");
    }

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
    void cleanup() { QFile::remove(cosmicPath()); QFile::remove(labwcPath()); }

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

    // -------- labwc key form --------
    void labwcKeyForm()
    {
        QCOMPARE(ShortcutKeyMap::toLabwcKey(ShortcutKeyMap::parseChord(QStringLiteral("Meta+Shift+S"))),
                 QStringLiteral("W-S-s"));
        QCOMPARE(ShortcutKeyMap::toLabwcKey(ShortcutKeyMap::parseChord(QStringLiteral("Ctrl+Alt+G"))),
                 QStringLiteral("C-A-g"));
        QCOMPARE(ShortcutKeyMap::toLabwcKey(ShortcutKeyMap::parseChord(QStringLiteral("Print"))),
                 QStringLiteral("Print"));
        // labwc needs a base key — a bare modifier can't bind.
        QVERIFY(ShortcutKeyMap::toLabwcKey(ShortcutKeyMap::parseChord(QStringLiteral("Meta"))).isEmpty());
    }

    // -------- Singularity labwc rc.xml writer --------
    void labwcInsertsInsideKeyboard()
    {
        seedLabwc(labwcSeedDoc());
        const Result r = install(Backend::Singularity, sample());
        QVERIFY(r.ok);
        QCOMPARE(r.written, 2);
        const QString text = readLabwc();
        QVERIFY(text.contains(QStringLiteral(
            "<keybind key=\"W-S-s\"><action name=\"Execute\"><command>/usr/bin/unisic --hotkey capture-region</command></action></keybind>")));
        // Ours land inside <keyboard>, after the DE's binds (last bind wins in labwc).
        const int ourPos = text.indexOf(QStringLiteral("--hotkey capture-region"));
        QVERIFY(ourPos > text.indexOf(QStringLiteral("screenshot_tool")));
        QVERIFY(ourPos < text.indexOf(QStringLiteral("</keyboard>")));
        QVERIFY(present(Backend::Singularity));
    }
    void labwcPreservesDeEntriesAndIsIdempotent()
    {
        seedLabwc(labwcSeedDoc());
        QVERIFY(install(Backend::Singularity, sample()).ok);
        QVERIFY(install(Backend::Singularity, sample()).ok); // twice
        const QString text = readLabwc();
        QVERIFY(text.contains(QStringLiteral("screenshot_tool")));          // DE bind kept
        QVERIFY(text.contains(QStringLiteral("A-F4")));
        QCOMPARE(text.count(QStringLiteral("--hotkey capture-region")), 1); // no dupes
        QCOMPARE(text.count(QStringLiteral("--hotkey capture-fullscreen")), 1);
    }
    void labwcRemoveClearsOursKeepsDe()
    {
        seedLabwc(labwcSeedDoc());
        QVERIFY(install(Backend::Singularity, sample()).ok);
        QVERIFY(remove(Backend::Singularity).ok);
        const QString text = readLabwc();
        QVERIFY(text.contains(QStringLiteral("screenshot_tool")));
        QVERIFY(!text.contains(QStringLiteral("--hotkey")));
        QVERIFY(!present(Backend::Singularity));
    }
    void labwcStripsPrettyPrintedOurs()
    {
        // A reflowed multi-line variant of our entry must still be recognized.
        seedLabwc(QStringLiteral(
            "<labwc_config>\n  <keyboard>\n"
            "    <keybind key=\"W-S-s\">\n      <action name=\"Execute\">\n"
            "        <command>/usr/bin/unisic --hotkey capture-region</command>\n"
            "      </action>\n    </keybind>\n"
            "  </keyboard>\n</labwc_config>\n"));
        QVERIFY(install(Backend::Singularity, sample()).ok);
        QCOMPARE(readLabwc().count(QStringLiteral("--hotkey capture-region")), 1);
    }
    void labwcCreatesMissingFileAndKeyboard()
    {
        // No rc.xml at all (fresh session) — a valid document is created.
        QVERIFY(!QFile::exists(labwcPath()));
        QVERIFY(install(Backend::Singularity, sample()).ok);
        QString text = readLabwc();
        QVERIFY(text.contains(QStringLiteral("<labwc_config>")));
        QVERIFY(text.indexOf(QStringLiteral("--hotkey")) < text.indexOf(QStringLiteral("</keyboard>")));
        // A config without a <keyboard> section gains one before </labwc_config>.
        seedLabwc(QStringLiteral("<labwc_config>\n  <theme></theme>\n</labwc_config>\n"));
        QVERIFY(install(Backend::Singularity, sample()).ok);
        text = readLabwc();
        QVERIFY(text.contains(QStringLiteral("<theme></theme>")));
        QVERIFY(text.indexOf(QStringLiteral("<keyboard>")) < text.indexOf(QStringLiteral("--hotkey")));
        QVERIFY(text.indexOf(QStringLiteral("</keyboard>")) < text.indexOf(QStringLiteral("</labwc_config>")));
    }
    void labwcEscapesCommandXml()
    {
        // A binary path with a quote must not break the XML (shell-quoted by
        // hotkeyCommand, XML-escaped by the writer).
        QList<Binding> b = {
            {QStringLiteral("capture-region"), QStringLiteral("Region"), QStringLiteral("Print"),
             QStringLiteral("'/opt/we\"ird/unisic' --hotkey capture-region")},
        };
        QVERIFY(install(Backend::Singularity, b).ok);
        const QString text = readLabwc();
        QVERIFY(text.contains(QStringLiteral("we&quot;ird")));
        QVERIFY(!text.contains(QStringLiteral("we\"ird")));
    }
};

QTEST_MAIN(ShortcutBinderTest)
#include "ShortcutBinderTest.moc"

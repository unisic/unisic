#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include "Settings.h"
#include "ConfigPath.h"

// Settings persistence must be verified from a FRESH process: in-process
// QSettings reads are served from the QConfFile cache and can "succeed" while
// the on-disk form is broken. The [%General] case-collision was exactly that —
// "general/x" serialized as [%General], a fresh process parsed it back as
// group "General", the case-sensitive read missed, and every General-tab
// setting silently reset on each launch (see the fold in the Settings ctor).
// The round-trip test below therefore writes in this process, then re-reads
// every property in a re-exec'd child ("--verify-child <expected.json>").
class SettingsPersistenceTest : public QObject
{
    Q_OBJECT

    static void wipeConfig()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                            + QStringLiteral("/unisic");
        // Refuse to touch anything but the redirected test location.
        QVERIFY2(dir.contains(QLatin1String("qttest")),
                 qPrintable(QStringLiteral("refusing to wipe non-test config dir: ") + dir));
        QDir(dir).removeRecursively();
    }

    // A deterministic non-default value per property type.
    static QVariant nonDefault(const QMetaProperty &p, const Settings &s)
    {
        const QVariant cur = p.read(&s);
        switch (p.userType()) {
        case QMetaType::Bool: return !cur.toBool();
        case QMetaType::Int:  return cur.toInt() + 7;
        default:              return QStringLiteral("qtest_") + QString::fromLatin1(p.name());
        }
    }

private slots:
    void initTestCase()
    {
        // Redirect GenericConfigLocation under <XDG_CONFIG_HOME>/qttest so the
        // real user config is never touched (same pattern as HistoryFilterTest).
        QStandardPaths::setTestModeEnabled(true);
    }
    void init() { wipeConfig(); }

    void defaultsOnFreshConfig()
    {
        Settings s;
        QVERIFY(s.persistent());
        QCOMPARE(s.imageFormat(), QStringLiteral("png"));
        QCOMPARE(s.imageQuality(), 90);
        QCOMPARE(s.filenameTemplate(), QStringLiteral("Unisic_%date%_%time%"));
        QCOMPARE(s.filenameCounter(), 1);
        QCOMPARE(s.uiLanguage(), QStringLiteral("system"));
        QVERIFY(s.copyToClipboard());
    }

    void everyPropertyRoundTripsFreshProcess()
    {
        // Settings export/import serializes Q_PROPERTYs via the metaobject —
        // every property must survive a write → flush → fresh-process read,
        // or a setting added with a colliding key silently resets on restart.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString expectedPath = tmp.path() + QStringLiteral("/expected.json");
        {
            Settings s;
            const QMetaObject *mo = s.metaObject();
            QJsonObject expected;
            for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
                const QMetaProperty p = mo->property(i);
                QVERIFY2(p.isReadable(), p.name());
                // Read-only state (persistent) is exported but never written —
                // the import side ignores a failed write the same way.
                if (!p.isWritable())
                    continue;
                const QVariant v = nonDefault(p, s);
                QVERIFY2(p.write(&s, v), p.name());
                expected.insert(QString::fromLatin1(p.name()), QJsonValue::fromVariant(v));
            }
            s.raw()->sync();
            QCOMPARE(s.raw()->status(), QSettings::NoError);
            QFile f(expectedPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(expected).toJson());
        } // Settings + QSettings destroyed before the child runs
        QCOMPARE(QProcess::execute(QCoreApplication::applicationFilePath(),
                                   {QStringLiteral("--verify-child"), expectedPath}),
                 0);
    }

    void generalTabKeysStayTopLevel()
    {
        QString confPath;
        {
            Settings s;
            // Representative General-tab writes — the keys that once lived in
            // the fatal "general" group and reset on every launch.
            s.setUiLanguage(QStringLiteral("pl"));
            s.setSoundVolume(42);
            s.setShowNotifications(false);
            s.setMinimizeToTrayOnClose(false);
            s.setAskWhereToSave(true);
            s.raw()->sync();
            confPath = s.raw()->fileName();
        }
        QFile f(confPath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray ini = f.readAll();
        // "[%General]" is the on-disk form of the trap: written for a group
        // named "general", parsed back as group "General" by a fresh process.
        QVERIFY2(!ini.contains("[%General]"), ini.constData());
        QVERIFY2(!ini.contains("[general]"), ini.constData());
        QVERIFY2(!ini.contains("general/"), ini.constData());
        // Bare keys under the plain [General] section round-trip exactly.
        QVERIFY2(ini.contains("uiLanguage=pl"), ini.constData());
    }

    void legacyGeneralKeysFolded()
    {
        // A config written before the key-folding fix holds General-tab keys
        // in a "general" group; the ctor must fold them to top-level keys.
        {
            QSettings raw(UnisicConfig::filePath(), QSettings::IniFormat);
            raw.setValue(QStringLiteral("general/uiLanguage"), QStringLiteral("pl"));
            raw.setValue(QStringLiteral("general/soundVolume"), 42);
            raw.sync();
        }
        Settings s;
        QCOMPARE(s.uiLanguage(), QStringLiteral("pl"));
        QCOMPARE(s.soundVolume(), 42);
        for (const QString &k : s.raw()->allKeys()) {
            QVERIFY2(!k.startsWith(QLatin1String("general/"))
                         && !k.startsWith(QLatin1String("General/")),
                     qPrintable(k));
        }
    }

    void ocrPinnedSpecKeepsAutoOff()
    {
        // Upgrade path: a user who pinned ocr/languages before ocr/autoLanguage
        // existed must keep the manual spec (ctor seeds autoLanguage OFF).
        {
            QSettings raw(UnisicConfig::filePath(), QSettings::IniFormat);
            raw.setValue(QStringLiteral("ocr/languages"), QStringLiteral("eng"));
            raw.sync();
        }
        Settings s;
        QVERIFY(!s.ocrAutoLanguage());
        QCOMPARE(s.ocrLanguages(), QStringLiteral("eng"));
    }
};

// Child mode: construct a FRESH Settings (this process never saw the writes,
// so QConfFile has to parse the on-disk bytes honestly) and compare every
// property against the parent's expected values.
static int verifyChild(const QString &expectedPath)
{
    QFile f(expectedPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot read" << expectedPath;
        return 2;
    }
    const QJsonObject expected = QJsonDocument::fromJson(f.readAll()).object();
    Settings s;
    const QMetaObject *mo = s.metaObject();
    int failures = 0;
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        const int idx = mo->indexOfProperty(it.key().toLatin1().constData());
        if (idx < mo->propertyOffset()) {
            qWarning() << "no such property:" << it.key();
            ++failures;
            continue;
        }
        const QVariant got = mo->property(idx).read(&s);
        const QVariant want = it.value().toVariant();
        if (got != want) {
            qWarning() << "MISMATCH" << it.key() << "got" << got << "want" << want;
            ++failures;
        }
    }
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() == 3 && args.at(1) == QLatin1String("--verify-child"))
        return verifyChild(args.at(2));
    SettingsPersistenceTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "SettingsPersistenceTest.moc"

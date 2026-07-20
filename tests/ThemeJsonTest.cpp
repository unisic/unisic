// Pins the community-theme JSON schema: required seed colors + isDark,
// optional overrides passed through untouched, swatches validation, name
// fallback to the file name, and precise rejection reasons.
#include <QtTest>
#include <QDir>
#include <QFile>

#include "theme/ThemeJson.h"

namespace {
QByteArray minimalTheme()
{
    return QByteArrayLiteral(
        "{\"isDark\": true,"
        " \"primary\": \"#17153B\", \"secondary\": \"#2E236C\","
        " \"tertiary\": \"#433D8B\", \"accent\": \"#C8ACD6\","
        " \"bg\": \"#100E2C\", \"surface\": \"#1E1B4A\","
        " \"text\": \"#F3F0FA\", \"textOnAccent\": \"#1B1834\"}");
}
} // namespace

class ThemeJsonTest : public QObject
{
    Q_OBJECT
private slots:
    void minimalThemeParses();
    void nameFallsBackToFileName();
    void explicitNameWins();
    void optionalOverridesPassThrough();
    void unknownKeysIgnored();
    void missingRequiredKeyRejected();
    void invalidColorRejected();
    void missingIsDarkRejected();
    void nonObjectRejected();
    void swatchesValidated();
    void shippedThemesAllParse();
};

void ThemeJsonTest::minimalThemeParses()
{
    QString err;
    const QVariantMap def = ThemeJson::parse(minimalTheme(), QStringLiteral("file-stem"), &err);
    QVERIFY2(!def.isEmpty(), qPrintable(err));
    QCOMPARE(def.value(QStringLiteral("primary")).toString(), QStringLiteral("#17153B"));
    QCOMPARE(def.value(QStringLiteral("isDark")).toBool(), true);
    // Nothing optional was given — nothing optional may appear.
    QVERIFY(!def.contains(QStringLiteral("danger")));
}

void ThemeJsonTest::nameFallsBackToFileName()
{
    QString err;
    const QVariantMap def = ThemeJson::parse(minimalTheme(), QStringLiteral("my-theme"), &err);
    QCOMPARE(def.value(QStringLiteral("name")).toString(), QStringLiteral("my-theme"));
}

void ThemeJsonTest::explicitNameWins()
{
    QByteArray json = minimalTheme();
    json.replace("{", "{\"name\": \"Pretty Name\",");
    QString err;
    const QVariantMap def = ThemeJson::parse(json, QStringLiteral("file"), &err);
    QCOMPARE(def.value(QStringLiteral("name")).toString(), QStringLiteral("Pretty Name"));
}

void ThemeJsonTest::optionalOverridesPassThrough()
{
    QByteArray json = minimalTheme();
    json.replace("{", "{\"danger\": \"#FF0000\", \"surfaceHi\": \"#222244\","
                      " \"recBadgeBg\": \"#CC000000\", \"keystrokeText\": \"#FFEEEE\",");
    QString err;
    const QVariantMap def = ThemeJson::parse(json, QStringLiteral("f"), &err);
    QVERIFY2(!def.isEmpty(), qPrintable(err));
    QCOMPARE(def.value(QStringLiteral("danger")).toString(), QStringLiteral("#FF0000"));
    QCOMPARE(def.value(QStringLiteral("surfaceHi")).toString(), QStringLiteral("#222244"));
    // Recording-overlay tokens ride the same optional-override path (alpha kept).
    QCOMPARE(def.value(QStringLiteral("recBadgeBg")).toString(), QStringLiteral("#CC000000"));
    QCOMPARE(def.value(QStringLiteral("keystrokeText")).toString(), QStringLiteral("#FFEEEE"));
}

void ThemeJsonTest::unknownKeysIgnored()
{
    QByteArray json = minimalTheme();
    json.replace("{", "{\"_comment\": \"hi\", \"futureKey\": 42,");
    QString err;
    QVERIFY(!ThemeJson::parse(json, QStringLiteral("f"), &err).isEmpty());
}

void ThemeJsonTest::missingRequiredKeyRejected()
{
    QByteArray json = minimalTheme();
    json.replace("\"accent\": \"#C8ACD6\",", "");
    QString err;
    QVERIFY(ThemeJson::parse(json, QStringLiteral("f"), &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("accent")), qPrintable(err));
}

void ThemeJsonTest::invalidColorRejected()
{
    QByteArray json = minimalTheme();
    json.replace("#C8ACD6", "not-a-color-at-all");
    QString err;
    QVERIFY(ThemeJson::parse(json, QStringLiteral("f"), &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("accent")), qPrintable(err));
}

void ThemeJsonTest::missingIsDarkRejected()
{
    QByteArray json = minimalTheme();
    json.replace("\"isDark\": true,", "");
    QString err;
    QVERIFY(ThemeJson::parse(json, QStringLiteral("f"), &err).isEmpty());
    QVERIFY2(err.contains(QStringLiteral("isDark")), qPrintable(err));
}

void ThemeJsonTest::nonObjectRejected()
{
    QString err;
    QVERIFY(ThemeJson::parse(QByteArrayLiteral("[1,2]"), QStringLiteral("f"), &err).isEmpty());
    QVERIFY(ThemeJson::parse(QByteArrayLiteral("garbage"), QStringLiteral("f"), &err).isEmpty());
}

void ThemeJsonTest::swatchesValidated()
{
    QByteArray good = minimalTheme();
    good.replace("{", "{\"swatches\": [\"#FF0000\", \"#00FF00\"],");
    QString err;
    const QVariantMap def = ThemeJson::parse(good, QStringLiteral("f"), &err);
    QVERIFY2(!def.isEmpty(), qPrintable(err));
    QCOMPARE(def.value(QStringLiteral("swatches")).toStringList().size(), 2);

    QByteArray bad = minimalTheme();
    bad.replace("{", "{\"swatches\": [\"#FF0000\", 7],");
    QVERIFY(ThemeJson::parse(bad, QStringLiteral("f"), &err).isEmpty());
}

// The decorative palettes ship in resources/themes/*.json baked into qrc — a
// broken file there would brick the theme it defines at RUNTIME, so parse them
// all straight from the source tree at TEST time. UNISIC_SOURCE_DIR from CMake.
void ThemeJsonTest::shippedThemesAllParse()
{
    const QDir dir(QStringLiteral(UNISIC_SOURCE_DIR "/resources/themes"));
    QVERIFY2(dir.exists(), qPrintable(dir.absolutePath()));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    QVERIFY(files.size() >= 5);
    for (const QString &name : files) {
        QFile f(dir.filePath(name));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
        QString err;
        const QVariantMap def = ThemeJson::parse(f.readAll(), name, &err);
        QVERIFY2(!def.isEmpty(), qPrintable(name + QStringLiteral(": ") + err));
    }
}

QTEST_MAIN(ThemeJsonTest)
#include "ThemeJsonTest.moc"

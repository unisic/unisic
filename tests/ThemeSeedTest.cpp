// Pins how the shipped decorative themes reach <config>/themes: seeded once per
// FILE, never recreated after the user deletes one, and re-delivered when the
// folder itself is gone. That last case is the regression this test exists for:
// the settings key travels with a restored/copied config (and with the dev
// build's config, which is seeded key-by-key from the stable one) while the
// files do not, and the old single "already seeded" bool then hid the themes
// forever on that machine.
#include <QtTest>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include "ConfigPath.h"
#include "theme/ThemeController.h"

namespace {

// Every ThemeController reads UnisicKit::filePath() at construction, so a case
// only has to point that at its own temp config to get a clean world.
struct Sandbox {
    QTemporaryDir dir;
    QString conf() const { return dir.path() + QStringLiteral("/unisic.conf"); }
    QString themes() const { return dir.path() + QStringLiteral("/themes"); }
    void use() const { UnisicKit::setConfigFilePath(conf()); }
    QStringList files() const
    {
        return QDir(themes()).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    }
    QStringList record() const
    {
        QSettings s(conf(), QSettings::IniFormat);
        return s.value(QStringLiteral("ui/themesSeededFiles")).toStringList();
    }
};

QStringList shippedNames()
{
    return QDir(QStringLiteral(":/resources/themes"))
        .entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
}

} // namespace

class ThemeSeedTest : public QObject
{
    Q_OBJECT
private slots:
    void freshConfigGetsEveryShippedTheme();
    void restoredConfigWithoutFolderReseeds();
    void deletedThemeStaysDeleted();
    void legacyFlagKeepsAnExistingFolderUntouched();
    void aNewlyShippedThemeStillArrives();
    void systemDarkFollowsThePortal();
};

void ThemeSeedTest::freshConfigGetsEveryShippedTheme()
{
    Sandbox box;
    box.use();
    ThemeController tc;
    const QStringList shipped = shippedNames();
    QVERIFY(!shipped.isEmpty());
    QCOMPARE(box.files(), shipped);
    QCOMPARE(box.record(), shipped);
    for (const QString &name : shipped) {
        const QString stem = QFileInfo(name).completeBaseName();
        QVERIFY2(tc.customDefs().contains(QStringLiteral("custom:") + stem), qPrintable(stem));
    }
}

void ThemeSeedTest::restoredConfigWithoutFolderReseeds()
{
    Sandbox box;
    box.use();
    {   // What a config restored from a backup, a dotfiles repo or the stable
        // build looks like: the keys are there, the themes folder is not.
        QSettings s(box.conf(), QSettings::IniFormat);
        s.setValue(QStringLiteral("ui/themesSeeded"), true);
        s.setValue(QStringLiteral("ui/themesSeededFiles"), shippedNames());
        s.sync();
    }
    QVERIFY(!QDir(box.themes()).exists());
    ThemeController tc;
    QCOMPARE(box.files(), shippedNames());
}

void ThemeSeedTest::deletedThemeStaysDeleted()
{
    Sandbox box;
    box.use();
    const QString victim = shippedNames().first();
    {
        ThemeController first;
        QVERIFY(first.seededThemes().contains(victim));
    }
    QVERIFY(QFile::remove(box.themes() + QLatin1Char('/') + victim));
    ThemeController second;
    QVERIFY(!box.files().contains(victim));
    QVERIFY(second.seededThemes().contains(victim));
}

void ThemeSeedTest::legacyFlagKeepsAnExistingFolderUntouched()
{
    Sandbox box;
    box.use();
    // A folder seeded by the old bool: no per-file record, and one theme the
    // user has already deleted. Nothing may come back, and the record must be
    // backfilled so the deletion survives the next launch too.
    QDir().mkpath(box.themes());
    const QStringList shipped = shippedNames();
    for (const QString &name : shipped.mid(1))
        QVERIFY(QFile::copy(QStringLiteral(":/resources/themes/") + name,
                            box.themes() + QLatin1Char('/') + name));
    {
        QSettings s(box.conf(), QSettings::IniFormat);
        s.setValue(QStringLiteral("ui/themesSeeded"), true);
        s.sync();
    }
    ThemeController tc;
    QCOMPARE(box.files(), shipped.mid(1));
    QCOMPARE(tc.seededThemes(), shipped);
}

void ThemeSeedTest::aNewlyShippedThemeStillArrives()
{
    Sandbox box;
    box.use();
    const QStringList shipped = shippedNames();
    QVERIFY(shipped.size() > 1);
    // An install that predates the newest shipped theme: everything else is on
    // disk and recorded, the new file is in neither.
    QDir().mkpath(box.themes());
    for (const QString &name : shipped.mid(1))
        QVERIFY(QFile::copy(QStringLiteral(":/resources/themes/") + name,
                            box.themes() + QLatin1Char('/') + name));
    {
        QSettings s(box.conf(), QSettings::IniFormat);
        s.setValue(QStringLiteral("ui/themesSeeded"), true);
        s.setValue(QStringLiteral("ui/themesSeededFiles"), shipped.mid(1));
        s.sync();
    }
    ThemeController tc;
    QCOMPARE(box.files(), shipped);
    QCOMPARE(tc.seededThemes(), shipped.mid(1) + QStringList{shipped.first()});
}

// The offscreen platform's palette is light and its colour-scheme hint is
// Unknown - the same pair Qt gave on GNOME under 'prefer-dark' (#118). Only
// the portal can make systemDark right there, so with a portal that states a
// preference the two must agree. Skips where no portal answers (CI).
void ThemeSeedTest::systemDarkFollowsThePortal()
{
    QDBusMessage read = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"), QStringLiteral("ReadOne"));
    read << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(read, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        QSKIP("no desktop portal on this session bus");
    QVariant v = reply.arguments().constFirst();
    while (v.userType() == qMetaTypeId<QDBusVariant>())
        v = v.value<QDBusVariant>().variant();
    const uint scheme = v.toUInt();
    if (scheme != 1 && scheme != 2)
        QSKIP("the desktop states no light/dark preference");
    Sandbox box;
    box.use();
    ThemeController tc;
    QCOMPARE(tc.systemDark(), scheme == 1);
}

QTEST_MAIN(ThemeSeedTest)
#include "ThemeSeedTest.moc"

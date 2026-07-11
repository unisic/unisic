#include <QtTest>
#include "VersionCompare.h"

// Release tags carry a "v" prefix and may carry letter suffixes ("v0.3b" is
// published as a prerelease). The comparator must upgrade a running
// prerelease to its final release, never downgrade, and never treat an
// unparsable tag as an update.
class VersionCompareTest : public QObject
{
    Q_OBJECT
private slots:
    void isNewer_data()
    {
        QTest::addColumn<QString>("remote");
        QTest::addColumn<QString>("local");
        QTest::addColumn<bool>("expected");
        QTest::newRow("patch bump")            << "0.5.2"   << "0.5.1"  << true;
        QTest::newRow("minor bump")            << "0.6"     << "0.5.9"  << true;
        QTest::newRow("major bump v-prefixed") << "v1.0.0"  << "0.9.9"  << true;
        QTest::newRow("longer tuple")          << "0.5.1.1" << "0.5.1"  << true;
        QTest::newRow("equal, v prefix")       << "v0.5.1"  << "0.5.1"  << false;
        QTest::newRow("equal final")           << "0.5.1"   << "0.5.1"  << false;
        QTest::newRow("older remote")          << "0.5.0"   << "0.5.1"  << false;
        QTest::newRow("prerelease to final")   << "0.5.1"   << "0.5.1b" << true;
        QTest::newRow("remote prerelease of local final") << "0.5.1b" << "0.5.1"  << false;
        QTest::newRow("same prerelease")       << "0.5.1b"  << "0.5.1b" << false;
        QTest::newRow("newer despite suffix")  << "0.5.2b"  << "0.5.1"  << true;
        QTest::newRow("garbage remote")        << "latest"  << "0.5.1"  << false;
        QTest::newRow("empty remote")          << ""        << "0.5.1"  << false;
    }
    void isNewer()
    {
        QFETCH(QString, remote);
        QFETCH(QString, local);
        QFETCH(bool, expected);
        QCOMPARE(UpdateVersion::isNewer(remote, local), expected);
    }

    void numericPartSplits()
    {
        QString suffix;
        QCOMPARE(UpdateVersion::numericPart(QStringLiteral("v0.3b"), &suffix),
                 QVersionNumber(0, 3));
        QCOMPARE(suffix, QStringLiteral("b"));
        QCOMPARE(UpdateVersion::numericPart(QStringLiteral("0.5.1"), &suffix),
                 QVersionNumber(0, 5, 1));
        QCOMPARE(suffix, QString());
        QVERIFY(UpdateVersion::numericPart(QStringLiteral("latest"), &suffix).isNull());
        QCOMPARE(suffix, QString());
    }
};

QTEST_APPLESS_MAIN(VersionCompareTest)
#include "VersionCompareTest.moc"

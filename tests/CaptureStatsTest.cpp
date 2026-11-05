#include <QtTest>
#include <QTemporaryDir>
#include "history/CaptureStats.h"

// unisic.stats is read by unisic.app/stats in the browser, so the layout is a
// contract with the website: format + version + id + one count per ISO day + sig.
class CaptureStatsTest : public QObject
{
    Q_OBJECT
private slots:
    void countsPerDayMonthYear()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("unisic.stats"));

        const CaptureStats::Counts none = CaptureStats::read(path, QDate(2026, 3, 1));
        QVERIFY(none.readable);
        QCOMPARE(none.total, 0);

        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 1)));
        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 1)));
        QVERIFY(CaptureStats::record(path, QDate(2026, 2, 28)));
        QVERIFY(CaptureStats::record(path, QDate(2025, 3, 1)));

        const CaptureStats::Counts c = CaptureStats::read(path, QDate(2026, 3, 1));
        QVERIFY(c.readable);
        QCOMPARE(c.today, 2);
        QCOMPARE(c.month, 2);
        QCOMPARE(c.year, 3);
        QCOMPARE(c.total, 4);

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("format")).toString(), QStringLiteral("unisic.stats"));
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
        const QJsonObject days = root.value(QStringLiteral("screenshots")).toObject();
        QCOMPARE(days.size(), 3);
        QCOMPARE(days.value(QStringLiteral("2026-03-01")).toInt(), 2);

        // The page tells copies of one counter from separate counters by id,
        // so it must survive every write.
        const QString id = root.value(QStringLiteral("id")).toString();
        QCOMPARE(id.size(), 36);
        // Closed before the next write: QSaveFile commits by renaming over
        // the file, and Windows refuses that while a handle is still open.
        f.close();
        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 2)));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("id")).toString(), id);
    }

    void signatureMatchesTheWebsite()
    {
        // The same vector is in unisic-website scripts/stats-check.ts.
        QJsonObject days;
        days.insert(QStringLiteral("2026-01-02"), 5);
        days.insert(QStringLiteral("2026-01-01"), 2);
        QCOMPARE(CaptureStats::signature(QStringLiteral("test"), days),
                 QByteArray("42be1ff52b41208061df97a7331544d319df964347c8095588aef701bb2ba3c3"));
    }

    void editedFileIsRefusedAndKept()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("unisic.stats"));
        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 1)));
        QCOMPARE(CaptureStats::read(path, QDate(2026, 3, 1)).total, 1);

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        root.insert(QStringLiteral("screenshots"), QJsonObject{{QStringLiteral("2026-03-01"), 400}});
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson());
        f.close();

        // Not shown, and not signed again by the next capture either.
        QVERIFY(!CaptureStats::read(path, QDate(2026, 3, 1)).readable);
        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 1)));
        QCOMPARE(CaptureStats::read(path, QDate(2026, 3, 1)).total, 1);
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("unisic.stats.broken-*")}).size(), 1);
    }

    void brokenFileIsKeptNotOverwritten()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("unisic.stats"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("not json");
        }
        QVERIFY(!CaptureStats::read(path, QDate(2026, 3, 1)).readable);

        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 1)));
        QCOMPARE(CaptureStats::read(path, QDate(2026, 3, 1)).total, 1);
        const QStringList kept = QDir(dir.path()).entryList({QStringLiteral("unisic.stats.broken-*")});
        QCOMPARE(kept.size(), 1);
        QFile old(dir.filePath(kept.first()));
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("not json"));
    }

    void unreadableFileIsLeftAlone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("unisic.stats"));
        QVERIFY(CaptureStats::record(path, QDate(2026, 3, 1)));
        QFile f(path);
        const QByteArray before = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::WriteOwner));
        if (f.open(QIODevice::ReadOnly))
            QSKIP("running with permission to read a write-only file (root)");

        QVERIFY(!CaptureStats::read(path, QDate(2026, 3, 1)).readable);
        QVERIFY(!CaptureStats::record(path, QDate(2026, 3, 1)));
        QVERIFY(QDir(dir.path()).entryList({QStringLiteral("unisic.stats.broken-*")}).isEmpty());
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }
};

QTEST_GUILESS_MAIN(CaptureStatsTest)
#include "CaptureStatsTest.moc"

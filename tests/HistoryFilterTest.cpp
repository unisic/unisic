#include <QtTest>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDir>
#include <QLocale>
#include "HistoryStore.h"
#include "HistoryFilterModel.h"

// The History page searches and batch-selects through HistoryFilterModel, and
// addresses entries by id because a proxy row means a different entry after a
// filter switch. Both halves are tested here: what each filter accepts, and
// that the id-addressed batch API keeps pointing at the right entries.
class HistoryFilterTest : public QObject
{
    Q_OBJECT

private:
    static QImage sampleImage(int w = 8, int h = 6)
    {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(Qt::red);
        return img;
    }
    // Entry ids visible through the proxy, in view order.
    static QList<quint64> visibleIds(const HistoryFilterModel &f)
    {
        QList<quint64> ids;
        const QVariantList l = f.entryIds();
        for (const QVariant &v : l)
            ids << v.toULongLong();
        return ids;
    }

private slots:
    void initTestCase()
    {
        // Keep history.json and thumbs/ out of the real user data dir
        // (redirects AppDataLocation under <XDG_DATA_HOME>/qttest).
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        // HistoryStore persists to disk and loads that file in its constructor,
        // so without this every test function inherits the previous RUN's
        // entries. Path-bearing entries self-prune (their files are gone), but a
        // pathless one is kept by design and would accumulate run after run.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        // Refuse to touch anything but the redirected test location.
        QVERIFY2(dir.contains(QLatin1String("qttest")),
                 qPrintable(QStringLiteral("refusing to wipe non-test data dir: ") + dir));
        QDir(dir).removeRecursively();
    }

    void filters()
    {
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);

        // Newest first, so the ids come back in reverse insertion order.
        const quint64 shot = store.addEntry(QStringLiteral("/tmp/unisic-alpha.png"), sampleImage(),
                                            QStringLiteral("image"));
        const quint64 clip = store.addEntry(QStringLiteral("/tmp/unisic-beta.gif"), sampleImage(),
                                            QStringLiteral("gif"));
        const quint64 vid = store.addEntry(QStringLiteral("/tmp/unisic-gamma.mp4"), sampleImage(),
                                           QStringLiteral("video"));
        const quint64 shared = store.addEntry(QStringLiteral("/tmp/unisic-delta.png"), sampleImage(),
                                              QStringLiteral("image"),
                                              QStringLiteral("https://example.invalid/zzz.png"));
        QCOMPARE(filter.count(), 4);
        QVERIFY(!filter.filtering());

        filter.setKindFilter(QStringLiteral("image"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{shared, shot}));
        QVERIFY(filter.filtering());

        filter.setKindFilter(QStringLiteral("gif"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{clip}));
        filter.setKindFilter(QStringLiteral("video"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{vid}));
        filter.setKindFilter(QString());
        QCOMPARE(filter.count(), 4);

        filter.setUploadedOnly(true);
        QCOMPARE(visibleIds(filter), (QList<quint64>{shared}));
        filter.setUploadedOnly(false);

        filter.setFavoritesOnly(true);
        QCOMPARE(filter.count(), 0);
        // dynamicSortFilter: starring an entry must pull it into the filter
        // without a manual invalidate.
        store.setFavoriteByIds({QVariant(clip)}, true);
        QCOMPARE(visibleIds(filter), (QList<quint64>{clip}));
        filter.setFavoritesOnly(false);
        store.setFavoriteByIds({QVariant(clip)}, false);

        // Search matches the file name and the upload URL, case-insensitively.
        filter.setSearchText(QStringLiteral("BETA"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{clip}));
        filter.setSearchText(QStringLiteral("zzz"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{shared}));
        // Not the directory: every capture shares it, so matching it would make
        // most searches match everything.
        filter.setSearchText(QStringLiteral("/tmp/"));
        QCOMPARE(filter.count(), 0);
        filter.setSearchText(QString());
        QVERIFY(!filter.filtering());

        // Combined filters intersect.
        filter.setKindFilter(QStringLiteral("image"));
        filter.setUploadedOnly(true);
        QCOMPARE(visibleIds(filter), (QList<quint64>{shared}));
    }

    void replayIsItsOwnCategory()
    {
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);

        // A saved instant replay is an .mp4 with kind "video" like any other
        // recording — only the entry's origin separates the two.
        const quint64 recording = store.addEntry(QStringLiteral("/tmp/Unisic_2026-01-01.mp4"),
                                                 sampleImage(), QStringLiteral("video"));
        const quint64 replay = store.addEntry(QStringLiteral("/tmp/Unisic_Replay_2026-01-01.mp4"),
                                              sampleImage(), QStringLiteral("video"), {}, {},
                                              QStringLiteral("replay"));
        // Entries written before 0.7.1 have no origin: the replay export's file
        // name stands in, so clips already in someone's history are categorized.
        const quint64 legacyReplay = store.addEntry(QStringLiteral("/tmp/Unisic_Replay_2025-12-31.mp4"),
                                                    sampleImage(), QStringLiteral("video"));

        QCOMPARE(store.data(store.index(2), HistoryStore::CategoryRole).toString(),
                 QStringLiteral("video"));
        QCOMPARE(store.data(store.index(1), HistoryStore::CategoryRole).toString(),
                 QStringLiteral("replay"));
        QCOMPARE(store.data(store.index(0), HistoryStore::CategoryRole).toString(),
                 QStringLiteral("replay"));
        // The media kind itself must NOT change — a replay still trims and plays
        // like the video it is.
        QCOMPARE(store.data(store.index(1), HistoryStore::KindRole).toString(),
                 QStringLiteral("video"));

        filter.setKindFilter(QStringLiteral("replay"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{legacyReplay, replay}));
        // ...and the Recordings chip must not show them again.
        filter.setKindFilter(QStringLiteral("video"));
        QCOMPARE(visibleIds(filter), (QList<quint64>{recording}));
    }

    void replayOriginSurvivesReload()
    {
        // origin is persisted, so a clip stays a clip across restarts (its file
        // name is only the fallback for entries written before the field existed).
        // A REAL file: a reloaded store prunes entries whose capture is gone.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("renamed-by-user.mp4"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
        {
            HistoryStore store;
            store.addEntry(path, sampleImage(), QStringLiteral("video"), {}, {},
                           QStringLiteral("replay"));
            QCOMPARE(store.data(store.index(0), HistoryStore::CategoryRole).toString(),
                     QStringLiteral("replay"));
        }   // destructor flushes the pending write
        HistoryStore reloaded;
        QCOMPARE(reloaded.rowCount(), 1);
        QCOMPARE(reloaded.data(reloaded.index(0), HistoryStore::CategoryRole).toString(),
                 QStringLiteral("replay"));
    }

    void rangeSelection()
    {
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);
        QList<quint64> ids;
        for (int i = 0; i < 5; ++i)
            ids << store.addEntry(QStringLiteral("/tmp/unisic-%1.png").arg(i), sampleImage(),
                                  QStringLiteral("image"));
        std::reverse(ids.begin(), ids.end());   // view order: newest first

        // Shift-click hands the range in whichever direction it was drawn.
        const QVariantList down = filter.entryIdsBetween(1, 3);
        const QVariantList up = filter.entryIdsBetween(3, 1);
        QCOMPARE(down.size(), 3);
        QCOMPARE(down.first().toULongLong(), ids[1]);
        QCOMPARE(down.last().toULongLong(), ids[3]);
        QCOMPARE(up, down);
        // Out-of-range ends clamp instead of returning junk rows.
        QCOMPARE(filter.entryIdsBetween(-5, 99).size(), 5);
        QVERIFY(filter.entryIdsBetween(7, 9).isEmpty());
    }

    void idsSurviveFiltering()
    {
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);
        const quint64 png = store.addEntry(QStringLiteral("/tmp/unisic-keep.png"), sampleImage(),
                                           QStringLiteral("image"));
        const quint64 gif = store.addEntry(QStringLiteral("/tmp/unisic-drop.gif"), sampleImage(),
                                           QStringLiteral("gif"));

        // Row 0 is the gif unfiltered and the png once recordings are filtered
        // out — the reason the page selects by id and not by row.
        QCOMPARE(filter.index(0, 0).data(HistoryStore::EntryIdRole).toULongLong(), gif);
        filter.setKindFilter(QStringLiteral("image"));
        QCOMPARE(filter.index(0, 0).data(HistoryStore::EntryIdRole).toULongLong(), png);

        QCOMPARE(store.entryById(png).value(QStringLiteral("filePath")).toString(),
                 QStringLiteral("/tmp/unisic-keep.png"));
        QCOMPARE(store.entryById(gif).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("gif"));
        QVERIFY(store.entryById(9999).isEmpty());
    }

    void batchDeleteSkipsStarred()
    {
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);
        const quint64 plain = store.addEntry(QStringLiteral("/tmp/unisic-plain.png"), sampleImage(),
                                             QStringLiteral("image"));
        const quint64 starred = store.addEntry(QStringLiteral("/tmp/unisic-starred.png"), sampleImage(),
                                               QStringLiteral("image"));
        store.setFavoriteByIds({QVariant(starred)}, true);

        // Same protection as remove(): a starred entry in the selection is kept,
        // not trashed, so a batch delete can't take a favorite with it.
        store.removeByIds({QVariant(plain), QVariant(starred)});
        QCOMPARE(visibleIds(filter), (QList<quint64>{starred}));
        QVERIFY(store.entryById(plain).isEmpty());

        store.setFavoriteByIds({QVariant(starred)}, false);
        store.removeByIds({QVariant(starred)});
        QCOMPARE(filter.count(), 0);
    }

    void sizeText()
    {
        // The tile has one line for time + dimensions + size, so the size string
        // trades precision for length as it grows: two significant-ish digits
        // under 10 units, none above. ("832,03 KiB" is what pushed the size out
        // of the tile before.)
        QLocale::setDefault(QLocale::c());   // assert digits, not the locale's comma
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);

        struct Case { qint64 bytes; const char *expected; };
        const QList<Case> cases{
            {761, "761 bytes"},
            {852000, "832 KiB"},           // was "832.03 KiB"
            {6752000, "6.4 MiB"},          // under 10 units: one decimal survives
            {52428800, "50 MiB"},
        };
        for (const Case &c : cases) {
            const QString path = dir.filePath(QStringLiteral("f%1.bin").arg(c.bytes));
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.resize(c.bytes));
            f.close();
            const quint64 id = store.addEntry(path, sampleImage(), QStringLiteral("image"));
            const int row = filter.rowCount() - 1 >= 0 ? 0 : -1;
            QVERIFY(row == 0);
            QCOMPARE(filter.index(0, 0).data(HistoryStore::SizeTextRole).toString(),
                     QString::fromLatin1(c.expected));
            store.removeByIds({QVariant(id)});
        }

        // An unsaved capture has no file to measure — the tile drops the field
        // rather than showing "0 bytes".
        store.addEntry(QString(), sampleImage(), QStringLiteral("image"));
        QVERIFY(filter.index(0, 0).data(HistoryStore::SizeTextRole).toString().isEmpty());
    }

    void timestampAt()
    {
        HistoryStore store;
        HistoryFilterModel filter;
        filter.setSourceModel(&store);
        store.addEntry(QStringLiteral("/tmp/unisic-ts.png"), sampleImage(), QStringLiteral("image"));
        // The sticky date header reads the topmost row's date this way.
        QVERIFY(filter.timestampAt(0).isValid());
        QVERIFY(!filter.timestampAt(-1).isValid());
        QVERIFY(!filter.timestampAt(5).isValid());
    }

    void dimCachePrunedOnRemoval()
    {
        // DimensionsRole caches per path; removing an entry must drop its cache
        // slot — both so the hash stays bounded by the live entry set and so a
        // later capture saved to the SAME path is not answered with the stale
        // size of the deleted file.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        HistoryStore store;
        const QString path = dir.filePath(QStringLiteral("recaptured.png"));

        QVERIFY(sampleImage(8, 6).save(path));
        const quint64 id = store.addEntry(path, sampleImage(), QStringLiteral("image"));
        QCOMPARE(store.data(store.index(0), HistoryStore::DimensionsRole).toString(),
                 QStringLiteral("8×6"));

        // Remove the file first so removeByIds has nothing to move to trash.
        QVERIFY(QFile::remove(path));
        store.removeByIds({QVariant(id)});
        QCOMPARE(store.rowCount(), 0);
        QVERIFY(sampleImage(16, 4).save(path));
        store.addEntry(path, sampleImage(), QStringLiteral("image"));
        QCOMPARE(store.data(store.index(0), HistoryStore::DimensionsRole).toString(),
                 QStringLiteral("16×4"));
    }
};

QTEST_MAIN(HistoryFilterTest)
#include "HistoryFilterTest.moc"

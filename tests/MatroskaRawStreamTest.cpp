#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include "record/MatroskaRawStream.h"

// The recorder's stdin framing. The byte layout is checked here, and the
// contract that matters - ffmpeg reading it back with OUR timestamps and the
// right channel order - is checked against the real ffmpeg, since a header
// that parses here but not there is exactly the failure this has to catch.
class MatroskaRawStreamTest : public QObject
{
    Q_OBJECT
private slots:
    void fourCCFollowsPixFmt();
    void clusterSizeCoversFrame();
    void ffmpegKeepsTimestamps();
    void ffmpegKeepsChannelOrder_data();
    void ffmpegKeepsChannelOrder();

private:
    static bool haveFfmpeg()
    {
        return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
            && !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
    }
    // Every pixel the same 4 bytes, so any decoded pixel tells which byte
    // landed in which channel.
    static QByteArray stream(const QString &pixFmt, const QList<qint64> &ptsMs,
                             const QByteArray &pixel)
    {
        const int w = 16, h = 8;
        QByteArray out = MatroskaRawStream::header(w, h, pixFmt, 30);
        const QByteArray frame = pixel.repeated(w * h);
        for (qint64 pts : ptsMs)
            out += MatroskaRawStream::frameHeader(pts, frame.size()) + frame;
        return out;
    }
    static QByteArray run(const QString &program, const QStringList &args,
                          const QByteArray &input)
    {
        QProcess p;
        p.start(program, args);
        if (!p.waitForStarted(5000))
            return {};
        p.write(input);
        p.closeWriteChannel();
        if (!p.waitForFinished(20000) || p.exitCode() != 0)
            return {};
        return p.readAllStandardOutput();
    }
};

void MatroskaRawStreamTest::fourCCFollowsPixFmt()
{
    QCOMPARE(MatroskaRawStream::fourCC(QStringLiteral("bgra")), QByteArray("BGRA"));
    QCOMPARE(MatroskaRawStream::fourCC(QStringLiteral("rgba")), QByteArray("RGBA"));
    QCOMPARE(MatroskaRawStream::fourCC(QStringLiteral("bgr0")), QByteArray("BGR\0", 4));
    QCOMPARE(MatroskaRawStream::fourCC(QStringLiteral("rgb0")), QByteArray("RGB\0", 4));
}

void MatroskaRawStreamTest::clusterSizeCoversFrame()
{
    // Cluster ID (4) + 8-byte size, then everything the size claims: the
    // demuxer skips by that number, so one byte off desyncs every frame after.
    const qsizetype frameBytes = 3840 * 2160 * 4;
    const QByteArray head = MatroskaRawStream::frameHeader(1234, frameBytes);
    QCOMPARE(head.left(4), QByteArray("\x1F\x43\xB6\x75", 4));
    QCOMPARE(quint8(head.at(4)), quint8(0x01));
    quint64 size = 0;
    for (int i = 5; i < 12; ++i)
        size = (size << 8) | quint8(head.at(i));
    QCOMPARE(size, quint64(head.size() - 12) + quint64(frameBytes));
}

void MatroskaRawStreamTest::ffmpegKeepsTimestamps()
{
    if (!haveFfmpeg())
        QSKIP("no ffmpeg/ffprobe in PATH");
    // A 1 ms burst then a 100 ms gap: rawvideo with wallclock timestamps
    // collapsed the burst onto one 1/fps tick and dropped it, and filled a gap
    // by deleting time. Both must come back exactly as written.
    const QList<qint64> pts{0, 1, 2, 3, 33, 133, 166};
    const QByteArray out = run(QStringLiteral("ffprobe"),
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-f"), QStringLiteral("matroska"), QStringLiteral("-i"), QStringLiteral("-"),
         QStringLiteral("-select_streams"), QStringLiteral("v"),
         QStringLiteral("-show_entries"), QStringLiteral("frame=pts_time"),
         QStringLiteral("-of"), QStringLiteral("csv=p=0")},
        stream(QStringLiteral("bgra"), pts, QByteArray("\x10\x20\x30\xFF", 4)));
    const QList<QByteArray> lines = out.trimmed().split('\n');
    QCOMPARE(lines.size(), pts.size());
    for (int i = 0; i < pts.size(); ++i)
        QCOMPARE(qRound64(lines.at(i).trimmed().toDouble() * 1000), pts.at(i));
}

void MatroskaRawStreamTest::ffmpegKeepsChannelOrder_data()
{
    QTest::addColumn<QString>("pixFmt");
    QTest::addColumn<QByteArray>("rgb");
    // Bytes in memory are 0x10 0x20 0x30 0xFF; the expected RGB is what that
    // order means for each format.
    QTest::newRow("bgra") << QStringLiteral("bgra") << QByteArray("\x30\x20\x10", 3);
    QTest::newRow("bgr0") << QStringLiteral("bgr0") << QByteArray("\x30\x20\x10", 3);
    QTest::newRow("rgba") << QStringLiteral("rgba") << QByteArray("\x10\x20\x30", 3);
    QTest::newRow("rgb0") << QStringLiteral("rgb0") << QByteArray("\x10\x20\x30", 3);
}

void MatroskaRawStreamTest::ffmpegKeepsChannelOrder()
{
    if (!haveFfmpeg())
        QSKIP("no ffmpeg/ffprobe in PATH");
    QFETCH(QString, pixFmt);
    QFETCH(QByteArray, rgb);
    const QByteArray out = run(QStringLiteral("ffmpeg"),
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-f"), QStringLiteral("matroska"), QStringLiteral("-i"), QStringLiteral("-"),
         QStringLiteral("-frames:v"), QStringLiteral("1"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"),
         QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"), QStringLiteral("-")},
        stream(pixFmt, {0}, QByteArray("\x10\x20\x30\xFF", 4)));
    QVERIFY2(out.size() >= 3, "ffmpeg decoded nothing");
    QCOMPARE(out.left(3), rgb);
}

QTEST_GUILESS_MAIN(MatroskaRawStreamTest)
#include "MatroskaRawStreamTest.moc"

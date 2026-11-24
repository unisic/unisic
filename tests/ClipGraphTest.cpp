#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "record/ClipGraph.h"

// The clip export's filter graph. The string checks pin its shape; the
// ffmpeg run proves the graph is one ffmpeg really accepts and that the
// joined file is as long and as large as the kept spans and the frame say.
class ClipGraphTest : public QObject
{
    Q_OBJECT

    static QString probe(const QString &file, const QString &entries, const QString &streams)
    {
        QProcess p;
        p.start(QStringLiteral("ffprobe"),
                {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), streams,
                 QStringLiteral("-show_entries"), entries, QStringLiteral("-of"),
                 QStringLiteral("csv=p=0"), file});
        p.waitForFinished(10000);
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    }

private slots:
    void tailCropsEvenAndNeverUpscales()
    {
        QCOMPARE(ClipGraph::videoTail(1920, 1080, 0, 0, 0, 0, 0),
                 QStringLiteral("crop=trunc(iw/2)*2:trunc(ih/2)*2"));
        // 9:16 out of 1080p, then 480 on the short side; 720 would upscale.
        QCOMPARE(ClipGraph::videoTail(1920, 1080, 656, 0, 607, 1080, 480),
                 QStringLiteral("crop=606:1080:656:0,scale=480:856"));
        QCOMPARE(ClipGraph::videoTail(1920, 1080, 656, 0, 607, 1080, 720),
                 QStringLiteral("crop=606:1080:656:0"));
        QCOMPARE(ClipGraph::videoTail(1280, 720, 0, 0, 0, 0, 1080),
                 QStringLiteral("crop=trunc(iw/2)*2:trunc(ih/2)*2"));
    }

    void remixRebuildsTheMixFromKeptStems()
    {
        const auto g = ClipGraph::build({{0, 1}, {2, 3}}, {}, {1.0, -1.0, 0.5}, 0,
                                        {QStringLiteral("Mix"), QStringLiteral("Desktop"),
                                         QStringLiteral("Mic")}, QStringLiteral("Mix"));
        // Track 0 is the mix (rebuilt), track 1 is dropped: only track 2 is cut.
        QVERIFY(!g.filter.contains(QStringLiteral("[0:a:0]")));
        QVERIFY(!g.filter.contains(QStringLiteral("[0:a:1]")));
        QVERIFY(g.filter.contains(QStringLiteral("concat=n=2:v=1:a=1[vc][c2]")));
        QVERIFY(g.filter.contains(QStringLiteral("[c2]volume=0.500,asplit=2[o2][m2]")));
        QCOMPARE(g.audioArgs.mid(0, 2), (QStringList{QStringLiteral("-map"), QStringLiteral("[mix]")}));
        QVERIFY(g.audioArgs.contains(QStringLiteral("title=Mic")));
    }

    void ffmpegJoinsTheSpans()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
            QSKIP("ffmpeg/ffprobe not installed");
        QTemporaryDir dir;
        const QString src = dir.filePath(QStringLiteral("src.mkv"));
        const QString out = dir.filePath(QStringLiteral("out.mkv"));
        QCOMPARE(QProcess::execute(QStringLiteral("ffmpeg"),
            {QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=size=320x180:rate=30:duration=6"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=6"),
             QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=880:duration=6"),
             QStringLiteral("-map"), QStringLiteral("0:v"), QStringLiteral("-map"), QStringLiteral("1:a"),
             QStringLiteral("-map"), QStringLiteral("2:a"), QStringLiteral("-c:v"), QStringLiteral("ffv1"),
             QStringLiteral("-c:a"), QStringLiteral("flac"), src}), 0);

        // Keep 0.5-1.5 and 3-4.5 (2.5 s), a 1:1 frame, both tracks, one at 50%.
        const auto g = ClipGraph::build({{0.5, 1.5}, {3.0, 4.5}},
                                        ClipGraph::videoTail(320, 180, 70, 0, 180, 180, 0),
                                        {1.0, 0.5}, -1, {QStringLiteral("A"), QStringLiteral("B")}, {});
        const QStringList args = QStringList{QStringLiteral("-v"), QStringLiteral("error"),
                                             QStringLiteral("-i"), src,
                                             QStringLiteral("-filter_complex"), g.filter,
                                             QStringLiteral("-map"), QStringLiteral("[v]"),
                                             QStringLiteral("-c:v"), QStringLiteral("ffv1")}
                                 + g.audioArgs
                                 + QStringList{QStringLiteral("-c:a"), QStringLiteral("flac"), out};
        QCOMPARE(QProcess::execute(QStringLiteral("ffmpeg"), args), 0);

        QCOMPARE(probe(out, QStringLiteral("stream=width,height"), QStringLiteral("v:0")),
                 QStringLiteral("180,180"));
        const double dur = probe(out, QStringLiteral("format=duration"), QStringLiteral("v:0")).toDouble();
        QVERIFY2(qAbs(dur - 2.5) < 0.1, qPrintable(QString::number(dur)));
        QCOMPARE(probe(out, QStringLiteral("stream=index"), QStringLiteral("a")).split(QLatin1Char('\n')).size(), 2);
    }
};

QTEST_GUILESS_MAIN(ClipGraphTest)
#include "ClipGraphTest.moc"

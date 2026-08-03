#include <QtTest>

#include "record/VideoQuality.h"

using namespace UnisicVideo;

// The percent <-> CRF scale. Two things have to hold for the setting to be
// honest: every CRF a config could already hold survives the one-time
// migration into a percent unchanged, and the percent the user reads back is
// the percent that reaches ffmpeg.
class VideoQualityTest : public QObject
{
    Q_OBJECT

private slots:
    void anchorsAreExact()
    {
        QCOMPARE(crfFromPercent(0), 40);
        QCOMPARE(crfFromPercent(50), 20);   // the pre-percent default CRF
        QCOMPARE(crfFromPercent(100), 0);
        QCOMPARE(percentFromCrf(40), 0);
        QCOMPARE(percentFromCrf(20), 50);
        QCOMPARE(percentFromCrf(0), 100);
    }

    void everyStoredCrfSurvivesTheMigration()
    {
        // crf -> percent -> crf is the exact path a 0.8.2 config takes: the
        // constructor rewrites the stored CRF as a percent once, and every
        // later encode derives the CRF back from it.
        for (int crf = 0; crf <= kCrfWorst; ++crf)
            QCOMPARE(crfFromPercent(percentFromCrf(crf)), crf);
    }

    void higherPercentNeverMeansWorseQuality()
    {
        int previous = crfFromPercent(0);
        for (int p = 1; p <= 100; ++p) {
            const int crf = crfFromPercent(p);
            QVERIFY2(crf <= previous, qPrintable(QStringLiteral("percent %1: crf %2 > %3")
                                                     .arg(p).arg(crf).arg(previous)));
            previous = crf;
        }
    }

    void outOfRangeInputClamps()
    {
        QCOMPARE(crfFromPercent(-1000), 40);
        QCOMPARE(crfFromPercent(1000), 0);
        // 41-51 exist in x264 but not on this scale; they read as the floor
        // rather than as a negative percent.
        QCOMPARE(percentFromCrf(51), 0);
        QCOMPARE(percentFromCrf(-5), 100);
    }
};

QTEST_APPLESS_MAIN(VideoQualityTest)
#include "VideoQualityTest.moc"

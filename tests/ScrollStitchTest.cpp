#include <QTest>
#include <QImage>
#include <QPainter>
#include "capture/ScrollStitcher.h"

class ScrollStitchTest : public QObject
{
    Q_OBJECT

private slots:
    void initialFrameSetsCanvas();
    void identicalFrameIgnored();
    void singleScrollDownStitchesCorrectly();
    void multiStepScrollReconstructsOriginal();
    void stickyHeaderHandling();
    void scrollbarInMarginIgnored();
    void upwardScrollHandling();

private:
    static QImage createTestDocument(int width, int height);
};

QImage ScrollStitchTest::createTestDocument(int width, int height)
{
    QImage doc(width, height, QImage::Format_RGB32);
    doc.fill(Qt::white);

    QPainter p(&doc);
    QFont f = p.font();
    f.setPixelSize(14);
    p.setFont(f);

    for (int y = 20; y < height - 10; y += 25) {
        // Draw colored bars and text lines
        p.setPen(Qt::NoPen);
        p.setBrush(QColor((y * 13) % 200, (y * 37) % 200, (y * 73) % 200));
        p.drawRect(20, y - 10, width - 60, 4);

        p.setPen(Qt::black);
        p.drawText(25, y + 10, QStringLiteral("Line at offset %1 with random text pattern ABCDEFG").arg(y));
    }
    p.end();

    return doc;
}

void ScrollStitchTest::initialFrameSetsCanvas()
{
    const QImage doc = createTestDocument(300, 600);
    const QImage frame0 = doc.copy(0, 0, 300, 200);

    ScrollStitcher stitcher;
    QCOMPARE(stitcher.frameCount(), 0);
    QVERIFY(stitcher.isEmpty());

    const bool added = stitcher.addFrame(frame0);
    QVERIFY(added);
    QCOMPARE(stitcher.frameCount(), 1);
    QCOMPARE(stitcher.stitchedWidth(), 300);
    QCOMPARE(stitcher.stitchedHeight(), 200);

    const QImage result = stitcher.stitchedImage();
    QCOMPARE(result.size(), QSize(300, 200));
    QCOMPARE(result, frame0);

    const QImage thumb = stitcher.previewThumbnail(100, 100);
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 100);
    QVERIFY(thumb.height() <= 100);
}

void ScrollStitchTest::identicalFrameIgnored()
{
    const QImage doc = createTestDocument(300, 600);
    const QImage frame0 = doc.copy(0, 0, 300, 200);

    ScrollStitcher stitcher;
    stitcher.addFrame(frame0);

    // Feed identical frame (user hasn't scrolled)
    const bool added = stitcher.addFrame(frame0);
    QVERIFY(!added);
    QCOMPARE(stitcher.frameCount(), 1);
    QCOMPARE(stitcher.stitchedHeight(), 200);
}

void ScrollStitchTest::singleScrollDownStitchesCorrectly()
{
    const QImage doc = createTestDocument(360, 800);
    const int viewportH = 250;
    const int shift = 50;

    const QImage frame0 = doc.copy(0, 0, 360, viewportH);
    const QImage frame1 = doc.copy(0, shift, 360, viewportH);

    double conf = 0.0;
    const int detected = ScrollStitcher::detectVerticalShift(frame0, frame1, ScrollStitcher::Direction::Down, &conf);
    QCOMPARE(detected, shift);
    QVERIFY(conf > 0.6);

    ScrollStitcher stitcher;
    stitcher.addFrame(frame0);
    const bool added = stitcher.addFrame(frame1);
    QVERIFY(added);

    QCOMPARE(stitcher.frameCount(), 2);
    QCOMPARE(stitcher.stitchedHeight(), viewportH + shift);

    // Verify pixel accuracy against ground truth document
    const QImage stitched = stitcher.stitchedImage();
    const QImage groundTruth = doc.copy(0, 0, 360, viewportH + shift);
    QCOMPARE(stitched, groundTruth);
}

void ScrollStitchTest::multiStepScrollReconstructsOriginal()
{
    const QImage doc = createTestDocument(400, 1000);
    const int viewportH = 300;
    const QVector<int> scrollSteps = {40, 65, 80, 55};

    ScrollStitcher stitcher;
    stitcher.addFrame(doc.copy(0, 0, 400, viewportH));

    int currentY = 0;
    for (int step : scrollSteps) {
        currentY += step;
        const QImage frame = doc.copy(0, currentY, 400, viewportH);
        const bool added = stitcher.addFrame(frame);
        QVERIFY(added);
    }

    QCOMPARE(stitcher.frameCount(), scrollSteps.size() + 1);
    const int expectedTotalH = viewportH + currentY;
    QCOMPARE(stitcher.stitchedHeight(), expectedTotalH);

    const QImage stitched = stitcher.stitchedImage();
    const QImage groundTruth = doc.copy(0, 0, 400, expectedTotalH);
    QCOMPARE(stitched, groundTruth);
}

void ScrollStitchTest::stickyHeaderHandling()
{
    // Document with fixed 35px sticky navbar at top
    const int w = 350;
    const int h = 700;
    const QImage doc = createTestDocument(w, h);

    auto makeFrameWithStickyHeader = [&](int contentY) {
        QImage f = doc.copy(0, contentY, w, 250);
        // Paint sticky header on top 35px
        QPainter fp(&f);
        fp.fillRect(0, 0, w, 35, QColor(30, 30, 60));
        fp.setPen(Qt::white);
        fp.drawText(15, 22, QStringLiteral("Fixed Top Navigation Bar"));
        fp.end();
        return f;
    };

    const QImage frame0 = makeFrameWithStickyHeader(0);
    const QImage frame1 = makeFrameWithStickyHeader(45);

    double conf = 0.0;
    const int detected = ScrollStitcher::detectVerticalShift(frame0, frame1, ScrollStitcher::Direction::Down, &conf);
    QCOMPARE(detected, 45);
    QVERIFY(conf > 0.6);

    ScrollStitcher stitcher;
    stitcher.addFrame(frame0);
    const bool added = stitcher.addFrame(frame1);
    QVERIFY(added);
    QCOMPARE(stitcher.stitchedHeight(), 250 + 45);
}

void ScrollStitchTest::scrollbarInMarginIgnored()
{
    const QImage doc = createTestDocument(350, 700);
    const int viewportH = 260;
    const int shift = 50;

    QImage frame0 = doc.copy(0, 0, 350, viewportH);
    QImage frame1 = doc.copy(0, shift, 350, viewportH);

    // Draw simulated scrollbars on the right 16px of each frame
    QPainter p0(&frame0);
    p0.fillRect(350 - 16, 0, 16, viewportH, QColor(220, 220, 220));
    p0.fillRect(350 - 14, 10, 12, 40, QColor(100, 100, 100)); // thumb at top
    p0.end();

    QPainter p1(&frame1);
    p1.fillRect(350 - 16, 0, 16, viewportH, QColor(220, 220, 220));
    p1.fillRect(350 - 14, 70, 12, 40, QColor(100, 100, 100)); // thumb moved down!
    p1.end();

    double conf = 0.0;
    const int detected = ScrollStitcher::detectVerticalShift(frame0, frame1, ScrollStitcher::Direction::Down, &conf);
    QCOMPARE(detected, shift);
    QVERIFY(conf > 0.6);
}

void ScrollStitchTest::upwardScrollHandling()
{
    const QImage doc = createTestDocument(350, 700);
    const int viewportH = 250;
    const int shift = 40;

    const QImage frameAt100 = doc.copy(0, 100, 350, viewportH);
    const QImage frameAt60 = doc.copy(0, 100 - shift, 350, viewportH); // scrolled UP by 40

    double conf = 0.0;
    const int detected = ScrollStitcher::detectVerticalShift(frameAt100, frameAt60, ScrollStitcher::Direction::Up, &conf);
    QCOMPARE(detected, -shift);
    QVERIFY(conf > 0.6);

    ScrollStitcher stitcher;
    stitcher.setDirection(ScrollStitcher::Direction::Up);
    stitcher.addFrame(frameAt100);
    const bool added = stitcher.addFrame(frameAt60);
    QVERIFY(added);
    QCOMPARE(stitcher.stitchedHeight(), viewportH + shift);

    // The result should match doc.copy(0, 60, 350, 290)
    const QImage stitched = stitcher.stitchedImage();
    const QImage groundTruth = doc.copy(0, 60, 350, viewportH + shift);
    QCOMPARE(stitched, groundTruth);
}

QTEST_MAIN(ScrollStitchTest)
#include "ScrollStitchTest.moc"

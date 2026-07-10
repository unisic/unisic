#include "ObjectDetector.h"

#include <QPainter>
#include <QTest>
#include <algorithm>

class ObjectDetectorTest : public QObject
{
    Q_OBJECT

private slots:
    void detectsHighContrastObject();
    void detectsNestedRects();
    void segmentsCenteredObject();
    void rejectsInvalidSegmentationRegion();
};

static bool hasRectNear(const QVector<QRect> &candidates, const QRect &want, int tol)
{
    return std::any_of(candidates.cbegin(), candidates.cend(), [&](const QRect &r) {
        return qAbs(r.left() - want.left()) <= tol && qAbs(r.top() - want.top()) <= tol
            && qAbs(r.right() - want.right()) <= tol && qAbs(r.bottom() - want.bottom()) <= tol;
    });
}

void ObjectDetectorTest::detectsHighContrastObject()
{
    QImage image(320, 200, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.fillRect(QRect(80, 50, 150, 100), QColor(30, 60, 90));
    painter.end();

    const QVector<QRect> candidates = ObjectDetector::detect(image);
    QVERIFY(!candidates.isEmpty());

    // Not just "something under the point" — the drawn rect itself, with
    // accurate (edge-snapped) borders.
    QVERIFY(hasRectNear(candidates, QRect(80, 50, 150, 100), 4));

    // The whole image is always the outermost candidate (the scroll-up end
    // of the nesting chain).
    QVERIFY(candidates.last() == image.rect());
}

void ObjectDetectorTest::detectsNestedRects()
{
    // A window-like container with an inner panel: both must be found, area-
    // sorted inner-first — the overlay's nesting chain depends on that order.
    QImage image(640, 400, QImage::Format_RGB32);
    image.fill(QColor(0x17, 0x15, 0x3B));

    QPainter painter(&image);
    painter.fillRect(QRect(60, 40, 500, 300), QColor(0xEC, 0xEC, 0xF4));   // window
    painter.fillRect(QRect(120, 90, 200, 120), QColor(0x43, 0x3D, 0x8B));  // panel
    painter.end();

    const QVector<QRect> candidates = ObjectDetector::detect(image);
    QVERIFY(hasRectNear(candidates, QRect(60, 40, 500, 300), 6));
    QVERIFY(hasRectNear(candidates, QRect(120, 90, 200, 120), 6));

    const QPoint inner(200, 140);
    QVector<QRect> chain;
    for (const QRect &r : candidates)
        if (r.contains(inner))
            chain.append(r);
    QVERIFY(chain.size() >= 3); // panel, window, whole screen
    for (int i = 1; i < chain.size(); ++i)
        QVERIFY(qint64(chain[i].width()) * chain[i].height()
                >= qint64(chain[i-1].width()) * chain[i-1].height());
}

void ObjectDetectorTest::segmentsCenteredObject()
{
    QImage image(300, 220, QImage::Format_RGB32);
    image.fill(QColor(20, 45, 70));

    QPainter painter(&image);
    painter.setBrush(QColor(230, 110, 60));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRect(100, 65, 100, 90));
    painter.end();

    const QRect region(55, 30, 190, 160);
    const QImage mask = ObjectDetector::segment(image, region);

    QVERIFY(!mask.isNull());
    QCOMPARE(mask.size(), region.size());
    QVERIFY(mask.constScanLine(mask.height() / 2)[mask.width() / 2] > 200);
    QVERIFY(mask.constScanLine(0)[0] < 55);
}

void ObjectDetectorTest::rejectsInvalidSegmentationRegion()
{
    QImage image(100, 100, QImage::Format_RGB32);
    image.fill(Qt::black);

    QVERIFY(ObjectDetector::segment(image, QRect(0, 0, 10, 10)).isNull());
    QVERIFY(ObjectDetector::segment(QImage(), QRect(0, 0, 50, 50)).isNull());
}

QTEST_GUILESS_MAIN(ObjectDetectorTest)

#include "ObjectDetectorTest.moc"

#include "ObjectDetector.h"

#include <QPainter>
#include <QTest>
#include <algorithm>

class ObjectDetectorTest : public QObject
{
    Q_OBJECT

private slots:
    void detectsHighContrastObject();
    void segmentsCenteredObject();
    void rejectsInvalidSegmentationRegion();
};

void ObjectDetectorTest::detectsHighContrastObject()
{
    QImage image(320, 200, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.fillRect(QRect(80, 50, 150, 100), QColor(30, 60, 90));
    painter.end();

    const QVector<QRect> candidates = ObjectDetector::detect(image);
    QVERIFY(!candidates.isEmpty());

    const QPoint objectCenter(155, 100);
    QVERIFY(std::any_of(candidates.cbegin(), candidates.cend(), [objectCenter](const QRect &candidate) {
        return candidate.contains(objectCenter);
    }));
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

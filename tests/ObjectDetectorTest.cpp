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
    void reportsCandidateEvidence();
    void detectsLowContrastTiledWindows();
    void detectsExternalImageWhenRequested();
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

void ObjectDetectorTest::reportsCandidateEvidence()
{
    QImage image(320, 200, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(QRect(80, 50, 150, 100), QColor(30, 60, 90));
    painter.end();

    const QVector<ObjectDetector::Candidate> candidates = ObjectDetector::detectCandidates(image);
    QVERIFY(!candidates.isEmpty());
    QCOMPARE(candidates.last().rect, image.rect());
    QCOMPARE(candidates.last().kind, ObjectDetector::CandidateKind::Screen);

    const auto it = std::find_if(candidates.cbegin(), candidates.cend(), [](const auto &candidate) {
        return candidate.kind == ObjectDetector::CandidateKind::Window
            && qAbs(candidate.rect.left() - 80) <= 4
            && qAbs(candidate.rect.top() - 50) <= 4
            && qAbs(candidate.rect.right() - 229) <= 4
            && qAbs(candidate.rect.bottom() - 149) <= 4;
    });
    QVERIFY(it != candidates.cend());
    QVERIFY(it->confidence > 0.9f);
}

void ObjectDetectorTest::detectsLowContrastTiledWindows()
{
    // The dividers are intentionally too subtle for the regular UI-edge
    // threshold. They model a tiled desktop where a vivid window's contents
    // would otherwise hide the dark 1 px splitters between apps.
    QImage image(640, 400, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 320, 200), QColor(35, 38, 42));
    painter.fillRect(QRect(320, 0, 320, 200), QColor(41, 44, 48));
    painter.fillRect(QRect(0, 200, 320, 200), QColor(47, 50, 54));
    painter.fillRect(QRect(320, 200, 320, 200), QColor(53, 56, 60));
    painter.end();

    const QVector<ObjectDetector::Candidate> candidates = ObjectDetector::detectCandidates(image);
    const auto hasWindowNear = [&](const QRect &want) {
        return std::any_of(candidates.cbegin(), candidates.cend(), [&](const auto &candidate) {
            return candidate.kind == ObjectDetector::CandidateKind::Window
                && qAbs(candidate.rect.left() - want.left()) <= 7
                && qAbs(candidate.rect.top() - want.top()) <= 7
                && qAbs(candidate.rect.right() - want.right()) <= 7
                && qAbs(candidate.rect.bottom() - want.bottom()) <= 7;
        });
    };
    QVERIFY(hasWindowNear(QRect(0, 0, 320, 200)));
    QVERIFY(hasWindowNear(QRect(320, 200, 320, 200)));
}

void ObjectDetectorTest::detectsExternalImageWhenRequested()
{
    const QString path = qEnvironmentVariable("UNISIC_DETECTOR_TEST_IMAGE");
    if (path.isEmpty())
        QSKIP("Set UNISIC_DETECTOR_TEST_IMAGE to exercise a local screenshot");

    const QImage image(path);
    QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("Could not load %1").arg(path)));
    const QVector<ObjectDetector::Candidate> candidates = ObjectDetector::detectCandidates(image);
    const auto countKind = [&](ObjectDetector::CandidateKind kind) {
        return std::count_if(candidates.cbegin(), candidates.cend(),
                             [kind](const auto &candidate) { return candidate.kind == kind; });
    };
    qInfo() << "External image candidates:" << candidates.size()
            << "elements" << countKind(ObjectDetector::CandidateKind::Element)
            << "groups" << countKind(ObjectDetector::CandidateKind::Group)
            << "containers" << countKind(ObjectDetector::CandidateKind::Container)
            << "windows" << countKind(ObjectDetector::CandidateKind::Window);
    QVERIFY(!candidates.isEmpty());
    QCOMPARE(candidates.last().rect, image.rect());
    QCOMPARE(candidates.last().kind, ObjectDetector::CandidateKind::Screen);
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

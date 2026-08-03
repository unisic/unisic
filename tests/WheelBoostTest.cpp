// WheelBoost is the kit's wheel-scrolling component for a Flickable: a fixed
// pixel step per notch, eased in over settleMs instead of teleporting. The ease
// is motion, and this project's one load-bearing layout rule is that nothing
// moves under the pointer - a click that lands while a step is still settling
// used to have its target slide away between press and release, and a MouseArea
// only emits `clicked` when the release is still inside it. Small controls lost
// that race first (a 50x30 switch is gone after 15 px of glide), which is what
// "some switches cannot be toggled" was.
//
// The component is plain QtQuick (no kit imports), so the test loads the real
// file straight from the kit checkout - no module, no Theme, no app.
#include <QtTest>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QWheelEvent>

class WheelBoostTest : public QObject
{
    Q_OBJECT
private slots:
    void clickLandsWithNothingMoving();
    void pressFreezesTheGlideAndTheClickLands();
};

// A Flickable with one small click target, scrolled by the real WheelBoost.
static const char *kSceneTemplate = R"QML(
import QtQuick
import "%1" as Kit

Flickable {
    id: fl
    width: 200
    height: 200
    contentWidth: 200
    contentHeight: 2000
    boundsBehavior: Flickable.StopAtBounds

    Rectangle {
        objectName: "target"
        property int clicks: 0
        // Inside the 200 px viewport while the glide is mid-flight, and pushed
        // clean out of it by the time the full 220 px step has landed - which is
        // exactly the race a click has to survive.
        x: 20; y: 180
        width: 50; height: 30      // a USwitch, to the pixel
        color: "red"
        MouseArea { anchors.fill: parent; onClicked: parent.clicks++ }
    }

    Kit.WheelBoost { flickable: fl }
}
)QML";

// The scene is written to disk per test: a QQuickView owns its root, and one
// scene per test keeps a failed case from poisoning the next.
static bool writeScene(const QTemporaryDir &dir, QString *path)
{
    *path = dir.filePath(QStringLiteral("Scene.qml"));
    QFile f(*path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(QString::fromLatin1(kSceneTemplate)
                       .arg(QUrl::fromLocalFile(QStringLiteral(UNISIC_KIT_QML_DIR)).toString())
                       .toUtf8())
        > 0;
}

// Control case: with nothing moving, a click on the target must land. If this
// fails, WheelBoost's press-catcher is eating clicks outright and the freeze
// test below would be measuring the wrong thing.
void WheelBoostTest::clickLandsWithNothingMoving()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString scenePath;
    QVERIFY(writeScene(dir, &scenePath));

    QQuickView view;
    view.setSource(QUrl::fromLocalFile(scenePath));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.resize(200, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *target = view.rootObject()->findChild<QQuickItem *>(QStringLiteral("target"));
    QVERIFY(target);
    const QPointF at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    QCOMPARE(target->property("clicks").toInt(), 1);
}

void WheelBoostTest::pressFreezesTheGlideAndTheClickLands()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString scenePath;
    QVERIFY(writeScene(dir, &scenePath));

    QQuickView view;
    view.setSource(QUrl::fromLocalFile(scenePath));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.resize(200, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *flick = view.rootObject();
    QVERIFY(flick);
    auto *target = flick->findChild<QQuickItem *>(QStringLiteral("target"));
    QVERIFY(target);

    // One notch down, away from the target: the glide starts.
    QWheelEvent wheel(QPointF(150, 20), view.mapToGlobal(QPoint(150, 20)), QPoint(0, 0),
                      QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QVERIFY(QGuiApplication::sendEvent(&view, &wheel));
    QTest::qWait(24); // two glide ticks: moving, nowhere near settled
    const qreal movedBy = flick->property("contentY").toReal();
    QVERIFY2(movedBy > 1.0 && movedBy < 219.0,
             qPrintable(QStringLiteral("the glide must be mid-flight for this test to mean "
                                       "anything, contentY=%1").arg(movedBy)));

    // Click the target where it is RIGHT NOW, the way a hand would.
    const QPointF at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    QVERIFY2(QRectF(0, 0, view.width(), view.height()).contains(at),
             qPrintable(QStringLiteral("the target must be on screen to be clicked, at=%1,%2")
                            .arg(at.x()).arg(at.y())));
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    const qreal atPress = flick->property("contentY").toReal();
    QTest::qWait(60); // longer than the rest of the ease
    QCOMPARE(flick->property("contentY").toReal(), atPress); // frozen by the press
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, at.toPoint());

    QCOMPARE(target->property("clicks").toInt(), 1);
}

QTEST_MAIN(WheelBoostTest)
#include "WheelBoostTest.moc"

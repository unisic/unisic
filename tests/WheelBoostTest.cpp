// WheelBoost is the kit's one wheel-scrolling rule for a Flickable, matched to
// Chromium: 120 px per notch, no multiplier for a fast spin, an ease-in-out
// glide of 200 ms for one notch that a new notch retargets, and touchpad
// pixels passed through 1:1. These tests drive the real file with synthetic
// wheel events and check those properties as behavior: where the view ends up,
// how long it takes to get there, and that it never runs backwards.
//
// The glide is motion, and this project's one load-bearing layout rule is that
// nothing moves under the pointer - a click that lands while a step is still
// settling used to have its target slide away between press and release, and
// a MouseArea only emits `clicked` when the release is still inside it. Small
// controls lost that race first (a 50x30 switch is gone after 15 px of glide),
// which is what "some switches cannot be toggled" was.
//
// Nothing here waits a fixed time for something to START: under a loaded
// parallel ctest the first glide tick can come late, so every "has it moved
// yet" is polled with a generous bound. The glide is time-based, so a late
// tick only ever makes it look further along, never shorter.
//
// The component is plain QtQuick (no kit imports), so the test loads the real
// file straight from the kit checkout - no module, no Theme, no app.
#include <QtTest>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QWheelEvent>

class WheelBoostTest : public QObject
{
    Q_OBJECT
private slots:
    void clickLandsWithNothingMoving();
    void oneNotchIsChromiumsStep();
    void fastSpinAddsNoBoost();
    void notchDuringGlideRetargetsSmoothly();
    void touchpadIsOneToOne();
    void contentThatFitsPassesTheWheelOn();
    void gridViewStepsTheSame();
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
        // Inside the 200 px viewport until the glide has moved 90 px, and
        // pushed clean out of it by the time the full 120 px step has landed -
        // which is exactly the race a click has to survive.
        x: 20; y: 60
        width: 50; height: 30      // a USwitch, to the pixel
        color: "red"
        MouseArea { anchors.fill: parent; onClicked: parent.clicks++ }
    }

    Kit.WheelBoost { flickable: fl }
}
)QML";

// An outer page with an inner box whose content fits: the wheel over the box
// must still scroll the page.
static const char *kNestedTemplate = R"QML(
import QtQuick
import "%1" as Kit

Flickable {
    id: outer
    width: 200
    height: 200
    contentWidth: 200
    contentHeight: 2000
    boundsBehavior: Flickable.StopAtBounds

    Flickable {
        id: inner
        objectName: "inner"
        x: 0; y: 0
        width: 200; height: 150
        contentWidth: 200
        contentHeight: 100
        boundsBehavior: Flickable.StopAtBounds
        Kit.WheelBoost { flickable: inner }
    }

    Kit.WheelBoost { flickable: outer }
}
)QML";

// The History page's shape: a GridView of tiles, each with its own MouseArea.
static const char *kGridTemplate = R"QML(
import QtQuick
import "%1" as Kit

GridView {
    id: grid
    width: 200
    height: 200
    cellWidth: 100
    cellHeight: 90
    model: 200
    boundsBehavior: Flickable.StopAtBounds
    delegate: Rectangle {
        width: 90; height: 80
        color: "steelblue"
        MouseArea { anchors.fill: parent; drag.target: parent; hoverEnabled: true }
    }
    Kit.WheelBoost { flickable: grid }
}
)QML";

// The scene is written to disk per test: a QQuickView owns its root, and one
// scene per test keeps a failed case from poisoning the next.
static bool writeScene(const QTemporaryDir &dir, const char *tmpl, QString *path)
{
    *path = dir.filePath(QStringLiteral("Scene.qml"));
    QFile f(*path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(QString::fromLatin1(tmpl)
                       .arg(QUrl::fromLocalFile(QStringLiteral(UNISIC_KIT_QML_DIR)).toString())
                       .toUtf8())
        > 0;
}

#define LOAD_SCENE(tmpl)                                                                 \
    QTemporaryDir dir;                                                                   \
    QVERIFY(dir.isValid());                                                              \
    QString scenePath;                                                                   \
    QVERIFY(writeScene(dir, tmpl, &scenePath));                                          \
    QQuickView view;                                                                     \
    view.setSource(QUrl::fromLocalFile(scenePath));                                      \
    QCOMPARE(view.status(), QQuickView::Ready);                                          \
    view.resize(200, 200);                                                               \
    view.show();                                                                         \
    QVERIFY(QTest::qWaitForWindowExposed(&view));                                        \
    auto *flick = view.rootObject();                                                     \
    QVERIFY(flick)

static qreal contentY(QObject *flick)
{
    return flick->property("contentY").toReal();
}

// One detent of a plain wheel (angleDelta 120, no pixelDelta), downwards.
static void notch(QQuickView &view, QPoint at = QPoint(150, 20))
{
    QWheelEvent wheel(at, view.mapToGlobal(at), QPoint(0, 0), QPoint(0, -120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(&view, &wheel);
}

// Generous: a loaded machine delays ticks, it never makes the glide longer
// than its own clock says, so this bound only catches a glide that never ends.
static constexpr int kSettleTimeoutMs = 5000;

// Control case: with nothing moving, a click on the target must land. If this
// fails, WheelBoost's press-catcher is eating clicks outright and the freeze
// test below would be measuring the wrong thing.
void WheelBoostTest::clickLandsWithNothingMoving()
{
    LOAD_SCENE(kSceneTemplate);
    auto *target = flick->findChild<QQuickItem *>(QStringLiteral("target"));
    QVERIFY(target);
    const QPointF at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    QCOMPARE(target->property("clicks").toInt(), 1);
}

// Chromium on Linux: 120 px per notch, eased in over 200 ms (kInverseDelta's
// 12 frames at 60 Hz for a 120 px segment), never a teleport.
void WheelBoostTest::oneNotchIsChromiumsStep()
{
    LOAD_SCENE(kSceneTemplate);
    QElapsedTimer clock;
    clock.start();
    notch(view);
    QVERIFY2(contentY(flick) < 120.0, "a notch must glide, not jump");
    QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 120.0, kSettleTimeoutMs);
    // 10 ms of slack for the glide's millisecond clock.
    QVERIFY2(clock.elapsed() >= 190,
             qPrintable(QStringLiteral("landed after %1 ms, Chromium takes 200").arg(clock.elapsed())));
    QTest::qWait(250);
    QCOMPARE(contentY(flick), 120.0); // no overshoot, no drift after landing
}

// No multiplier: five notches are five steps, whether they come in one burst
// or spread out like a hand spinning the wheel.
void WheelBoostTest::fastSpinAddsNoBoost()
{
    LOAD_SCENE(kSceneTemplate);
    for (int i = 0; i < 5; ++i)
        notch(view);
    QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 600.0, kSettleTimeoutMs);
    for (int i = 0; i < 5; ++i) {
        notch(view);
        QTest::qWait(40);
    }
    QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 1200.0, kSettleTimeoutMs);
    QTest::qWait(250);
    QCOMPARE(contentY(flick), 1200.0);
}

// A notch that arrives mid-glide adds its 120 px to the target and the view
// keeps moving forward to it: never backwards, never past it.
void WheelBoostTest::notchDuringGlideRetargetsSmoothly()
{
    LOAD_SCENE(kSceneTemplate);
    notch(view);
    QTRY_VERIFY_WITH_TIMEOUT(contentY(flick) > 0.0, kSettleTimeoutMs);
    notch(view);
    qreal last = contentY(flick);
    QElapsedTimer clock;
    clock.start();
    while (contentY(flick) != 240.0 && clock.elapsed() < kSettleTimeoutMs) {
        QTest::qWait(2);
        const qreal now = contentY(flick);
        QVERIFY2(now >= last, qPrintable(QStringLiteral("ran backwards %1 -> %2").arg(last).arg(now)));
        QVERIFY2(now <= 240.0, qPrintable(QStringLiteral("overshot to %1").arg(now)));
        last = now;
    }
    QCOMPARE(contentY(flick), 240.0);
}

// A touchpad's pixelDelta is applied as is, at once, with no glide.
void WheelBoostTest::touchpadIsOneToOne()
{
    LOAD_SCENE(kSceneTemplate);
    const QPoint at(150, 20);
    QWheelEvent swipe(at, view.mapToGlobal(at), QPoint(0, -37), QPoint(0, -37 * 8), Qt::NoButton,
                      Qt::NoModifier, Qt::ScrollUpdate, false);
    QGuiApplication::sendEvent(&view, &swipe);
    QCOMPARE(contentY(flick), 37.0);
    QTest::qWait(100);
    QCOMPARE(contentY(flick), 37.0);
}

// A Flickable whose content fits has nothing to scroll: the wheel goes on to
// what is behind it (an outer page, a menu's click-away catcher).
void WheelBoostTest::contentThatFitsPassesTheWheelOn()
{
    LOAD_SCENE(kNestedTemplate);
    auto *inner = flick->findChild<QQuickItem *>(QStringLiteral("inner"));
    QVERIFY(inner);
    notch(view, QPoint(100, 50)); // over the inner box
    QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 120.0, kSettleTimeoutMs);
    QCOMPARE(contentY(inner), 0.0);
}

// An item view steps exactly like a plain Flickable: the History grid has no
// speed of its own.
void WheelBoostTest::gridViewStepsTheSame()
{
    LOAD_SCENE(kGridTemplate);
    notch(view, QPoint(50, 50)); // over a tile
    QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 120.0, kSettleTimeoutMs);
    notch(view, QPoint(50, 50));
    QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 240.0, kSettleTimeoutMs);
}

void WheelBoostTest::pressFreezesTheGlideAndTheClickLands()
{
    LOAD_SCENE(kSceneTemplate);
    auto *target = flick->findChild<QQuickItem *>(QStringLiteral("target"));
    QVERIFY(target);

    // Catch the glide mid-flight with the target still on screen: moved, but
    // less than 90 px. A loaded machine can deliver the first tick too late for
    // that, so a miss lets the step land, rewinds and tries again; the test is
    // about what a press does, not about how fast the scheduler is.
    qreal movedBy = 0;
    bool caught = false;
    for (int attempt = 0; attempt < 20 && !caught; ++attempt) {
        flick->setProperty("contentY", 0.0);
        notch(view);
        QElapsedTimer clock;
        clock.start();
        while (contentY(flick) <= 0.0 && clock.elapsed() < kSettleTimeoutMs)
            QTest::qWait(1);
        movedBy = contentY(flick);
        caught = movedBy > 0.0 && movedBy < 90.0;
        if (!caught)
            QTRY_COMPARE_WITH_TIMEOUT(contentY(flick), 120.0, kSettleTimeoutMs);
    }
    QVERIFY2(caught, qPrintable(QStringLiteral("never caught the glide mid-flight, last contentY=%1")
                                    .arg(movedBy)));

    // Click the target where it is RIGHT NOW, the way a hand would.
    const QPointF at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    QVERIFY2(QRectF(0, 0, view.width(), view.height()).contains(at),
             qPrintable(QStringLiteral("the target must be on screen to be clicked, at=%1,%2")
                            .arg(at.x()).arg(at.y())));
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    const qreal atPress = contentY(flick);
    QTest::qWait(300); // longer than a whole glide
    QCOMPARE(contentY(flick), atPress); // frozen by the press
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, at.toPoint());

    QCOMPARE(target->property("clicks").toInt(), 1);
}

QTEST_MAIN(WheelBoostTest)
#include "WheelBoostTest.moc"

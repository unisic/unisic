#include "AnnotationCanvas.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QTest>

class TestAnnotationCanvas final : public AnnotationCanvas
{
public:
    using AnnotationCanvas::mouseMoveEvent;
    using AnnotationCanvas::mousePressEvent;
    using AnnotationCanvas::mouseReleaseEvent;
    using AnnotationCanvas::hoverMoveEvent;
};

class AnnotationCanvasTest : public QObject
{
    Q_OBJECT

private slots:
    void penStrokeKeepsReleaseEndpoint();
    void penTapRendersDot();
    void deselectRestoresStrokeStyle();
    void smartPickPrefersTiledWindow();
    void editShapesSelectMoveDelete();
    void drawToolClickSelectsAndMoves();
};

// Convenience: full press→(move)→release cycle at item coordinates.
static void click(TestAnnotationCanvas &c, const QPointF &at)
{
    QMouseEvent p(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    c.mousePressEvent(&p);
    QMouseEvent r(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    c.mouseReleaseEvent(&r);
}
static void drag(TestAnnotationCanvas &c, const QPointF &from, const QPointF &to)
{
    QMouseEvent p(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    c.mousePressEvent(&p);
    QMouseEvent m(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    c.mouseMoveEvent(&m);
    QMouseEvent r(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    c.mouseReleaseEvent(&r);
}

void AnnotationCanvasTest::penStrokeKeepsReleaseEndpoint()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(100);
    canvas.setHeight(100);

    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);
    canvas.setTool(AnnotationCanvas::Pen);
    canvas.setStrokeColor(Qt::black);
    canvas.setStrokeWidth(4);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 50), QPointF(10, 50),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mousePressEvent(&press);

    QMouseEvent nearDuplicate(QEvent::MouseMove, QPointF(10.2, 50), QPointF(10.2, 50),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mouseMoveEvent(&nearDuplicate);

    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(90, 50), QPointF(90, 50),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    canvas.mouseReleaseEvent(&release);

    const QColor endpoint = canvas.rendered().pixelColor(85, 50);
    QVERIFY2(endpoint.lightness() < 80, "the rendered pen path must include its release endpoint");
}

// A pen TAP (press+release, no drag) is a deliberate dot. A 1-point path has
// no segments, so it must be rendered as a filled dot — not an invisible
// annotation that only a puzzling Ctrl+Z removes.
void AnnotationCanvasTest::penTapRendersDot()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(100);
    canvas.setHeight(100);
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);
    canvas.setTool(AnnotationCanvas::Pen);
    canvas.setStrokeColor(Qt::black);
    canvas.setStrokeWidth(12);

    click(canvas, QPointF(50, 50)); // tap in place, no movement
    QCOMPARE(canvas.annotCount(), 1);
    QVERIFY2(canvas.rendered().pixelColor(50, 50).lightness() < 80,
             "a pen tap must render a visible dot at the tap point");
}

// Selecting a shape seeds the props bar from that shape's style; deselecting via
// ANY path (click-empty, delete, …) must hand the user their own pre-selection
// style back, or the clicked shape's colour leaks into the next drawn shape.
void AnnotationCanvasTest::deselectRestoresStrokeStyle()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(200);
    canvas.setHeight(200);
    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);

    canvas.setTool(AnnotationCanvas::Rect);
    canvas.setShapeFillEnabled(true);
    canvas.setStrokeColor(Qt::red);
    drag(canvas, QPointF(50, 50), QPointF(150, 150)); // a red shape

    canvas.setStrokeColor(Qt::blue); // the user's own current pick
    click(canvas, QPointF(100, 100)); // select the red shape
    QVERIFY(canvas.hasAnnotSelection());
    QCOMPARE(canvas.strokeColor(), QColor(Qt::red)); // props bar shows the shape's colour

    click(canvas, QPointF(5, 5)); // deselect on empty space
    QVERIFY(!canvas.hasAnnotSelection());
    QCOMPARE(canvas.strokeColor(), QColor(Qt::blue)); // restored, not leaked

    // The delete path used to skip the restore entirely — verify it too.
    click(canvas, QPointF(100, 100));
    QCOMPARE(canvas.strokeColor(), QColor(Qt::red));
    canvas.removeSelectedAnnot();
    QCOMPARE(canvas.strokeColor(), QColor(Qt::blue));
}

void AnnotationCanvasTest::smartPickPrefersTiledWindow()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(640);
    canvas.setHeight(400);

    QImage image(640, 400, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 320, 200), QColor(35, 38, 42));
    painter.fillRect(QRect(320, 0, 320, 200), QColor(41, 44, 48));
    painter.fillRect(QRect(0, 200, 320, 200), QColor(47, 50, 54));
    painter.fillRect(QRect(320, 200, 320, 200), QColor(53, 56, 60));
    painter.end();

    canvas.setImage(image);
    canvas.setSelectionMode(true);
    canvas.setSmartPick(true);

    QHoverEvent hover(QEvent::HoverMove, QPointF(100, 100), QPointF(100, 100), QPointF(99, 99));
    canvas.hoverMoveEvent(&hover);

    QTRY_COMPARE(canvas.hoverObjectKind(), QStringLiteral("Window"));
    const QRectF rect = canvas.hoverObjectRect();
    QVERIFY(qAbs(rect.left()) <= 7 && qAbs(rect.top()) <= 7);
    QVERIFY(qAbs(rect.right() - 319) <= 7 && qAbs(rect.bottom() - 199) <= 7);
}

void AnnotationCanvasTest::editShapesSelectMoveDelete()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(200);
    canvas.setHeight(200);
    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);

    // Draw a filled rectangle (50,50)-(150,150) with the Rect tool.
    canvas.setTool(AnnotationCanvas::Rect);
    canvas.setShapeFillEnabled(true);
    QMouseEvent p1(QEvent::MouseButtonPress, QPointF(50, 50), QPointF(50, 50),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mousePressEvent(&p1);
    QMouseEvent m1(QEvent::MouseMove, QPointF(150, 150), QPointF(150, 150),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mouseMoveEvent(&m1);
    QMouseEvent r1(QEvent::MouseButtonRelease, QPointF(150, 150), QPointF(150, 150),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    canvas.mouseReleaseEvent(&r1);
    QCOMPARE(canvas.annotCount(), 1);

    // Edit tool: click inside selects it.
    canvas.setTool(AnnotationCanvas::EditShapes);
    canvas.selectAnnotAt(100, 100);
    QVERIFY(canvas.hasAnnotSelection());
    QCOMPARE(canvas.selectedAnnotTool(), int(AnnotationCanvas::Rect));

    // Drag from inside the shape moves it right by 20 px.
    QMouseEvent p2(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mousePressEvent(&p2);
    QMouseEvent m2(QEvent::MouseMove, QPointF(120, 100), QPointF(120, 100),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mouseMoveEvent(&m2);
    QMouseEvent r2(QEvent::MouseButtonRelease, QPointF(120, 100), QPointF(120, 100),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    canvas.mouseReleaseEvent(&r2);
    // The shape's left edge (was 50) should have moved toward 70; sample a
    // pixel that is now inside but was outside the original rect.
    QVERIFY2(canvas.rendered().pixelColor(165, 100).lightness() < 250,
             "the moved rectangle should cover pixels past its original right edge");

    // Delete removes it; undo brings it back.
    canvas.removeSelectedAnnot();
    QCOMPARE(canvas.annotCount(), 0);
    canvas.undo();
    QCOMPARE(canvas.annotCount(), 1);
}

// Direct manipulation WITHOUT the Edit tool: with the Rect tool still active,
// a plain click on a placed shape selects it, dragging its body moves it,
// dragging empty space draws a NEW shape, and a click on empty deselects.
void AnnotationCanvasTest::drawToolClickSelectsAndMoves()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(200);
    canvas.setHeight(200);
    QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);

    canvas.setTool(AnnotationCanvas::Rect);
    canvas.setShapeFillEnabled(true);
    drag(canvas, QPointF(50, 50), QPointF(150, 150));
    QCOMPARE(canvas.annotCount(), 1);

    // Still the Rect tool: click inside the shape → selected.
    click(canvas, QPointF(100, 100));
    QVERIFY2(canvas.hasAnnotSelection(), "click with the Rect tool must select the shape");
    QCOMPARE(canvas.selectedAnnotTool(), int(AnnotationCanvas::Rect));

    // Drag the selected body → moves (no new shape drawn).
    drag(canvas, QPointF(100, 100), QPointF(120, 100));
    QCOMPARE(canvas.annotCount(), 1);
    QVERIFY2(canvas.rendered().pixelColor(165, 100).lightness() < 250,
             "dragging the selected shape's body must move it, not draw a new one");

    // Drag on empty space → draws a second shape.
    drag(canvas, QPointF(10, 160), QPointF(40, 190));
    QCOMPARE(canvas.annotCount(), 2);

    // Click empty space → deselects.
    click(canvas, QPointF(5, 5));
    QVERIFY(!canvas.hasAnnotSelection());

    // UNFILLED rect: clicking its INTERIOR (not the outline) must select too —
    // that is where users click, only the border actually paints.
    canvas.undo(); // drop shape 2
    canvas.undo(); // undo the body move
    canvas.undo(); // drop shape 1
    QCOMPARE(canvas.annotCount(), 0);
    canvas.setShapeFillEnabled(false);
    drag(canvas, QPointF(50, 50), QPointF(150, 150));
    QCOMPARE(canvas.annotCount(), 1);
    click(canvas, QPointF(100, 100)); // dead center, far from the outline
    QVERIFY2(canvas.hasAnnotSelection(),
             "clicking the interior of an unfilled rect must select it");
    drag(canvas, QPointF(100, 100), QPointF(130, 100));
    QCOMPARE(canvas.annotCount(), 1); // moved, not drawn over
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    AnnotationCanvasTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "AnnotationCanvasTest.moc"

#include "AnnotationCanvas.h"
#include "ImageEffects.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
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
    void editShapesSelectMoveDelete();
    void drawToolClickSelectsAndMoves();
    void captureOnReleaseConfirms();
    void ocrSelectionCreatesLineBatchedAnnotations();
    void ocrCaretSelectionIsCharacterPrecise();
    void textHighlightPenSnapsToLineTextHeight();
    void pasteClipboardCreatesTextAndImage();
    void shiftSnapsLineAngleAndRectangleRatio();
    void watermarkStampsAndPreservesBaseDimensions();
    void calloutRendersTailAndCanBeSelected();
    void arrowAndMeasureRenderAndCanBeSelected();
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

void AnnotationCanvasTest::captureOnReleaseConfirms()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(100);
    canvas.setHeight(100);
    QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);
    canvas.setSelectionMode(true);
    canvas.setConfirmOnRelease(true);

    QSignalSpy confirmed(&canvas, &AnnotationCanvas::selectionConfirmed);

    // Releasing a selection drag confirms exactly once.
    drag(canvas, QPointF(10, 10), QPointF(80, 60));
    QVERIFY(canvas.hasSelection());
    QCOMPARE(confirmed.count(), 1);

    // Toggle off: a fresh drag (outside the old rect) selects but never confirms.
    canvas.setConfirmOnRelease(false);
    drag(canvas, QPointF(85, 80), QPointF(95, 95));
    QVERIFY(canvas.hasSelection());
    QCOMPARE(confirmed.count(), 1);
}

// OCR selection is letter-granular, but exported highlight/redaction must be
// one continuous bar per text line and undo as a single user action.
void AnnotationCanvasTest::ocrSelectionCreatesLineBatchedAnnotations()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(160);
    canvas.setHeight(100);
    QImage image(160, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    // Give each glyph box detail for blur/pixelate to alter; a flat white base
    // would correctly render a redaction as identical white pixels.
    {
        QPainter painter(&image);
        painter.fillRect(QRect(11, 14, 2, 12), Qt::black);
        painter.fillRect(QRect(23, 14, 2, 12), Qt::black);
        painter.fillRect(QRect(11, 50, 2, 12), Qt::black);
        painter.fillRect(QRect(23, 50, 2, 12), Qt::black);
    }
    canvas.setImage(image);
    const QVector<OcrWord> words{
        {QRect(10, 12, 10, 18), QStringLiteral("A"), 0, 100.f, false},
        {QRect(22, 12, 10, 18), QStringLiteral("B"), 0, 100.f, false},
        {QRect(10, 48, 10, 18), QStringLiteral("C"), 1, 100.f, false},
        {QRect(22, 48, 10, 18), QStringLiteral("D"), 1, 100.f, false},
    };
    // Each OCR action (highlight/redact) turns the transient selection into
    // permanent marks and LEAVES OCR mode, so re-enter + reselect before each
    // one — exactly what the UI does for a fresh selection.
    const auto reselect = [&] {
        canvas.setOcrMode(true);
        canvas.setOcrWords(words);
        canvas.ocrSelectAll();
    };

    reselect();
    QVERIFY(canvas.highlightOcrSelection());
    QCOMPARE(canvas.annotCount(), 2); // one bar for each OCR line, not glyph
    canvas.undo();
    QCOMPARE(canvas.annotCount(), 0); // the whole batch is one undo step
    canvas.redo();
    QCOMPARE(canvas.annotCount(), 2);

    reselect();
    QVERIFY(canvas.redactOcrSelection(false));
    QCOMPARE(canvas.annotCount(), 4);
    reselect();
    QVERIFY(canvas.redactOcrSelection(true));
    QCOMPARE(canvas.annotCount(), 6);
    QVERIFY(canvas.rendered() != image);
}

// Windows-Snipping-style text selection: a drag selects a CHARACTER-precise
// caret range constrained to the line under the cursor (not the whole line, not
// a nearest-glyph guess across the whole image); a bare click selects the whole
// line; a click away from text keeps the current pick.
void AnnotationCanvasTest::ocrCaretSelectionIsCharacterPrecise()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(160);
    canvas.setHeight(100);
    QImage image(160, 100, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);
    // Two lines, two glyphs each; B and D start a word (spaceBefore) so a copied
    // range keeps its spacing.
    const QVector<OcrWord> words{
        {QRect(10, 12, 10, 18), QStringLiteral("A"), 0, 100.f, false},
        {QRect(22, 12, 10, 18), QStringLiteral("B"), 0, 100.f, true},
        {QRect(10, 48, 10, 18), QStringLiteral("C"), 1, 100.f, false},
        {QRect(22, 48, 10, 18), QStringLiteral("D"), 1, 100.f, true},
    };
    canvas.setOcrMode(true);
    canvas.setOcrWords(words);

    // Drag ending between the two glyphs of line 0 selects ONLY the first glyph.
    drag(canvas, QPointF(6, 21), QPointF(21, 21));
    QVERIFY(canvas.hasOcrSelection());
    QCOMPARE(canvas.ocrSelectedText(), QStringLiteral("A"));

    // Dragging past the last glyph takes the whole line (with the word space).
    drag(canvas, QPointF(6, 21), QPointF(60, 21));
    QCOMPARE(canvas.ocrSelectedText(), QStringLiteral("A B"));

    // A cross-line drag keeps reading order and a newline between lines.
    drag(canvas, QPointF(6, 21), QPointF(60, 57));
    QCOMPARE(canvas.ocrSelectedText(), QStringLiteral("A B\nC D"));

    // A bare click selects the whole line under the cursor.
    click(canvas, QPointF(15, 21));
    QCOMPARE(canvas.ocrSelectedText(), QStringLiteral("A B"));

    // A click far from any text must NOT wipe a careful selection.
    click(canvas, QPointF(140, 92));
    QCOMPARE(canvas.ocrSelectedText(), QStringLiteral("A B"));
}

// The text-mode highlighter is a PEN: a freehand swipe over text snaps to the
// glyph lines it crosses, and each band takes that LINE's own text height (like
// a PDF highlighter) — never a fixed size or the stroke's bounding box.
void AnnotationCanvasTest::textHighlightPenSnapsToLineTextHeight()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(200);
    canvas.setHeight(160);
    QImage image(200, 160, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);

    // Two lines of DIFFERENT text heights: line 0 is 16px tall, line 1 is 28px.
    canvas.setGlyphBoxes({
        {QRect(20, 50, 20, 16), QStringLiteral("a"), 0, 100.f, false},
        {QRect(40, 50, 20, 16), QStringLiteral("b"), 0, 100.f, false},
        {QRect(60, 50, 20, 16), QStringLiteral("c"), 0, 100.f, false},
        {QRect(80, 50, 20, 16), QStringLiteral("d"), 0, 100.f, false},
        {QRect(20, 100, 20, 28), QStringLiteral("e"), 1, 100.f, false},
        {QRect(40, 100, 20, 28), QStringLiteral("f"), 1, 100.f, false},
        {QRect(60, 100, 20, 28), QStringLiteral("g"), 1, 100.f, false},
        {QRect(80, 100, 20, 28), QStringLiteral("h"), 1, 100.f, false},
    });
    canvas.setTool(AnnotationCanvas::Highlight);
    canvas.setHighlightMode(AnnotationCanvas::HlText);
    canvas.setStrokeColor(QColor(255, 234, 112)); // yellow marker
    canvas.setStrokeWidth(8);

    // Swipe the pen across line 0 only (through the glyphs' vertical middle).
    drag(canvas, QPointF(25, 58), QPointF(95, 58));
    QCOMPARE(canvas.annotCount(), 1); // ONE band for the swiped line, not per glyph

    const QImage r1 = canvas.rendered();
    // Multiply of yellow over white leaves a low blue channel; white stays 255.
    QVERIFY2(r1.pixelColor(60, 58).blue() < 200, "the swiped text line must be highlighted");
    // 6px above the 16px text (y=44 < 50): a text-sized band stops at the glyph
    // top, so this pixel must stay white. A fixed/oversized box would bleed here.
    QVERIFY2(r1.pixelColor(60, 44).blue() > 240, "band must not extend above the text height");
    QVERIFY2(r1.pixelColor(60, 112).blue() > 240, "an un-swiped line must stay untouched");

    // Swipe the taller line 1. Its band must reach the full 28px text height.
    drag(canvas, QPointF(25, 112), QPointF(95, 112));
    QCOMPARE(canvas.annotCount(), 2);
    const QImage r2 = canvas.rendered();
    QVERIFY2(r2.pixelColor(60, 120).blue() < 200, "the second swiped line must be highlighted");
    // y=125 is inside the 28px line (100..128) but would be BELOW a 16px band —
    // proves the band is sized to this line's own, taller, text.
    QVERIFY2(r2.pixelColor(60, 125).blue() < 200,
             "the band must hug the taller line's full text height");
}

void AnnotationCanvasTest::pasteClipboardCreatesTextAndImage()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(200);
    canvas.setHeight(120);
    QImage image(200, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    canvas.setImage(image);

    QGuiApplication::clipboard()->setText(QStringLiteral("paste"));
    QVERIFY(canvas.pasteClipboard(20, 20));
    QCOMPARE(canvas.annotCount(), 1);

    QImage stamp(24, 16, QImage::Format_ARGB32_Premultiplied);
    stamp.fill(Qt::black);
    QGuiApplication::clipboard()->setImage(stamp);
    QVERIFY(canvas.pasteClipboard(100, 60));
    QCOMPARE(canvas.annotCount(), 2);
    QVERIFY(canvas.rendered() != image);
}

void AnnotationCanvasTest::watermarkStampsAndPreservesBaseDimensions()
{
    QImage base(300, 180, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::black);
    const QImage stamped = UnisicImageEffects::watermarkText(
        base, QStringLiteral("Unisic"), 100, QStringLiteral("bottom-right"));
    QCOMPARE(stamped.size(), base.size());
    QVERIFY(stamped != base);
    QCOMPARE(UnisicImageEffects::watermarkText(base, {}, 80,
                                               QStringLiteral("bottom-right")), base);

    QImage logo(80, 40, QImage::Format_ARGB32_Premultiplied);
    logo.fill(Qt::transparent);
    {
        QPainter painter(&logo);
        painter.fillRect(QRect(5, 5, 70, 30), Qt::white);
    }
    const QImage logoStamped = UnisicImageEffects::watermarkImage(
        base, logo, 65, QStringLiteral("top-left"));
    QCOMPARE(logoStamped.size(), base.size());
    QVERIFY(logoStamped != base);
    QCOMPARE(UnisicImageEffects::watermarkImage(base, {}, 65,
                                                QStringLiteral("top-left")), base);
}

void AnnotationCanvasTest::calloutRendersTailAndCanBeSelected()
{
    TestAnnotationCanvas canvas;
    canvas.setWidth(220);
    canvas.setHeight(160);
    QImage base(220, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    canvas.setImage(base);
    canvas.setTool(AnnotationCanvas::Callout);
    canvas.setStrokeColor(Qt::black);
    canvas.setShapeFillColor(Qt::black);
    canvas.setShapeFillEnabled(true);
    drag(canvas, QPointF(30, 20), QPointF(180, 100));

    QCOMPARE(canvas.annotCount(), 1);
    const QImage out = canvas.rendered();
    QVERIFY(out != base);
    QVERIFY2(out.pixelColor(38, 110).lightness() < 80,
             "the callout tail must extend below its dragged rectangle");

    canvas.setTool(AnnotationCanvas::EditShapes);
    click(canvas, QPointF(38, 110));
    QVERIFY2(canvas.hasAnnotSelection(), "the callout tail must be selectable");
}

void AnnotationCanvasTest::arrowAndMeasureRenderAndCanBeSelected()
{
    QImage base(240, 160, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);

    TestAnnotationCanvas arrow;
    arrow.setWidth(240);
    arrow.setHeight(160);
    arrow.setImage(base);
    arrow.setTool(AnnotationCanvas::Arrow);
    arrow.setStrokeColor(Qt::black);
    arrow.setStrokeWidth(5);
    drag(arrow, QPointF(20, 30), QPointF(200, 30));
    arrow.setTool(AnnotationCanvas::EditShapes);
    arrow.selectAnnotAt(100, 30);
    const QImage filledHead = arrow.rendered();
    arrow.setArrowHeadStyle(1);
    QVERIFY2(arrow.rendered() != filledHead,
             "the selected arrow's head style must be editable");
    arrow.undo();
    QCOMPARE(arrow.rendered(), filledHead);

    TestAnnotationCanvas measure;
    measure.setWidth(240);
    measure.setHeight(160);
    measure.setImage(base);
    measure.setTool(AnnotationCanvas::Measure);
    measure.setStrokeColor(Qt::black);
    measure.setStrokeWidth(4);
    drag(measure, QPointF(20, 100), QPointF(200, 100));
    QCOMPARE(measure.annotCount(), 1);
    QVERIFY(measure.rendered() != base);
    measure.setTool(AnnotationCanvas::EditShapes);
    click(measure, QPointF(60, 100));
    QVERIFY2(measure.hasAnnotSelection(), "the measure shaft must be selectable");
}

void AnnotationCanvasTest::shiftSnapsLineAngleAndRectangleRatio()
{
    TestAnnotationCanvas line;
    line.setWidth(100);
    line.setHeight(100);
    QImage base(100, 100, QImage::Format_ARGB32_Premultiplied);
    base.fill(Qt::white);
    line.setImage(base);
    line.setTool(AnnotationCanvas::Line);
    line.setStrokeColor(Qt::black);
    line.setStrokeWidth(4);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(50, 25), QPointF(50, 25),
                     Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(50, 25), QPointF(50, 25),
                        Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    line.mousePressEvent(&press);
    line.mouseMoveEvent(&move);
    line.mouseReleaseEvent(&release);
    QVERIFY2(line.rendered().pixelColor(44, 10).lightness() < 80,
             "Shift must constrain a line to the nearest 45-degree angle");

    TestAnnotationCanvas rect;
    rect.setWidth(100);
    rect.setHeight(100);
    rect.setImage(base);
    rect.setTool(AnnotationCanvas::Rect);
    rect.setStrokeColor(Qt::black);
    rect.setStrokeWidth(4);
    rect.mousePressEvent(&press);
    rect.mouseMoveEvent(&move);
    rect.mouseReleaseEvent(&release);
    QVERIFY2(rect.rendered().pixelColor(48, 48).lightness() < 80,
             "Shift must constrain a rectangle to a square ratio");
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    AnnotationCanvasTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "AnnotationCanvasTest.moc"

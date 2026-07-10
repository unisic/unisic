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
    void smartPickPrefersTiledWindow();
};

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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    AnnotationCanvasTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "AnnotationCanvasTest.moc"

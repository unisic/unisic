#include "AnnotationCanvas.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QTest>

class TestAnnotationCanvas final : public AnnotationCanvas
{
public:
    using AnnotationCanvas::mouseMoveEvent;
    using AnnotationCanvas::mousePressEvent;
    using AnnotationCanvas::mouseReleaseEvent;
};

class AnnotationCanvasTest : public QObject
{
    Q_OBJECT

private slots:
    void penStrokeKeepsReleaseEndpoint();
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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    AnnotationCanvasTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "AnnotationCanvasTest.moc"

#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <functional>

// Optional Tesseract-backed OCR. This whole translation unit is only compiled
// when HAVE_TESSERACT is defined (see CMakeLists). Recognition runs on a worker
// thread; the callback is delivered on the GUI thread.
class OcrEngine : public QObject
{
    Q_OBJECT
public:
    using Result = std::function<void(const QString &text, const QString &error)>;
    explicit OcrEngine(QObject *parent = nullptr);

    // langs is a Tesseract language spec, e.g. "pol+eng".
    void recognize(const QImage &img, const QString &langs, Result cb);
};

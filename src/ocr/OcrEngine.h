#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

// Optional Tesseract-backed OCR. This whole translation unit is only compiled
// when HAVE_TESSERACT is defined (see CMakeLists). Recognition runs on a worker
// thread; the callback is delivered on the GUI thread.
class OcrEngine : public QObject
{
    Q_OBJECT
public:
    using Result = std::function<void(const QString &text, const QString &error)>;
    explicit OcrEngine(QObject *parent = nullptr);
    ~OcrEngine() override;

    // langs is a Tesseract language spec, e.g. "pol+eng".
    void recognize(const QImage &img, const QString &langs, Result cb);

private:
    // Flipped by the destructor so an in-flight worker aborts at its next
    // checkpoint: QtConcurrent tasks can't be cancelled from outside, and the
    // global pool's destructor waits for them — without this, quitting mid-
    // recognition keeps the process alive until tesseract finishes.
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);
};

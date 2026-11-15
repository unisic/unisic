#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include "OcrWord.h"
#include "PaddleOcr.h"

// OCR front end. Tesseract (+ leptonica) is a hard build requirement (see
// CMakeLists), so it is always compiled in; whether any language data is
// INSTALLED is a separate runtime question. On Windows the OS recognizer
// (WindowsOcr) can run instead, on Linux the better OCR model (PaddleOcr),
// see setEngine(). Recognition runs on a worker
// thread; the callback is delivered on the GUI thread.
class OcrEngine : public QObject
{
    Q_OBJECT
public:
    using Result = std::function<void(const QString &text, const QString &error)>;
    // Word-boxes result for the editor's selectable-text overlay.
    using BoxResult = std::function<void(const QVector<OcrWord> &words, const QString &error)>;
    explicit OcrEngine(QObject *parent = nullptr);
    ~OcrEngine() override;

    // The "+"-joined spec of every installed Tesseract langpack (osd/equ
    // dropped, English weighted first), or "" if none are found. Used when the
    // OCR language is set to auto-detect. Scans the same tessdata dirs as the
    // Init fallback; cheap (a directory listing), safe to call on the GUI thread.
    static QString detectedLanguages();

    // Whether osd.traineddata is installed - the model the auto-language script
    // detection needs. Without it, auto falls back to loading every pack.
    static bool scriptDetectionAvailable();

    // Given an OSD script name ("Arabic", "Han", "Latin", …) and the "+"-joined
    // set of installed langpacks, return the langs to OCR a page of that script
    // with, or "" to keep the full set. Latin/Cyrillic/Greek always return ""
    // (OSD confuses them, and narrowing could drop the user's real language).
    // Public + static so the dev/smoke harness can pin the mapping without the
    // traineddata that live OSD needs.
    static QString languagesForScript(const QString &script, const QString &availableSpec);

    // langs is a Tesseract language spec, e.g. "pol+eng". When `autoScript` is
    // true, `langs` is treated as the full "load everything installed" fallback:
    // the engine first runs OSD on the image to detect its script and narrows to
    // that script's installed langpacks (much more accurate than loading every
    // pack at once), keeping `langs` only when OSD is unavailable or unsure.
    void recognize(const QImage &img, const QString &langs, bool autoScript, Result cb);
    // Same recognition, but returns per-word bounding boxes (image pixels)
    // instead of one blob. Skips the QR/barcode short-circuit (a payload has
    // no word geometry). Mirrors recognize()'s threading + cancellation.
    void recognizeBoxes(const QImage &img, const QString &langs, bool autoScript, BoxResult cb);

    // An OcrEngines id ("tesseract", "windows", "paddle"). Applies to the next
    // recognize()/recognizeBoxes(); an id this build cannot run means Tesseract.
    // `langs`/`autoScript` only steer Tesseract: Windows OCR reads the user's
    // profile languages, and when none of them has OCR installed that run falls
    // back to Tesseract with them.
    void setEngine(const QString &id) { m_engine = id; }
    // Where a "paddle" run gets the loaded runtime and the model files from
    // (BackgroundRemover::ocrSetup()). Asked on the GUI thread, once per run.
    void setPaddleSetup(std::function<PaddleOcr::Setup()> f) { m_paddleSetup = std::move(f); }

private:
    PaddleOcr::Setup paddleSetup(const QString &engine) const
    {
        return engine == QLatin1String("paddle") && m_paddleSetup ? m_paddleSetup() : PaddleOcr::Setup{};
    }

    QString m_engine = QStringLiteral("tesseract");
    std::function<PaddleOcr::Setup()> m_paddleSetup;
    // Flipped by the destructor so an in-flight worker aborts at its next
    // checkpoint: QtConcurrent tasks can't be cancelled from outside, and the
    // global pool's destructor waits for them - without this, quitting mid-
    // recognition keeps the process alive until tesseract finishes.
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);
};

#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include "OcrWord.h"

// Optional Tesseract-backed OCR. This whole translation unit is only compiled
// when HAVE_TESSERACT is defined (see CMakeLists). Recognition runs on a worker
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

    // Whether osd.traineddata is installed — the model the auto-language script
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

private:
    // Flipped by the destructor so an in-flight worker aborts at its next
    // checkpoint: QtConcurrent tasks can't be cancelled from outside, and the
    // global pool's destructor waits for them — without this, quitting mid-
    // recognition keeps the process alive until tesseract finishes.
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);
};

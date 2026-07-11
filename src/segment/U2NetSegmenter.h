#pragma once
#include <QObject>
#include <QImage>
#include <QRect>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

// Optional U-2-Net (u2netp) saliency background removal via onnxruntime. The
// whole translation unit is compiled only when HAVE_ONNX is defined (see
// CMakeLists). Produces the SAME mask contract as ObjectDetector::segment():
// an 8-bit grayscale image the size of `region`, 255 = keep (foreground),
// 0 = remove — so it drops straight into the existing cutout compositing.
//
// The heavy model file (4.5–170 MB) is not bundled; it is downloaded on first
// use into the user data dir. Integrity is guarded by a size floor plus an
// atomic temp-file install (a truncated download is rejected and never swapped
// in), and a catalog model that ORT refuses to load is deleted so the next run
// re-downloads it — there is no cryptographic hash pin today.
class U2NetSegmenter : public QObject
{
    Q_OBJECT
public:
    using Result = std::function<void(const QImage &mask, const QString &error)>;
    explicit U2NetSegmenter(QObject *parent = nullptr);
    ~U2NetSegmenter() override;

    // Downloadable model catalog (u2net family + IS-Net; see kModels in the
    // .cpp for URLs/sizes). "custom" is a user-provided .onnx file.
    static QStringList modelIds();
    // Human size hint for a catalog id ("4.5 MB"); empty for custom/unknown.
    static QString modelSizeText(const QString &id);
    // Where a catalog model is stored on disk.
    static QString pathFor(const QString &id);

    // Select the active model (a catalog id, or "custom" + the file path).
    // Thread-safe; a change drops the loaded session so the next run reloads.
    void setModel(const QString &id, const QString &customPath);
    QString activeModelId() const;
    // The active model's on-disk path, and whether it is present + plausibly
    // sized.
    QString modelPath() const;
    bool modelReady() const;

    // Synchronous segmentation — SAFE to call from a worker thread (guarded by
    // an internal mutex; lazily creates the ORT session). Returns a null image
    // on any failure (missing model, load/inference error), which callers treat
    // as "fall back to the heuristic segmenter".
    QImage segmentSync(const QImage &img, const QRect &region);

    // Async convenience for the editor's Remove-background action: runs
    // segmentSync off-thread, delivers the mask on the GUI thread.
    void segment(const QImage &img, const QRect &region, Result cb);

    // A self-contained segmentation callable bound to this segmenter's shared
    // state. SAFE to keep and invoke from any thread even after the
    // U2NetSegmenter is destroyed: it holds a shared_ptr to the state, so a
    // destroyed segmenter simply makes it return a null image (the cancelled
    // flag is set in the destructor). Used to hand the canvas an external
    // segmenter without capturing a raw `this`.
    std::function<QImage(const QImage &img, const QRect &region)> makeWorker() const;

    // Download the ACTIVE catalog model into modelPath()
    // (QNetworkAccessManager). Errors out for "custom" — that file is the
    // user's. `done(ok, message)` fires on the GUI thread.
    void downloadModel(std::function<void(bool ok, const QString &message)> done);

private:
    // All state a worker thread touches (model selection, the ORT session, the
    // mutexes guarding them, and the cancelled flag) lives in one State object
    // held through a shared_ptr. Every worker lambda captures a shared_ptr copy,
    // so the state — and therefore both mutexes and the Ort::Session — outlives
    // the U2NetSegmenter object whenever a queued or in-flight task can still
    // reach it. The destructor merely flips State::cancelled; it never tears
    // down anything a pool thread might still be using (no more raw-`this`
    // capture, no use-after-free window on shutdown). Opaque here to keep the
    // ORT headers out of this file; defined in the .cpp.
    struct State;
    std::shared_ptr<State> m_state;
};

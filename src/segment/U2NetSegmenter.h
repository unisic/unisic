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
// The heavy model file (~4.5 MB) is not bundled; it is downloaded on first use
// into the user data dir and verified against a pinned SHA-256.
class U2NetSegmenter : public QObject
{
    Q_OBJECT
public:
    using Result = std::function<void(const QImage &mask, const QString &error)>;
    explicit U2NetSegmenter(QObject *parent = nullptr);
    ~U2NetSegmenter() override;

    // Where the model file lives, and whether it is present + plausibly sized.
    static QString modelPath();
    bool modelReady() const;

    // Synchronous segmentation — SAFE to call from a worker thread (guarded by
    // an internal mutex; lazily creates the ORT session). Returns a null image
    // on any failure (missing model, load/inference error), which callers treat
    // as "fall back to the heuristic segmenter".
    QImage segmentSync(const QImage &img, const QRect &region);

    // Async convenience for the editor's Remove-background action: runs
    // segmentSync off-thread, delivers the mask on the GUI thread.
    void segment(const QImage &img, const QRect &region, Result cb);

    // Download the model into modelPath() (QNetworkAccessManager), verifying the
    // pinned SHA-256. `done(ok, message)` fires on the GUI thread.
    void downloadModel(std::function<void(bool ok, const QString &message)> done);

private:
    // Set by the destructor before it blocks on m_mutex; a worker that acquires
    // the lock after this bails instead of touching torn-down state.
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);

    // ORT objects are created lazily and reused. Opaque here to keep the ORT
    // headers out of this file; defined in the .cpp.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::mutex m_mutex;
};

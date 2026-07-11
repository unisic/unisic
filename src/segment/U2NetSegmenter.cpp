#include "U2NetSegmenter.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QPointer>

// Header location differs by distro/packaging: some put it directly on the
// include path, Fedora ships it under an onnxruntime/ subdir.
#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#else
#include <onnxruntime/onnxruntime_cxx_api.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
// Downloadable saliency models, all from the pinned rembg v0.0.0 release
// (fixed URLs over https; a truncated download is rejected by minBytes and
// the atomic install). All share U-2-Net's contract: NCHW float input, a
// saliency map output that is min-max normalized to an alpha mask.
//   inputSize     — the model's expected square input (0 = ask the session);
//   imagenetNorm  — true: ImageNet mean/std (u2net family),
//                   false: mean 0.5 / std 1.0 (IS-Net preprocessing).
struct ModelSpec {
    const char *id;
    const char *url;
    qint64 minBytes;
    int inputSize;
    bool imagenetNorm;
    const char *sizeText;
};
const ModelSpec kModels[] = {
    {"u2netp", "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx",
     1'000'000, 320, true, "4.5 MB"},
    {"u2net", "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx",
     100'000'000, 320, true, "168 MB"},
    {"u2net_human_seg",
     "https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net_human_seg.onnx",
     100'000'000, 320, true, "168 MB"},
    {"silueta", "https://github.com/danielgatis/rembg/releases/download/v0.0.0/silueta.onnx",
     30'000'000, 320, true, "43 MB"},
    {"isnet-general-use",
     "https://github.com/danielgatis/rembg/releases/download/v0.0.0/isnet-general-use.onnx",
     100'000'000, 1024, false, "170 MB"},
};

const ModelSpec *specFor(const QString &id)
{
    for (const ModelSpec &m : kModels)
        if (id == QLatin1String(m.id))
            return &m;
    return nullptr;
}

// ImageNet normalization used by the u2net family's preprocessing.
constexpr float kImagenetMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kImagenetStd[3]  = {0.229f, 0.224f, 0.225f};
} // namespace

struct U2NetSegmenter::State {
    // Model SELECTION lives under its own tiny mutex (cfgMutex, never held
    // across work): setModel() runs on the GUI thread and must never wait for
    // `mutex`, which a worker holds across a multi-second ORT inference — that
    // would freeze the UI on a combo change. segmentSync() adopts the latest
    // selection at the start of each run.
    QString modelId = QStringLiteral("u2netp");
    QString customPath;
    mutable std::mutex cfgMutex;

    // Selection the loaded session was built for (worker-side, under `mutex`).
    QString loadedId;
    QString loadedPath;

    // Held across the whole (multi-second, uninterruptible) ORT inference so a
    // model swap or object teardown can't race the session.
    std::mutex mutex;

    // Set by ~U2NetSegmenter before it drops its reference; a worker that has
    // not yet started the expensive Run bails instead of doing pointless work.
    std::atomic_bool cancelled{false};

    // ORT objects, created lazily and reused.
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "unisic-u2net"};
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
    int inputSize = 320; // resolved from the model when it declares fixed dims

    bool ensureSession(const std::string &modelPath, int fallbackSize)
    {
        if (session)
            return true;
        try {
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(1);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            session = std::make_unique<Ort::Session>(env, modelPath.c_str(), opts);
            Ort::AllocatorWithDefaultOptions alloc;
            inputName = session->GetInputNameAllocated(0, alloc).get();
            outputName = session->GetOutputNameAllocated(0, alloc).get();
            // Ask the model for its input side (NCHW): fixed dims beat any
            // guess — this is what lets an arbitrary CUSTOM .onnx work.
            // Dynamic dims (-1) fall back to the spec/default size.
            inputSize = fallbackSize > 0 ? fallbackSize : 320;
            const auto shape =
                session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
            if (shape.size() == 4 && shape[2] > 0 && shape[2] == shape[3]
                && shape[2] <= 4096)
                inputSize = int(shape[2]);
            return true;
        } catch (const std::exception &) {
            session.reset();
            return false;
        }
    }

    bool modelReady() const
    {
        QString p, id;
        {
            std::lock_guard<std::mutex> cfg(cfgMutex);
            id = modelId;
            p = (id == QLatin1String("custom")) ? customPath : U2NetSegmenter::pathFor(id);
        }
        if (p.isEmpty())
            return false;
        const QFileInfo fi(p);
        // A truncated/failed download would be tiny. Catalog models have a
        // known floor; for a custom file any plausible ONNX size is accepted.
        const ModelSpec *s = specFor(id);
        const qint64 floor = s ? s->minBytes : 100'000;
        return fi.exists() && fi.size() > floor;
    }

    QImage segmentSync(const QImage &img, const QRect &region);
};

U2NetSegmenter::U2NetSegmenter(QObject *parent)
    : QObject(parent), m_state(std::make_shared<State>())
{
}

U2NetSegmenter::~U2NetSegmenter()
{
    // No lock, no wait: every worker holds its own shared_ptr to m_state, so
    // the ORT session and both mutexes outlive this object as long as any
    // queued or in-flight task can still reach them. Flipping `cancelled` just
    // lets a not-yet-started worker skip the pointless inference.
    m_state->cancelled.store(true);
}

QStringList U2NetSegmenter::modelIds()
{
    QStringList ids;
    for (const ModelSpec &m : kModels)
        ids << QString::fromLatin1(m.id);
    return ids;
}

QString U2NetSegmenter::modelSizeText(const QString &id)
{
    const ModelSpec *s = specFor(id);
    return s ? QString::fromLatin1(s->sizeText) : QString();
}

QString U2NetSegmenter::pathFor(const QString &id)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + id + QStringLiteral(".onnx");
}

void U2NetSegmenter::setModel(const QString &id, const QString &customPath)
{
    // GUI thread. Only records the selection — NEVER touches the ORT session
    // and never takes State::mutex (a worker holds that across a multi-second
    // inference; waiting here would freeze the UI on a combo change).
    // segmentSync() adopts the new selection at the start of its next run.
    std::lock_guard<std::mutex> cfg(m_state->cfgMutex);
    m_state->modelId = (specFor(id) || id == QLatin1String("custom"))
                           ? id : QStringLiteral("u2netp");
    m_state->customPath = customPath;
}

QString U2NetSegmenter::activeModelId() const
{
    std::lock_guard<std::mutex> cfg(m_state->cfgMutex);
    return m_state->modelId;
}

QString U2NetSegmenter::modelPath() const
{
    std::lock_guard<std::mutex> cfg(m_state->cfgMutex);
    if (m_state->modelId == QLatin1String("custom"))
        return m_state->customPath;
    return pathFor(m_state->modelId);
}

bool U2NetSegmenter::modelReady() const
{
    return m_state->modelReady();
}

QImage U2NetSegmenter::State::segmentSync(const QImage &img, const QRect &region)
{
    if (img.isNull() || !modelReady())
        return {};
    const QRect r = region.intersected(img.rect());
    if (r.width() < 8 || r.height() < 8)
        return {};

    std::lock_guard<std::mutex> lock(mutex);
    // Bail if the owning segmenter is gone (its dtor set this before dropping
    // its reference to this State).
    if (cancelled.load())
        return {};
    // Adopt the latest model selection (recorded by the GUI thread in
    // setModel) — a change drops the loaded session here, on the worker, where
    // the reload belongs.
    QString id, path;
    {
        std::lock_guard<std::mutex> cfg(cfgMutex);
        id = modelId;
        path = (id == QLatin1String("custom")) ? customPath : U2NetSegmenter::pathFor(id);
    }
    if (id != loadedId || path != loadedPath) {
        session.reset();
        loadedId = id;
        loadedPath = path;
    }
    const ModelSpec *spec = specFor(id);
    if (!ensureSession(path.toStdString(), spec ? spec->inputSize : 0)) {
        // A corrupt/truncated CATALOG model that passes the size check but
        // fails to load would break background removal forever with no
        // recourse — delete it so the next attempt re-downloads (self-heal,
        // complements the atomic write in downloadModel). A custom file is
        // the user's — never delete it.
        if (id != QLatin1String("custom"))
            QFile::remove(path);
        return {};
    }

    // Preprocess: crop → model-input RGB → NCHW float, normalized per model
    // family (u2net: ImageNet mean/std; IS-Net and unknown-custom fallback:
    // see below).
    const int size = inputSize;
    const bool imagenetNorm = spec ? spec->imagenetNorm
                                   : true; // custom: u2net-style is the common case
    const float *mean = kImagenetMean;
    const float *stddev = kImagenetStd;
    constexpr float kHalfMean[3] = {0.5f, 0.5f, 0.5f};
    constexpr float kUnitStd[3] = {1.0f, 1.0f, 1.0f};
    if (!imagenetNorm) { mean = kHalfMean; stddev = kUnitStd; }
    const QImage crop = img.copy(r).convertToFormat(QImage::Format_RGB888)
                            .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> input(static_cast<size_t>(3) * size * size);
    for (int y = 0; y < size; ++y) {
        const uchar *line = crop.constScanLine(y);
        for (int x = 0; x < size; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float v = line[x * 3 + c] / 255.0f;
                input[static_cast<size_t>(c) * size * size + y * size + x] =
                    (v - mean[c]) / stddev[c];
            }
        }
    }

    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> shape{1, 3, size, size};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            mem, input.data(), input.size(), shape.data(), shape.size());
        const char *inNames[] = {inputName.c_str()};
        const char *outNames[] = {outputName.c_str()};
        auto outputs = session->Run(Ort::RunOptions{nullptr},
                                    inNames, &tensor, 1, outNames, 1);
        // Validate the output tensor before reading it as a float saliency map.
        // A custom .onnx whose first output is not float, or is smaller than the
        // spatial map we expect, would otherwise over-read the ORT buffer by
        // megabytes (crash) or reinterpret non-float bytes as floats (garbage).
        const auto tinfo = outputs.front().GetTensorTypeAndShapeInfo();
        if (tinfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            return {};
        const auto oshape = tinfo.GetShape();
        // The saliency map is the trailing (H, W); accept [H,W], [1,H,W],
        // [1,1,H,W]. Fall back to the input side only when the shape is opaque.
        int oh = size, ow = size;
        if (oshape.size() >= 2) {
            ow = int(oshape[oshape.size() - 1]);
            oh = int(oshape[oshape.size() - 2]);
        }
        if (oh <= 0 || ow <= 0
            || tinfo.GetElementCount() < static_cast<size_t>(oh) * ow)
            return {};
        const float *out = outputs.front().GetTensorData<float>();
        const size_t n = static_cast<size_t>(ow) * oh;

        // Min-max normalize the saliency map to 0..1 (U-2-Net's d0 output).
        float lo = out[0], hi = out[0];
        for (size_t i = 1; i < n; ++i) { lo = std::min(lo, out[i]); hi = std::max(hi, out[i]); }
        const float span = (hi - lo) > 1e-6f ? (hi - lo) : 1.0f;

        QImage small(ow, oh, QImage::Format_Grayscale8);
        for (int y = 0; y < oh; ++y) {
            uchar *line = small.scanLine(y);
            for (int x = 0; x < ow; ++x) {
                const float m = (out[y * ow + x] - lo) / span;
                line[x] = static_cast<uchar>(std::clamp(m, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
        // Back to region size (soft edges are fine — DestinationIn uses the
        // gray value directly as alpha).
        return small.scaled(r.width(), r.height(), Qt::IgnoreAspectRatio,
                            Qt::SmoothTransformation);
    } catch (const std::exception &) {
        return {};
    }
}

QImage U2NetSegmenter::segmentSync(const QImage &img, const QRect &region)
{
    return m_state->segmentSync(img, region);
}

void U2NetSegmenter::segment(const QImage &img, const QRect &region, Result cb)
{
    auto *fw = new QFutureWatcher<QImage>(this);
    connect(fw, &QFutureWatcher<QImage>::finished, this, [fw, cb]() {
        const QImage mask = fw->result();
        fw->deleteLater();
        cb(mask, mask.isNull() ? QObject::tr("Background removal failed") : QString());
    });
    // Capture a shared_ptr copy of the state, never `this`: the task may outlive
    // the segmenter (queued in the global pool at shutdown), and State keeps the
    // session + mutexes alive for exactly as long as the task can reach them.
    auto state = m_state;
    fw->setFuture(QtConcurrent::run([state, img, region]() { return state->segmentSync(img, region); }));
}

std::function<QImage(const QImage &, const QRect &)> U2NetSegmenter::makeWorker() const
{
    auto state = m_state;
    return [state](const QImage &img, const QRect &region) -> QImage {
        if (state->cancelled.load() || !state->modelReady())
            return {};
        return state->segmentSync(img, region);
    };
}

void U2NetSegmenter::downloadModel(std::function<void(bool, const QString &)> done)
{
    const ModelSpec *spec = specFor(activeModelId());
    if (!spec) {
        done(false, tr("A custom model is a file you provide — pick the .onnx on disk instead of downloading"));
        return;
    }
    if (modelReady()) {
        done(true, tr("Model already downloaded"));
        return;
    }
    const qint64 minBytes = spec->minBytes;
    // Resolve the destination NOW, from the model being downloaded — not via
    // modelPath() in the finished handler, which reads the CURRENT selection.
    // Otherwise switching the model combo mid-download installs these bytes
    // under the newly-selected model's filename (wrong weights forever).
    const QString finalPath = pathFor(QString::fromLatin1(spec->id));
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QString::fromLatin1(spec->url))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    // Bound the download so a hung/stalled server doesn't leave the UI stuck on
    // "Downloading…" forever (finished still fires, with a timeout error). The
    // big models (~170 MB) need real time; the small ones fail fast.
    req.setTransferTimeout(minBytes > 10'000'000 ? 600000 : 60000);
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, nam, done, minBytes, finalPath]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, tr("Download failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() < minBytes) {
            done(false, tr("Downloaded model looks truncated"));
            return;
        }
        // Write to a temp file and atomically rename into place, so a short
        // write (ENOSPC, kill) never leaves a truncated model that the size
        // check would accept forever with no way to recover.
        const QString tmp = finalPath + QStringLiteral(".part");
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.flush()) {
            f.close();
            QFile::remove(tmp);
            done(false, tr("Could not write the model file"));
            return;
        }
        f.close();
        if (QFileInfo(tmp).size() != data.size()) {
            QFile::remove(tmp);
            done(false, tr("Downloaded model looks truncated"));
            return;
        }
        QFile::remove(finalPath); // rename won't overwrite on all platforms
        if (!QFile::rename(tmp, finalPath)) {
            QFile::remove(tmp);
            done(false, tr("Could not write the model file"));
            return;
        }
        done(true, tr("Model downloaded"));
    });
}

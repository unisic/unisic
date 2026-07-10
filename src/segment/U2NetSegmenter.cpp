#include "U2NetSegmenter.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
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
// u2netp (the small 4.5 MB U-2-Net variant) from the pinned rembg release.
// The URL and hash are pinned so a downloaded model is exactly the one this
// code was written against; a mismatch is rejected.
constexpr int kSize = 320; // model input side (u2netp is 320x320)
const QString kModelUrl =
    QStringLiteral("https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx");
// SHA-256 of u2netp.onnx. TODO: pin the real 64-hex digest once a build with
// onnxruntime has downloaded and validated the asset. Empty = accept by size
// only (still an https download from a fixed URL).
const QString kModelSha256 = QString();

// ImageNet normalization used by U-2-Net preprocessing.
constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3]  = {0.229f, 0.224f, 0.225f};
} // namespace

struct U2NetSegmenter::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "unisic-u2net"};
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;

    bool ensureSession(const std::string &modelPath)
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
            return true;
        } catch (const std::exception &) {
            session.reset();
            return false;
        }
    }
};

U2NetSegmenter::U2NetSegmenter(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
}

U2NetSegmenter::~U2NetSegmenter()
{
    m_cancelled->store(true);
}

QString U2NetSegmenter::modelPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/u2netp.onnx");
}

bool U2NetSegmenter::modelReady() const
{
    const QFileInfo fi(modelPath());
    // A truncated/failed download would be tiny; the real model is ~4.5 MB.
    return fi.exists() && fi.size() > 1'000'000;
}

QImage U2NetSegmenter::segmentSync(const QImage &img, const QRect &region)
{
    if (img.isNull() || !modelReady())
        return {};
    const QRect r = region.intersected(img.rect());
    if (r.width() < 8 || r.height() < 8)
        return {};

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_impl->ensureSession(modelPath().toStdString()))
        return {};

    // Preprocess: crop → 320×320 RGB → NCHW float, ImageNet-normalized.
    const QImage crop = img.copy(r).convertToFormat(QImage::Format_RGB888)
                            .scaled(kSize, kSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> input(static_cast<size_t>(3) * kSize * kSize);
    for (int y = 0; y < kSize; ++y) {
        const uchar *line = crop.constScanLine(y);
        for (int x = 0; x < kSize; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float v = line[x * 3 + c] / 255.0f;
                input[static_cast<size_t>(c) * kSize * kSize + y * kSize + x] =
                    (v - kMean[c]) / kStd[c];
            }
        }
    }

    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 4> shape{1, 3, kSize, kSize};
        Ort::Value tensor = Ort::Value::CreateTensor<float>(
            mem, input.data(), input.size(), shape.data(), shape.size());
        const char *inNames[] = {m_impl->inputName.c_str()};
        const char *outNames[] = {m_impl->outputName.c_str()};
        auto outputs = m_impl->session->Run(Ort::RunOptions{nullptr},
                                            inNames, &tensor, 1, outNames, 1);
        const float *out = outputs.front().GetTensorData<float>();
        const size_t n = static_cast<size_t>(kSize) * kSize;

        // Min-max normalize the saliency map to 0..1 (U-2-Net's d0 output).
        float lo = out[0], hi = out[0];
        for (size_t i = 1; i < n; ++i) { lo = std::min(lo, out[i]); hi = std::max(hi, out[i]); }
        const float span = (hi - lo) > 1e-6f ? (hi - lo) : 1.0f;

        QImage small(kSize, kSize, QImage::Format_Grayscale8);
        for (int y = 0; y < kSize; ++y) {
            uchar *line = small.scanLine(y);
            for (int x = 0; x < kSize; ++x) {
                const float m = (out[y * kSize + x] - lo) / span;
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

void U2NetSegmenter::segment(const QImage &img, const QRect &region, Result cb)
{
    auto *fw = new QFutureWatcher<QImage>(this);
    connect(fw, &QFutureWatcher<QImage>::finished, this, [fw, cb]() {
        const QImage mask = fw->result();
        fw->deleteLater();
        cb(mask, mask.isNull() ? QObject::tr("Background removal failed") : QString());
    });
    fw->setFuture(QtConcurrent::run([this, img, region]() { return segmentSync(img, region); }));
}

void U2NetSegmenter::downloadModel(std::function<void(bool, const QString &)> done)
{
    if (modelReady()) {
        done(true, tr("Model already downloaded"));
        return;
    }
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(kModelUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, done]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, tr("Download failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        // The pinned hash guards against a tampered/mismatched asset. A blank or
        // placeholder pin (development) accepts by size only.
        QString pin = kModelSha256;
        pin.remove(QLatin1Char(' '));
        if (pin.size() == 64 && sha.compare(pin, Qt::CaseInsensitive) != 0) {
            done(false, tr("Downloaded model failed its checksum — refusing to install"));
            return;
        }
        if (data.size() < 1'000'000) {
            done(false, tr("Downloaded model looks truncated"));
            return;
        }
        QFile f(modelPath());
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            done(false, tr("Could not write the model file"));
            return;
        }
        f.close();
        done(true, tr("Model downloaded"));
    });
}

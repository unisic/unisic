#include "BackgroundRemover.h"

#include <onnxruntime/onnxruntime_c_api.h>

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLibrary>
#include <QLibraryInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVersionNumber>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

namespace {
// Both files are pinned twice: by a release URL and by size and SHA-256, so
// neither can change under a user.
struct Pin {
    const char *url;
    qint64 bytes;
    const char *sha256;
};
enum { RuntimePin, ModelPin };
const Pin kPins[] = {
    {"https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/"
     "onnxruntime-linux-x64-1.31.1.tgz",
     11413655, "c849e36a1ef3e22d4ff217b9d1ce4a3cf5b9467b975610913ae959ad5b16bdc9"},
    // scripts/birefnet-deformconv.py applied to the onnx-community export
    // (docs/dev/packaging.md "Background removal").
    {"https://github.com/unisic/unisic-models/releases/download/birefnet-lite-dcn-1/"
     "birefnet-lite-dcn.onnx",
     181120537, "ef0bfeb72e5d83fa032433b4017accd338b74bcfb98af53f3240a751ef0ae961"},
};
// The first release with a CPU kernel for DeformConv, which the model uses
// (microsoft/onnxruntime 163f614, in v1.25.0 and not in v1.24.4). Older ones
// would load and then fail on every run, so the probe passes them over.
const QVersionNumber kMinRuntime(1, 25);
constexpr const char *kRuntimeMember = "onnxruntime-linux-x64-1.31.1/lib/libonnxruntime.so.1.31.1";
constexpr const char *kRuntimeFile = "libonnxruntime.so.1.31.1";

// BiRefNet's contract (onnx-community export): a 1024 x 1024 RGB tensor with
// ImageNet normalization in, one channel of logits at the same size out.
constexpr int kSide = 1024;
constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};
// Peak RSS of one run including the model load, measured with ORT 1.31.1 on
// x86_64: 1.93 GB, against 6.6 GB for the export as published, whose
// deformable convolutions are GatherND/Mul/Transpose over 784 MB tensors.
// Starting a run without that much free memory pushes the desktop into swap
// or the OOM killer.
constexpr qint64 kRunMiB = 2200;
// The foreground colour pass holds about this much per pixel of the picture
// (four float planes and three 8-bit images), next to the loaded model.
constexpr qint64 kCutBytesPerPixel = 50;
constexpr qint64 kModelMiB = 400;
// How long a loaded model outlives its last run: ~2 s of load saved per run
// against ~390 MB held.
constexpr int kIdleMs = 60000;

// MemAvailable from /proc/meminfo in MiB, -1 where there is no such file.
qint64 availableMiB()
{
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith("MemAvailable:"))
            return line.mid(13).trimmed().split(' ').value(0).toLongLong() / 1024;
    return -1;
}

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString runtimeDir()
{
    return dataDir() + QStringLiteral("/onnxruntime");
}

struct Cut {
    QImage image;
    QString error;
};

// Three floats per pixel, row-major.
struct Plane {
    int w = 0;
    int h = 0;
    std::vector<float> v;
    float *at(int x, int y) { return &v[(size_t(y) * w + x) * 3]; }
    const float *at(int x, int y) const { return &v[(size_t(y) * w + x) * 3]; }
};

Plane resizeNearest(const Plane &s, int w, int h)
{
    Plane d{w, h, std::vector<float>(size_t(w) * h * 3)};
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(s.h - 1, y * s.h / h);
        for (int x = 0; x < w; ++x)
            std::copy_n(s.at(std::min(s.w - 1, x * s.w / w), sy), 3, d.at(x, y));
    }
    return d;
}

std::vector<float> resizeNearest(const std::vector<float> &a, int sw, int sh, int w, int h)
{
    std::vector<float> d(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            d[size_t(y) * w + x] = a[size_t(std::min(sh - 1, y * sh / h)) * sw + std::min(sw - 1, x * sw / w)];
    return d;
}

// The colour of the subject alone under every pixel the mask leaves partly
// transparent. Keeping the source colour there drags the old background along
// as a halo round hair and soft edges. Germer et al. 2020, "Fast Multi-Level
// Foreground Estimation": solves I = aF + (1 - a)B for F and B, smooth where
// the alpha is, coarse to fine; ~60-170 ms single-threaded up to 2 Mpx.
Plane estimateForeground(const Plane &img, const std::vector<float> &alpha)
{
    const int w0 = img.w, h0 = img.h;
    const int levels = std::max(1, int(std::ceil(std::log2(std::max(w0, h0)))));
    constexpr float reg = 1e-5f;
    float mean[3] = {0, 0, 0};
    for (size_t i = 0; i < img.v.size(); ++i)
        mean[i % 3] += img.v[i];
    for (float &m : mean)
        m /= float(size_t(w0) * h0);
    Plane F{1, 1, {mean[0], mean[1], mean[2]}};
    Plane B = F;
    Plane smallImg;
    std::vector<float> smallAlpha;
    for (int level = 0; level <= levels; ++level) {
        const int w = int(std::lround(std::pow(w0, double(level) / levels)));
        const int h = int(std::lround(std::pow(h0, double(level) / levels)));
        const bool full = w == w0 && h == h0;
        if (!full) {
            smallImg = resizeNearest(img, w, h);
            smallAlpha = resizeNearest(alpha, w0, h0, w, h);
        } else {
            smallImg = {};
            smallAlpha = {};
        }
        const Plane &I = full ? img : smallImg;
        const std::vector<float> &a = full ? alpha : smallAlpha;
        F = resizeNearest(F, w, h);
        B = resizeNearest(B, w, h);
        const int iterations = (w <= 32 && h <= 32) ? 10 : 2;
        for (int it = 0; it < iterations; ++it) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const float a0 = a[size_t(y) * w + x], a1 = 1.0f - a0;
                    float a00 = a0 * a0, a11 = a1 * a1;
                    const float a01 = a0 * a1;
                    float b0[3] = {0, 0, 0}, b1[3] = {0, 0, 0};
                    static constexpr int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                    for (int k = 0; k < 4; ++k) {
                        const int x2 = std::clamp(x + dx[k], 0, w - 1), y2 = std::clamp(y + dy[k], 0, h - 1);
                        const float da = reg + std::fabs(a0 - a[size_t(y2) * w + x2]);
                        a00 += da;
                        a11 += da;
                        const float *f = F.at(x2, y2), *b = B.at(x2, y2);
                        for (int c = 0; c < 3; ++c) {
                            b0[c] += da * f[c];
                            b1[c] += da * b[c];
                        }
                    }
                    const float *p = I.at(x, y);
                    const float inv = 1.0f / (a00 * a11 - a01 * a01);
                    float *f = F.at(x, y), *b = B.at(x, y);
                    for (int c = 0; c < 3; ++c) {
                        const float u = b0[c] + a0 * p[c], v = b1[c] + a1 * p[c];
                        f[c] = std::clamp(inv * (a11 * u - a01 * v), 0.0f, 1.0f);
                        b[c] = std::clamp(inv * (a00 * v - a01 * u), 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    return F;
}
} // namespace

// Process-lifetime once loaded: ORT does not survive being unloaded, and the
// env is never released for the same reason. A worker holds a reference, so
// a run in flight outlives the BackgroundRemover that started it.
struct BackgroundRemover::Runtime {
    QLibrary lib;
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;

    std::mutex mutex;              // guards running, stop and idle
    OrtRunOptions *running = nullptr;
    bool stop = false;
    // The session of the last run, kept for the next one. A run takes it out
    // and puts it back when it is done, so the one in use is never in here.
    OrtSession *idle = nullptr;

    void terminate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
        if (running)
            if (OrtStatus *s = api->RunOptionsSetTerminate(running))
                api->ReleaseStatus(s);
    }

    void dropIdle()
    {
        OrtSession *s = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            s = std::exchange(idle, nullptr);
        }
        if (s)
            api->ReleaseSession(s);
    }

    QImage mask(const QString &modelFile, const QImage &src, QString *err);
    Cut cutOut(const QString &modelFile, const QImage &src);
};

// The 1024 x 1024 alpha mask. Everything ORT allocates is released before it
// returns, so only the small mask survives into the full-size compositing.
QImage BackgroundRemover::Runtime::mask(const QString &modelFile, const QImage &src, QString *err)
{
    const OrtApi *a = api;
    auto ok = [a, err](OrtStatus *s) {
        if (!s)
            return true;
        *err = QString::fromUtf8(a->GetErrorMessage(s));
        a->ReleaseStatus(s);
        return false;
    };

    const QImage rgb = src.scaled(kSide, kSide, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB888);
    std::vector<float> input(size_t(3) * kSide * kSide);
    const size_t plane = size_t(kSide) * kSide;
    for (int y = 0; y < kSide; ++y) {
        const uchar *line = rgb.constScanLine(y);
        for (int x = 0; x < kSide; ++x)
            for (int c = 0; c < 3; ++c)
                input[c * plane + size_t(y) * kSide + x] = (line[x * 3 + c] / 255.0f - kMean[c]) / kStd[c];
    }

    OrtSession *session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        session = std::exchange(idle, nullptr);
    }
    OrtMemoryInfo *mem = nullptr;
    OrtValue *in = nullptr;
    OrtValue *out = nullptr;
    OrtRunOptions *run = nullptr;
    bool keep = false;
    const auto release = qScopeGuard([&] {
        if (out) a->ReleaseValue(out);
        if (in) a->ReleaseValue(in);
        if (run) a->ReleaseRunOptions(run);
        if (mem) a->ReleaseMemoryInfo(mem);
        if (session && keep) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!stop && !idle)
                idle = std::exchange(session, nullptr);
        }
        if (session) a->ReleaseSession(session);
    });

    if (!session) {
        QFile file(modelFile);
        if (!file.open(QIODevice::ReadOnly)) {
            *err = file.errorString();
            return {};
        }
        uchar *bytes = file.map(0, file.size());
        if (!bytes) {
            *err = file.errorString();
            return {};
        }
        // No arena: its blocks would stay with the process after the run.
        OrtSessionOptions *opts = nullptr;
        const bool created = ok(a->CreateSessionOptions(&opts))
                             && ok(a->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL))
                             && ok(a->DisableCpuMemArena(opts))
                             && ok(a->CreateSessionFromArray(env, bytes, size_t(file.size()), opts, &session));
        if (opts)
            a->ReleaseSessionOptions(opts);
        file.unmap(bytes);
        if (!created)
            return {};
    }
    const int64_t shape[4] = {1, 3, kSide, kSide};
    const bool built = ok(a->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &mem))
            && ok(a->CreateTensorWithDataAsOrtValue(mem, input.data(), input.size() * sizeof(float), shape, 4,
                                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in))
            && ok(a->CreateRunOptions(&run));
    if (!built)
        return {};
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stop) {
            *err = QStringLiteral("stopped");
            return {};
        }
        running = run;
    }
    const char *inName = "input_image";
    const char *outName = "output_image";
    OrtStatus *ran = a->Run(session, run, &inName, &in, 1, &outName, 1, &out);
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = nullptr;
    }
    if (!ok(ran))
        return {};
    keep = true;

    OrtTensorTypeAndShapeInfo *info = nullptr;
    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    size_t rank = 0;
    int64_t dims[4] = {};
    float *logits = nullptr;
    const bool shaped = ok(a->GetTensorTypeAndShape(out, &info))
                        && ok(a->GetTensorElementType(info, &type))
                        && ok(a->GetDimensionsCount(info, &rank)) && rank == 4
                        && ok(a->GetDimensions(info, dims, 4));
    if (info)
        a->ReleaseTensorTypeAndShapeInfo(info);
    if (!shaped || type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || dims[0] != 1 || dims[1] != 1
        || dims[2] != kSide || dims[3] != kSide
        || !ok(a->GetTensorMutableData(out, reinterpret_cast<void **>(&logits)))) {
        if (err->isEmpty())
            *err = QStringLiteral("unexpected model output");
        return {};
    }
    QImage m(kSide, kSide, QImage::Format_Grayscale8);
    for (int y = 0; y < kSide; ++y) {
        uchar *line = m.scanLine(y);
        const float *row = logits + size_t(y) * kSide;
        for (int x = 0; x < kSide; ++x)
            line[x] = uchar(255.0f / (1.0f + std::exp(-row[x])) + 0.5f);
    }
    return m;
}

Cut BackgroundRemover::Runtime::cutOut(const QString &modelFile, const QImage &src)
{
    QString err;
    const QImage small = mask(modelFile, src, &err);
    if (small.isNull())
        return {{}, err};
    const QImage mask = small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    Plane colour{w, h, std::vector<float>(size_t(w) * h * 3)};
    std::vector<float> alpha(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const auto *px = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        const uchar *m = mask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            float *c = colour.at(x, y);
            c[0] = qRed(px[x]) / 255.0f;
            c[1] = qGreen(px[x]) / 255.0f;
            c[2] = qBlue(px[x]) / 255.0f;
            alpha[size_t(y) * w + x] = m[x] / 255.0f;
        }
    }
    const Plane fg = estimateForeground(colour, alpha);
    for (int y = 0; y < h; ++y) {
        auto *px = reinterpret_cast<QRgb *>(img.scanLine(y));
        const uchar *m = mask.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const float *f = fg.at(x, y);
            px[x] = qRgba(int(f[0] * 255.0f + 0.5f), int(f[1] * 255.0f + 0.5f), int(f[2] * 255.0f + 0.5f),
                          (qAlpha(px[x]) * m[x] + 127) / 255);
        }
    }
    return {std::move(img).convertToFormat(QImage::Format_ARGB32_Premultiplied), {}};
}

BackgroundRemover::BackgroundRemover(QObject *parent)
    : QObject(parent)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (m_rt)
            m_rt->dropIdle();
    });
}

BackgroundRemover::~BackgroundRemover()
{
    // A run in flight would hold Unisic's exit for its remaining seconds; a
    // stopped run releases its own session.
    if (m_rt) {
        m_rt->terminate();
        m_rt->dropIdle();
    }
}

QString BackgroundRemover::modelPath()
{
    return dataDir() + QStringLiteral("/models/birefnet-lite.onnx");
}

bool BackgroundRemover::canDownloadRuntime() const
{
    // Inside the Flatpak the bundled runtime is the only one that can load.
    return available() && QSysInfo::currentCpuArchitecture() == QLatin1String("x86_64")
           && !QFile::exists(QStringLiteral("/.flatpak-info"));
}

bool BackgroundRemover::modelInstalled() const
{
    return QFileInfo(modelPath()).size() == kPins[ModelPin].bytes;
}

bool BackgroundRemover::hasDownloads() const
{
    return QFile::exists(modelPath()) || QFile::exists(runtimeDir() + QLatin1Char('/') + QLatin1String(kRuntimeFile));
}

bool BackgroundRemover::runtimeFound()
{
    if (!m_probed)
        probe();
    return m_rt != nullptr;
}

QString BackgroundRemover::runtimeInfo()
{
    runtimeFound();
    return m_runtimeInfo;
}

// The soname differs by source: libonnxruntime.so.1 from upstream, the
// Flatpak, Fedora and Arch; libonnxruntime.so.1.<minor> on Debian and Ubuntu,
// next to Qt's own libraries; and the copy download() unpacked.
void BackgroundRemover::probe()
{
    m_probed = true;
    if (m_rt)
        return;
    QList<std::pair<QString, int>> candidates{{QStringLiteral("onnxruntime"), 1}};
    const QDir qtLibs(QLibraryInfo::path(QLibraryInfo::LibrariesPath));
    for (const QFileInfo &fi : qtLibs.entryInfoList({QStringLiteral("libonnxruntime.so.1.*")}, QDir::Files,
                                                    QDir::Name | QDir::Reversed))
        candidates.append({fi.absoluteFilePath(), -1});
    candidates.append({runtimeDir() + QLatin1Char('/') + QLatin1String(kRuntimeFile), -1});

    m_runtimeInfo.clear();
    for (const auto &[name, version] : candidates) {
        auto rt = std::make_shared<Runtime>();
        if (version >= 0)
            rt->lib.setFileNameAndVersion(name, version);
        else
            rt->lib.setFileName(name);
        if (!rt->lib.load())
            continue;
        using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();
        const auto getBase = reinterpret_cast<GetApiBase>(rt->lib.resolve("OrtGetApiBase"));
        const OrtApiBase *base = getBase ? getBase() : nullptr;
        if (!base) {
            rt->lib.unload();
            continue;
        }
        const QString ver = QString::fromUtf8(base->GetVersionString());
        rt->api = QVersionNumber::fromString(ver) >= kMinRuntime ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (!rt->api) {
            m_runtimeInfo = tr("ONNX Runtime %1 is too old, %2 or newer is needed").arg(ver, kMinRuntime.toString());
            rt->lib.unload();
            continue;
        }
        if (OrtStatus *s = rt->api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "unisic", &rt->env)) {
            m_runtimeInfo = QString::fromUtf8(rt->api->GetErrorMessage(s));
            rt->api->ReleaseStatus(s);
            continue;
        }
        m_rt = rt;
        m_runtimeInfo = tr("ONNX Runtime %1").arg(ver);
        qInfo().noquote() << "background removal: ONNX Runtime" << ver << "from" << rt->lib.fileName();
        return;
    }
}

QString BackgroundRemover::downloadSizeText()
{
    qint64 bytes = 0;
    if (!runtimeFound() && canDownloadRuntime())
        bytes += kPins[RuntimePin].bytes;
    if (!modelInstalled())
        bytes += kPins[ModelPin].bytes;
    return QLocale().formattedDataSize(bytes, 0, QLocale::DataSizeTraditionalFormat);
}

QNetworkAccessManager *BackgroundRemover::nam()
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    return m_nam;
}

void BackgroundRemover::fail(const QString &message)
{
    qWarning().noquote() << "background removal download:" << message;
    m_queue.clear();
    m_error = message;
    emit stateChanged();
}

void BackgroundRemover::download()
{
    if (downloading())
        return;
    m_error.clear();
    m_queue.clear();
    m_cancelled = false;
    if (!runtimeFound() && canDownloadRuntime())
        m_queue << RuntimePin;
    if (!modelInstalled())
        m_queue << ModelPin;
    m_totalBytes = 0;
    for (int pin : std::as_const(m_queue))
        m_totalBytes += kPins[pin].bytes;
    m_doneBytes = 0;
    m_progress = 0;
    emit progressChanged();
    fetchNext();
}

void BackgroundRemover::fetchNext()
{
    if (m_queue.isEmpty()) {
        emit stateChanged();
        return;
    }
    fetch(m_queue.takeFirst());
}

void BackgroundRemover::fetch(int pin)
{
    const Pin &p = kPins[pin];
    const QString dest = pin == ModelPin ? modelPath() : runtimeDir() + QStringLiteral("/runtime.tgz");
    auto file = std::make_shared<QSaveFile>(dest);
    if (!QDir().mkpath(QFileInfo(dest).absolutePath()) || !file->open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1: %2").arg(dest, file->errorString()));
        return;
    }
    QNetworkRequest req{QUrl(QString::fromLatin1(p.url))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Unisic/" UNISIC_VERSION " (screenshot tool)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    // Without a byte for a minute, not for the whole 181 MB.
    req.setTransferTimeout(60000);
    QNetworkReply *reply = nam()->get(req);
    m_reply = reply;
    emit stateChanged();

    auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto received = std::make_shared<qint64>(0);
    // Why this side stopped the transfer; empty for a cancel or a timeout.
    auto problem = std::make_shared<QString>();
    // Streamed to disk and hashed as it arrives; a reply that runs past the
    // pinned size is cut off rather than written.
    auto take = [reply, file, hash, received, problem, bytes = p.bytes] {
        const QByteArray chunk = reply->readAll();
        *received += chunk.size();
        if (*received > bytes)
            *problem = tr("The download is larger than expected, so it was not kept");
        else if (file->write(chunk) != chunk.size())
            *problem = tr("Cannot write %1: %2").arg(file->fileName(), file->errorString());
        else
            hash->addData(chunk);
        return problem->isEmpty();
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, take] {
        if (!take())
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64) {
        m_progress = m_totalBytes > 0 ? qreal(m_doneBytes + got) / m_totalBytes : 0;
        emit progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, received, problem, take, pin, dest] {
        reply->deleteLater();
        m_reply = nullptr;
        const Pin &p = kPins[pin];
        const QNetworkReply::NetworkError error = reply->error();
        if (error == QNetworkReply::NoError)
            take();
        if (error != QNetworkReply::NoError || !problem->isEmpty()) {
            file->cancelWriting();
            if (!problem->isEmpty())
                fail(*problem);
            else if (error != QNetworkReply::OperationCanceledError)
                fail(tr("Download failed: %1").arg(reply->errorString()));
            else if (!m_cancelled)
                fail(tr("Download stopped: nothing arrived for a minute"));
            else
                emit stateChanged();
            return;
        }
        if (*received != p.bytes || hash->result().toHex() != QByteArray(p.sha256)) {
            file->cancelWriting();
            fail(tr("The download does not match the expected file, so it was not kept"));
            return;
        }
        if (!file->commit()) {
            fail(tr("Cannot write %1: %2").arg(dest, file->errorString()));
            return;
        }
        m_doneBytes += p.bytes;
        if (pin == RuntimePin)
            installRuntime(dest);
        else
            fetchNext();
    });
}

// The archive is already verified, so only the one library is unpacked from
// it, by name. tar is part of every Linux base system.
void BackgroundRemover::installRuntime(const QString &archive)
{
    m_extracting = true;
    auto *tar = new QProcess(this);
    tar->setProcessChannelMode(QProcess::MergedChannels);
    auto done = [this, tar, archive](const QString &problem) {
        tar->deleteLater();
        m_extracting = false;
        QFile::remove(archive);
        if (!problem.isEmpty()) {
            fail(tr("Cannot unpack ONNX Runtime: %1").arg(problem));
            return;
        }
        m_probed = false;
        fetchNext();
    };
    connect(tar, &QProcess::errorOccurred, this, [tar, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done(tar->errorString());
    });
    connect(tar, &QProcess::finished, this, [this, tar, done](int code, QProcess::ExitStatus status) {
        const bool unpacked = status == QProcess::NormalExit && code == 0
                              && QFile::exists(runtimeDir() + QLatin1Char('/') + QLatin1String(kRuntimeFile));
        done(unpacked ? QString() : QString::fromLocal8Bit(tar->readAll()).trimmed());
    });
    tar->start(QStringLiteral("tar"), {QStringLiteral("-xzf"), archive, QStringLiteral("-C"), runtimeDir(),
                                       QStringLiteral("--strip-components=2"), QLatin1String(kRuntimeMember)});
}

void BackgroundRemover::cancelDownload()
{
    m_queue.clear();
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
}

void BackgroundRemover::deleteDownloads()
{
    cancelDownload();
    m_idle.stop();
    if (m_rt)
        m_rt->dropIdle();
    QFile::remove(modelPath());
    QDir(runtimeDir()).removeRecursively();
    m_error.clear();
    emit stateChanged();
}

void BackgroundRemover::remove(const QImage &img, QObject *context, Done done)
{
    if (m_busy) {
        done({}, tr("Background removal is already running"));
        return;
    }
    if (!ready()) {
        done({}, tr("Background removal needs ONNX Runtime and its model - see Settings > Editor"));
        return;
    }
    const qint64 need = std::max(kRunMiB, kModelMiB + qint64(img.width()) * img.height() * kCutBytesPerPixel / (1024 * 1024));
    if (const qint64 free = availableMiB(); free >= 0 && free < need) {
        const QLocale loc;
        done({}, tr("Background removal needs about %1 GB of free memory and %2 GB is free - close something and try again")
                     .arg(loc.toString(need / 1024.0, 'f', 1), loc.toString(free / 1024.0, 'f', 1)));
        return;
    }
    m_busy = true;
    emit stateChanged();
    auto rt = m_rt;
    {
        std::lock_guard<std::mutex> lock(rt->mutex);
        rt->stop = false;
    }
    QPointer<QObject> ctx(context);
    auto stopOnClose = std::make_shared<QMetaObject::Connection>(
        connect(context, &QObject::destroyed, this, [rt] { rt->terminate(); }));
    auto *watcher = new QFutureWatcher<Cut>(this);
    connect(watcher, &QFutureWatcher<Cut>::finished, this, [this, watcher, ctx, stopOnClose, done] {
        disconnect(*stopOnClose);
        watcher->deleteLater();
        m_busy = false;
        if (modelInstalled())
            m_idle.start();
        else
            m_rt->dropIdle();
        emit stateChanged();
        const Cut r = watcher->result();
        if (!r.error.isEmpty())
            qWarning().noquote() << "background removal:" << r.error;
        if (ctx)
            done(r.image, r.error.isEmpty() ? QString() : tr("Background removal failed: %1").arg(r.error));
    });
    watcher->setFuture(QtConcurrent::run([rt, model = modelPath(), img] { return rt->cutOut(model, img); }));
}

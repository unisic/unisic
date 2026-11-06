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
#include <QtConcurrent>

#include <cmath>
#include <mutex>
#include <vector>

namespace {
// Both files are pinned twice: by an immutable URL (a release tag, a Hugging
// Face commit) and by size and SHA-256, so neither can change under a user.
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
    {"https://huggingface.co/onnx-community/BiRefNet_lite-ONNX/resolve/"
     "de15b22ba131738a16dff04aab8bdf8dc32e3ac1/onnx/model.onnx",
     224005088, "5600024376f572a557870a5eb0afb1e5961636bef4e1e22132025467d0f03333"},
};
constexpr const char *kRuntimeMember = "onnxruntime-linux-x64-1.31.1/lib/libonnxruntime.so.1.31.1";
constexpr const char *kRuntimeFile = "libonnxruntime.so.1.31.1";

// BiRefNet's contract (onnx-community export): a 1024 x 1024 RGB tensor with
// ImageNet normalization in, one channel of logits at the same size out.
constexpr int kSide = 1024;
constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};
// Peak RSS of one run, measured with ORT 1.31.1 on x86_64: the decoder's
// deformable convolution is exported as GatherND/Mul/Transpose over 784 MB
// tensors, and no session option (arena, memory pattern, optimization level,
// threads) brings it below 6.6 GB. Starting a run without that much free
// memory pushes the desktop into swap or the OOM killer.
constexpr qint64 kRunMiB = 6800;

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
} // namespace

// Process-lifetime once loaded: ORT does not survive being unloaded, and the
// env is never released for the same reason. A worker holds a reference, so
// a run in flight outlives the BackgroundRemover that started it.
struct BackgroundRemover::Runtime {
    QLibrary lib;
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;

    std::mutex mutex;              // guards running and stop
    OrtRunOptions *running = nullptr;
    bool stop = false;

    void terminate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
        if (running)
            if (OrtStatus *s = api->RunOptionsSetTerminate(running))
                api->ReleaseStatus(s);
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

    OrtSessionOptions *opts = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *mem = nullptr;
    OrtValue *in = nullptr;
    OrtValue *out = nullptr;
    OrtRunOptions *run = nullptr;
    const auto release = qScopeGuard([&] {
        if (out) a->ReleaseValue(out);
        if (in) a->ReleaseValue(in);
        if (run) a->ReleaseRunOptions(run);
        if (mem) a->ReleaseMemoryInfo(mem);
        if (session) a->ReleaseSession(session);
        if (opts) a->ReleaseSessionOptions(opts);
    });

    // No arena: its blocks would stay with the process after the run.
    bool built = ok(a->CreateSessionOptions(&opts))
                 && ok(a->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL))
                 && ok(a->DisableCpuMemArena(opts))
                 && ok(a->CreateSessionFromArray(env, bytes, size_t(file.size()), opts, &session));
    file.unmap(bytes);
    file.close();
    const int64_t shape[4] = {1, 3, kSide, kSide};
    built = built && ok(a->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &mem))
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
    const QImage alpha = small.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *px = reinterpret_cast<QRgb *>(img.scanLine(y));
        const uchar *m = alpha.constScanLine(y);
        for (int x = 0; x < img.width(); ++x)
            px[x] = (px[x] & 0x00ffffffu) | (uint((qAlpha(px[x]) * m[x] + 127) / 255) << 24);
    }
    return {std::move(img).convertToFormat(QImage::Format_ARGB32_Premultiplied), {}};
}

BackgroundRemover::BackgroundRemover(QObject *parent)
    : QObject(parent)
{
}

BackgroundRemover::~BackgroundRemover()
{
    // A run in flight would hold Unisic's exit for its remaining seconds.
    if (m_rt)
        m_rt->terminate();
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
        rt->api = base->GetApi(ORT_API_VERSION);
        if (!rt->api) {
            m_runtimeInfo = tr("ONNX Runtime %1 is too old, 1.17 or newer is needed").arg(ver);
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
    // Without a byte for a minute, not for the whole 214 MB.
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
    if (const qint64 free = availableMiB(); free >= 0 && free < kRunMiB) {
        const QLocale loc;
        done({}, tr("Background removal needs about %1 GB of free memory and %2 GB is free - close something and try again")
                     .arg(loc.toString(kRunMiB / 1024.0, 'f', 1), loc.toString(free / 1024.0, 'f', 1)));
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
        emit stateChanged();
        const Cut r = watcher->result();
        if (!r.error.isEmpty())
            qWarning().noquote() << "background removal:" << r.error;
        if (ctx)
            done(r.image, r.error.isEmpty() ? QString() : tr("Background removal failed: %1").arg(r.error));
    });
    watcher->setFuture(QtConcurrent::run([rt, model = modelPath(), img] { return rt->cutOut(model, img); }));
}

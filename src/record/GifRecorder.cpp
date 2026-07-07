#include "GifRecorder.h"
#include "Settings.h"
#include "capture/ScreenCastSession.h"
#ifdef HAVE_PIPEWIRE
#include "PipeWireGrabber.h"
#endif
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QDebug>
#include <cstring>

GifRecorder::GifRecorder(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_sampler.setTimerType(Qt::PreciseTimer);
    connect(&m_sampler, &QTimer::timeout, this, &GifRecorder::sampleFrame);
    m_maxTimer.setSingleShot(true);
    connect(&m_maxTimer, &QTimer::timeout, this, &GifRecorder::stop);
    m_elapsedTick.setInterval(250);
    connect(&m_elapsedTick, &QTimer::timeout, this, &GifRecorder::elapsedChanged);
}

GifRecorder::~GifRecorder()
{
    abort();
}

int GifRecorder::elapsedSeconds() const
{
    return m_elapsed.isValid() ? int(m_elapsed.elapsed() / 1000) : 0;
}

void GifRecorder::start(Output output, SourceType source, const QRect &cropPhysical, QScreen *screen)
{
#ifndef HAVE_PIPEWIRE
    Q_UNUSED(output) Q_UNUSED(source) Q_UNUSED(cropPhysical) Q_UNUSED(screen)
    emit failed(tr("Unisic was built without PipeWire support — recording unavailable"));
    return;
#else
    if (m_state != Idle)
        return;
    m_state = Starting;
    m_output = output;
    m_source = source;
    m_crop = (source == Region) ? cropPhysical : QRect();
    m_encodeCrop = {};
    m_encodeSize = {};
    m_targetScreen = screen;
    m_lastFrame.clear();

    m_session = new ScreenCastSession(this);
    connect(m_session, &ScreenCastSession::ready, this, &GifRecorder::onStreamReady);
    connect(m_session, &ScreenCastSession::failed, this, [this](const QString &e) { fail(e); });
    // Window source → portal WINDOW picker; otherwise a monitor.
    m_session->start(m_settings->includeCursor(), source == Window ? 2u : 1u);
#endif
}

void GifRecorder::onStreamReady(int fd, uint nodeId, const QSize &, const QPoint &)
{
#ifdef HAVE_PIPEWIRE
    m_grabber = new PipeWireGrabber(this);
    connect(m_grabber, &PipeWireGrabber::formatReady, this, &GifRecorder::beginEncoding);
    connect(m_grabber, &PipeWireGrabber::streamError, this, [this](const QString &e) { fail(e); });
    if (!m_grabber->start(fd, nodeId))
        fail(tr("Failed to connect to the PipeWire stream"));
#else
    Q_UNUSED(fd) Q_UNUSED(nodeId)
#endif
}

void GifRecorder::beginEncoding(const QSize &streamSize)
{
    if (m_state != Starting)
        return;
    if (!streamSize.isValid() || streamSize.width() < 2 || streamSize.height() < 2) {
        fail(tr("PipeWire returned an invalid stream size"));
        return;
    }
    m_streamSize = streamSize;
    QRect sourceRect(QPoint(0, 0), m_streamSize);
    m_encodeCrop = sourceRect;
    m_encodeSize = m_streamSize;

    if (m_source == Region) {
        QRect c = m_crop.normalized().intersected(sourceRect);
        c.setWidth(c.width() & ~1);
        c.setHeight(c.height() & ~1);
        if (c.width() < 2 || c.height() < 2) {
            fail(tr("Selected recording region is outside the chosen screen stream"));
            return;
        }
        if (m_targetScreen) {
            const qreal dpr = m_targetScreen->devicePixelRatio();
            const QSize expected(qRound(m_targetScreen->geometry().width() * dpr),
                                 qRound(m_targetScreen->geometry().height() * dpr));
            if (expected.isValid() && expected != m_streamSize) {
                qWarning() << "Recording stream size does not match selected screen"
                           << "stream" << m_streamSize << "screen" << expected
                           << "crop" << m_crop;
            }
        }
        m_encodeCrop = c;
        m_encodeSize = c.size();
    }

    const int fps = qBound(1, m_output == Gif ? m_settings->gifFps() : m_settings->videoFps(), 60);
    const QString ext = m_output == Gif ? QStringLiteral("gif")
                      : m_output == WebM ? QStringLiteral("webm")
                                         : QStringLiteral("mp4");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    QDir().mkpath(m_settings->saveDirectory());
    m_tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                 + QStringLiteral("/unisic-rec-%1.mkv").arg(stamp);
    m_outPath = m_settings->saveDirectory()
                + QStringLiteral("/Unisic_%1.%2").arg(stamp, ext);

    QStringList args{QStringLiteral("-y"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"),
                     QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                     QStringLiteral("-video_size"),
                     QStringLiteral("%1x%2").arg(m_encodeSize.width()).arg(m_encodeSize.height()),
                     QStringLiteral("-framerate"), QString::number(fps),
                     QStringLiteral("-i"), QStringLiteral("-")};

    args << QStringLiteral("-c:v") << QStringLiteral("libx264rgb")
         << QStringLiteral("-preset") << QStringLiteral("ultrafast")
         << QStringLiteral("-qp") << QStringLiteral("0")
         << m_tempPath;

    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_ffmpeg, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if ((m_state == Recording || m_state == Starting) && error == QProcess::FailedToStart)
            fail(tr("ffmpeg could not be started — is it installed?"));
    });
    connect(m_ffmpeg, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        auto *rec = m_ffmpeg;
        if (!rec)
            return;
        const QByteArray out = rec->readAll();
        m_ffmpeg = nullptr;
        rec->deleteLater();

        if (m_state == Recording || m_state == Starting) {
            if (!out.isEmpty())
                qWarning() << out;
            fail(status == QProcess::CrashExit
                     ? tr("Recording encoder crashed")
                     : tr("Recording encoder stopped unexpectedly (code %1)").arg(code));
            return;
        }
        if (m_state != Converting)
            return;
        if (code != 0 || status == QProcess::CrashExit) {
            if (!out.isEmpty())
                qWarning() << out;
            fail(tr("Recording encoder failed (code %1)").arg(code));
            return;
        }
        if (m_output == Gif)
            convertToGif();
        else
            convertVideo();
    });
    auto *encoder = m_ffmpeg;
    encoder->start(QStringLiteral("ffmpeg"), args);
    if (!encoder->waitForStarted(3000)) {
        // errorOccurred may already have fired fail() synchronously (state now
        // Idle); guard so we don't emit failed() twice.
        if (m_ffmpeg == encoder && m_state == Starting)
            fail(tr("ffmpeg could not be started — is it installed?"));
        return;
    }
    if (m_ffmpeg != encoder)
        return;

    m_state = Recording;
    m_elapsed.start();
    m_elapsedTick.start();
    m_sampler.start(1000 / fps);
    const int maxSec = m_output == Gif ? m_settings->gifMaxDurationSec()
                                       : m_settings->videoMaxDurationSec();
    if (maxSec > 0)
        m_maxTimer.start(maxSec * 1000);
    emit started();
}

void GifRecorder::sampleFrame()
{
#ifdef HAVE_PIPEWIRE
    if (m_state != Recording || !m_grabber || !m_ffmpeg)
        return;
    if (m_ffmpeg->bytesToWrite() > 64 * 1024 * 1024)
        return; // bounded buffer: drop this sample instead of aborting the recording
    QByteArray frame;
    if (!m_grabber->latestFrame(frame))
        return; // no frame yet (idle screen) — sample-and-hold will catch up
    // ffmpeg's rawvideo demuxer slices stdin into fixed m_streamSize frames. If
    // the source renegotiated a different size mid-recording (e.g. a window was
    // resized), feeding the new-size frame would desync every subsequent frame
    // into corruption — drop mismatched frames so the encoder holds the last
    // good one instead.
    const qsizetype expected = qsizetype(m_streamSize.width()) * m_streamSize.height() * 4;
    if (frame.size() != expected) {
        if (!m_lastFrame.isEmpty())
            m_ffmpeg->write(m_lastFrame);
        return;
    }

    QByteArray encoded;
    if (m_encodeCrop == QRect(QPoint(0, 0), m_streamSize)) {
        encoded = frame;
    } else {
        encoded.resize(qsizetype(m_encodeSize.width()) * m_encodeSize.height() * 4);
        const qsizetype srcStride = qsizetype(m_streamSize.width()) * 4;
        const qsizetype dstStride = qsizetype(m_encodeSize.width()) * 4;
        const char *src = frame.constData() + qsizetype(m_encodeCrop.y()) * srcStride
                          + qsizetype(m_encodeCrop.x()) * 4;
        char *dst = encoded.data();
        for (int y = 0; y < m_encodeSize.height(); ++y) {
            memcpy(dst, src, dstStride);
            src += srcStride;
            dst += dstStride;
        }
    }
    m_lastFrame = encoded;
    m_ffmpeg->write(encoded);
#endif
}

void GifRecorder::stop()
{
    if (m_state != Recording) {
        if (m_state == Starting)
            abort();
        return;
    }
    m_state = Converting;
    m_sampler.stop();
    m_maxTimer.stop();
    m_elapsedTick.stop();
#ifdef HAVE_PIPEWIRE
    if (m_grabber) {
        m_grabber->stop();
        m_grabber->deleteLater();
        m_grabber = nullptr;
    }
#endif
    delete m_session;
    m_session = nullptr;

    emit converting();
    if (!m_ffmpeg) {
        fail(tr("Recording encoder is not running"));
        return;
    }
    m_ffmpeg->closeWriteChannel(); // EOF -> ffmpeg finalizes the file
}

void GifRecorder::convertToGif()
{
    const int fps = qBound(1, m_settings->gifFps(), 60);
    // quality: 0 = fast/small, 1 = balanced, 2 = best
    const int q = qBound(0, m_settings->gifQuality(), 2);
    const QString dither = q == 0 ? QStringLiteral("bayer:bayer_scale=3")
                                  : (q == 1 ? QStringLiteral("bayer:bayer_scale=5")
                                            : QStringLiteral("sierra2_4a"));
    const QString statsMode = q == 2 ? QStringLiteral("diff") : QStringLiteral("full");
    const QString vf = QStringLiteral(
        "fps=%1,split[a][b];[a]palettegen=stats_mode=%2[p];[b][p]paletteuse=dither=%3:diff_mode=rectangle")
        .arg(fps).arg(statsMode, dither);

    auto *conv = new QProcess(this);
    m_converter = conv;
    conv->setProcessChannelMode(QProcess::MergedChannels);
    connect(conv, &QProcess::finished, this, [this, conv](int code, QProcess::ExitStatus) {
        if (m_converter != conv)
            return;
        const QByteArray out = conv->readAll();
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        if (code != 0) {
            qWarning() << out;
            fail(tr("GIF conversion failed"));
            return;
        }
        const QString path = m_outPath;
        cleanup();
        emit finished(path);
    });
    connect(conv, &QProcess::errorOccurred, this, [this, conv](QProcess::ProcessError error) {
        if (m_converter != conv || error != QProcess::FailedToStart)
            return;
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started — is it installed?"));
    });
    conv->start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-y"), QStringLiteral("-i"), m_tempPath,
                 QStringLiteral("-vf"), vf, m_outPath});
    if (!conv->waitForStarted(3000) && m_converter == conv) {
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started — is it installed?"));
    }
}

void GifRecorder::convertVideo()
{
    // Re-encode the lossless temp into a shareable MP4 (H.264) or WebM (VP9).
    const int crf = qBound(0, m_settings->videoQuality(), 51);
    QStringList args{QStringLiteral("-y"), QStringLiteral("-i"), m_tempPath};
    if (m_output == WebM) {
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9")
             << QStringLiteral("-crf") << QString::number(crf)
             << QStringLiteral("-b:v") << QStringLiteral("0")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-row-mt") << QStringLiteral("1");
    } else {
        args << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("veryfast")
             << QStringLiteral("-crf") << QString::number(crf)
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    args << m_outPath;

    auto *conv = new QProcess(this);
    m_converter = conv;
    conv->setProcessChannelMode(QProcess::MergedChannels);
    connect(conv, &QProcess::finished, this, [this, conv](int c, QProcess::ExitStatus) {
        if (m_converter != conv)
            return;
        const QByteArray out = conv->readAll();
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        if (c != 0) {
            qWarning() << out;
            fail(tr("Video conversion failed"));
            return;
        }
        const QString path = m_outPath;
        cleanup();
        emit finished(path);
    });
    connect(conv, &QProcess::errorOccurred, this, [this, conv](QProcess::ProcessError error) {
        if (m_converter != conv || error != QProcess::FailedToStart)
            return;
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started — is it installed?"));
    });
    conv->start(QStringLiteral("ffmpeg"), args);
    if (!conv->waitForStarted(3000) && m_converter == conv) {
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started — is it installed?"));
    }
}

void GifRecorder::stopProcess(QProcess *&process)
{
    if (!process)
        return;
    QProcess *p = process;
    process = nullptr;
    QObject::disconnect(p, nullptr, this, nullptr);
    if (p->state() != QProcess::NotRunning) {
        p->closeWriteChannel();
        p->terminate();
        if (!p->waitForFinished(1000)) {
            p->kill();
            p->waitForFinished(3000);
        }
    }
    p->deleteLater();
}

void GifRecorder::abort()
{
    m_sampler.stop();
    m_maxTimer.stop();
    m_elapsedTick.stop();
#ifdef HAVE_PIPEWIRE
    if (m_grabber) {
        m_grabber->stop();
        m_grabber->deleteLater();
        m_grabber = nullptr;
    }
#endif
    delete m_session;
    m_session = nullptr;
    stopProcess(m_ffmpeg);
    stopProcess(m_converter);
    if (!m_tempPath.isEmpty())
        QFile::remove(m_tempPath);
    cleanup();
}

void GifRecorder::cleanup()
{
    m_state = Idle;
    m_tempPath.clear();
    m_encodeCrop = {};
    m_encodeSize = {};
    m_targetScreen = nullptr;
    m_lastFrame.clear();
    m_elapsed.invalidate();
    emit elapsedChanged();
}

void GifRecorder::fail(const QString &msg)
{
    qWarning() << "GifRecorder:" << msg;
    abort();
    emit failed(msg);
}

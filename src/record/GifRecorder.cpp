#include "GifRecorder.h"
#include "Settings.h"
#include "capture/ScreenCastSession.h"
#ifdef HAVE_PIPEWIRE
#include "PipeWireGrabber.h"
#endif
#include <QStandardPaths>
#include <QSettings>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QSet>
#include <QDebug>
#include <QtConcurrent>
#include <cstring>

// The ffmpeg found in PATH varies: the Flatpak KDE runtime ships one without
// GPL x264. Probe the available video encoders once so both the lossless
// intermediate and the MP4 output can pick a working fallback. An empty set
// means the probe itself failed (no ffmpeg) — callers keep their preferred
// encoder and the existing "ffmpeg could not be started" path reports it.
static QSet<QString> probeFfmpegEncoders()
{
    QSet<QString> found;
    QProcess p;
    p.start(QStringLiteral("ffmpeg"),
            {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    if (p.waitForFinished(5000)) {
        const QList<QByteArray> lines = p.readAllStandardOutput().split('\n');
        for (const QByteArray &line : lines) {
            // " V....D libx264rgb   libx264 H.264 ... (codec h264)"
            // (skip the legend line " V..... = Video")
            const QList<QByteArray> cols = line.simplified().split(' ');
            if (cols.size() >= 2 && cols[0].startsWith('V') && cols[1] != "=")
                found.insert(QString::fromLatin1(cols[1]));
        }
    }
    return found;
}

// Magic-static: the first caller runs the probe, later callers get the cache,
// concurrent callers block until the probe finishes — which makes the warm-up
// from a worker thread in the constructor safe.
static const QSet<QString> &ffmpegEncoders()
{
    static const QSet<QString> cached = probeFfmpegEncoders();
    return cached;
}

GifRecorder::GifRecorder(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    // The lossless intermediates live in ~/.cache/unisic (disk-backed — see
    // beginEncoding), which unlike /tmp is never reclaimed by a reboot: sweep
    // recordings orphaned by a crash/SIGKILL (multi-GB each) once at startup.
    // The recorder is constructed before any recording can start, so nothing
    // live can be swept.
    QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    const QStringList stale = cache.entryList({QStringLiteral("unisic-rec-*")}, QDir::Files);
    for (const QString &f : stale)
        cache.remove(f);

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

static QString restoreTokenKey(GifRecorder::SourceType source)
{
    return source == GifRecorder::Window
               ? QStringLiteral("record/portalRestoreTokenWindow")
               : QStringLiteral("record/portalRestoreTokenMonitor");
}

int GifRecorder::elapsedSeconds() const
{
    return m_elapsed.isValid() ? int(m_elapsed.elapsed() / 1000) : 0;
}

void GifRecorder::start(Output output, SourceType source, const QRect &cropPhysical, QScreen *screen)
{
#ifndef HAVE_PIPEWIRE
    Q_UNUSED(output) Q_UNUSED(source) Q_UNUSED(cropPhysical) Q_UNUSED(screen)
    emit failed(tr("Unisic was built without PipeWire support, so recording is unavailable"));
    return;
#else
    if (m_state != Idle)
        return;
    // Warm the ffmpeg encoder probe off the GUI thread while the portal
    // dialog / stream negotiation runs — without this the first beginEncoding
    // blocks the UI for up to 5 s on the synchronous "ffmpeg -encoders"
    // round-trip. Done here, not in the constructor: one-shot CLI invocations
    // (--export-settings etc.) construct the recorder without ever recording,
    // and the global pool would hold process exit until the probe finished.
    if (!m_probeWarmed) {
        m_probeWarmed = true;
        (void)QtConcurrent::run([] { (void)ffmpegEncoders(); });
    }
    m_state = Starting;
    m_output = output;
    m_source = source;
    m_crop = (source == Region) ? cropPhysical : QRect();
    m_encodeCrop = {};
    m_encodeSize = {};
    m_targetScreen = screen;
    m_lastFrame.clear();
    m_lastSampledSeq = 0;

    m_session = new ScreenCastSession(this);
    connect(m_session, &ScreenCastSession::ready, this, &GifRecorder::onStreamReady);
    connect(m_session, &ScreenCastSession::failed, this, [this](const QString &e) { fail(e); });
    connect(m_session, &ScreenCastSession::sessionClosed, this, [this] {
        // Sharing stopped from the system UI: finalize what we have.
        if (m_state == Recording)
            stop();
        else if (m_state == Starting)
            fail(tr("Screen sharing was stopped"));
    });
    connect(m_session, &ScreenCastSession::restoreTokenChanged, this, [this, source](const QString &token) {
        const QString key = restoreTokenKey(source);
        if (token.isEmpty())
            m_settings->raw()->remove(key);
        else
            m_settings->raw()->setValue(key, token);
    });
    // Window source → portal WINDOW picker; otherwise a monitor.
    const QString restoreToken = m_settings->raw()->value(restoreTokenKey(source)).toString();
    m_session->start(m_settings->includeCursor(), source == Window ? 2u : 1u, restoreToken);
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
    QRect c = sourceRect;

    if (m_source == Region) {
        QRect crop = m_crop.normalized();
        if (m_targetScreen) {
            const qreal dpr = m_targetScreen->devicePixelRatio();
            const QSize expected(qRound(m_targetScreen->geometry().width() * dpr),
                                 qRound(m_targetScreen->geometry().height() * dpr));
            if (!expected.isEmpty() && expected != m_streamSize) {
                // The portal stream is the monitor at a DIFFERENT pixel size
                // than the overlay assumed (fractional scaling on GNOME, or a
                // logically-sized stream). Rescale the crop by the ratio
                // instead of recording a misplaced region.
                const qreal sx = qreal(m_streamSize.width()) / expected.width();
                const qreal sy = qreal(m_streamSize.height()) / expected.height();
                qWarning() << "Recording stream size differs from the selected screen —"
                           << "rescaling crop" << crop << "by" << sx << sy;
                crop = QRect(qRound(crop.x() * sx), qRound(crop.y() * sy),
                             qRound(crop.width() * sx), qRound(crop.height() * sy));
            }
        }
        c = crop.intersected(sourceRect);
    }

    // yuv420p (MP4/WebM) requires even dimensions — enforce for every source,
    // not just Region: window streams are frequently odd-sized.
    c.setWidth(c.width() & ~1);
    c.setHeight(c.height() & ~1);
    if (c.width() < 2 || c.height() < 2) {
        fail(m_source == Region
                 ? tr("Selected recording region is outside the chosen screen stream")
                 : tr("Recording stream is too small"));
        return;
    }
    m_encodeCrop = c;
    m_encodeSize = c.size();

    const int fps = qBound(1, m_output == Gif ? m_settings->gifFps() : m_settings->videoFps(), 60);
    m_fps = fps;
    m_framesWritten = 0;
    const QString ext = m_output == Gif ? QStringLiteral("gif")
                      : m_output == WebM ? QStringLiteral("webm")
                                         : QStringLiteral("mp4");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    QDir().mkpath(m_settings->saveDirectory());
    // Lossless intermediate goes to disk-backed XDG cache, NOT TempLocation:
    // /tmp is tmpfs on Fedora and in Flatpak, and minutes of lossless 4K would
    // exhaust RAM (max duration defaults to unlimited) and lose the recording
    // when ffmpeg's write hits ENOSPC.
    const QString tmpBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(tmpBase);
    m_tempPath = tmpBase + QStringLiteral("/unisic-rec-%1.mkv").arg(stamp);
    m_outPath = m_settings->saveDirectory()
                + QStringLiteral("/Unisic_%1.%2").arg(stamp, ext);

    // -nostats: with MergedChannels the progress spam would otherwise grow
    // unbounded in the QProcess read buffer for the whole recording.
    QStringList args{QStringLiteral("-y"),
                     QStringLiteral("-nostats"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"),
                     QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                     QStringLiteral("-video_size"),
                     QStringLiteral("%1x%2").arg(m_encodeSize.width()).arg(m_encodeSize.height()),
                     QStringLiteral("-framerate"), QString::number(fps),
                     QStringLiteral("-thread_queue_size"), QStringLiteral("512"),
                     QStringLiteral("-i"), QStringLiteral("-")};

    // Optional audio — video output only (GIF has none). Each enabled source is
    // a live pulse capture indexed after the video (input 0): @DEFAULT_MONITOR@
    // is the default sink's monitor (system sound), "default" is the mic.
    m_hasAudio = false;
    QStringList audioSources;
    if (m_output != Gif) {
        if (m_settings->recordSystemAudio())
            audioSources << QStringLiteral("@DEFAULT_MONITOR@");
        if (m_settings->recordMicrophone())
            audioSources << QStringLiteral("default");
    }
    for (const QString &dev : audioSources) {
        args << QStringLiteral("-f") << QStringLiteral("pulse")
             << QStringLiteral("-thread_queue_size") << QStringLiteral("1024")
             << QStringLiteral("-i") << dev;
    }

    // Lossless RGB intermediate: libx264rgb (fastest) when the ffmpeg has GPL
    // x264, else utvideo (fast intra-only RGB), else FFV1 — both ship in the
    // Flatpak KDE runtime's ffmpeg.
    const QSet<QString> &encoders = ffmpegEncoders();
    if (encoders.contains(QStringLiteral("libx264rgb")) || encoders.isEmpty()) {
        args << QStringLiteral("-c:v") << QStringLiteral("libx264rgb")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-qp") << QStringLiteral("0");
    } else if (encoders.contains(QStringLiteral("utvideo"))) {
        args << QStringLiteral("-c:v") << QStringLiteral("utvideo");
    } else {
        args << QStringLiteral("-c:v") << QStringLiteral("ffv1");
    }

    // Audio mux: one source maps straight through, two are mixed. Stored as
    // lossless FLAC in the intermediate (convertVideo re-encodes to AAC/Opus).
    // -shortest ends the file when the video (pipe) stops so the live pulse
    // captures — which never EOF on their own — don't hang the encoder.
    if (audioSources.size() == 1) {
        args << QStringLiteral("-map") << QStringLiteral("0:v")
             << QStringLiteral("-map") << QStringLiteral("1:a")
             << QStringLiteral("-c:a") << QStringLiteral("flac")
             << QStringLiteral("-shortest");
        m_hasAudio = true;
    } else if (audioSources.size() >= 2) {
        args << QStringLiteral("-filter_complex")
             << QStringLiteral("[1:a][2:a]amix=inputs=2:duration=longest:normalize=0[aout]")
             << QStringLiteral("-map") << QStringLiteral("0:v")
             << QStringLiteral("-map") << QStringLiteral("[aout]")
             << QStringLiteral("-c:a") << QStringLiteral("flac")
             << QStringLiteral("-shortest");
        m_hasAudio = true;
    }
    args << m_tempPath;

    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_ffmpeg, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if ((m_state == Recording || m_state == Starting) && error == QProcess::FailedToStart)
            fail(tr("ffmpeg could not be started. Is it installed?"));
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
            fail(tr("ffmpeg could not be started. Is it installed?"));
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
    // Bounded buffer: drop this sample instead of aborting the recording.
    // Scale with the frame size — a fixed 64 MB is only ~2 frames at 4K BGRA,
    // so a momentary encoder stall would drop samples immediately.
    const qsizetype frameBytes = qsizetype(m_encodeSize.width()) * m_encodeSize.height() * 4;
    const qsizetype writeCap = qMax<qsizetype>(64 * 1024 * 1024, frameBytes * 30);
    if (m_ffmpeg->bytesToWrite() > writeCap)
        return;
    // ffmpeg's rawvideo demuxer slices stdin into fixed m_streamSize frames. If
    // the source renegotiated a different size mid-recording (e.g. a window was
    // resized), feeding the new-size frame would desync every subsequent frame
    // into corruption — hold the last good frame instead.
    const qsizetype expected = qsizetype(m_streamSize.width()) * m_streamSize.height() * 4;
    QByteArray frame;
    QByteArray encoded;
    quint64 seq = 0;
    if (m_grabber->latestFrame(frame, &seq) && frame.size() == expected) {
        if (seq == m_lastSampledSeq && !m_lastFrame.isEmpty()) {
            // Compositor streams are damage-driven: on a static screen no new
            // frame arrives, and re-cropping the identical buffer every tick
            // (alloc + row-by-row memcpy at the sample rate) produced byte-
            // identical output. Reuse the previous sample.
            encoded = m_lastFrame;
        } else {
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
            m_lastSampledSeq = seq;
        }
    } else {
        encoded = m_lastFrame; // no frame yet / renegotiated size: sample-and-hold
    }
    if (encoded.isEmpty())
        return;

    // Wall-clock pacing: the timer interval truncates (1000/30 = 33 ms →
    // 30.3 fps) and backpressure drops ticks, while the container claims an
    // exact -framerate — pace by elapsed time, duplicating frames as needed,
    // or playback speed drifts from real time.
    const qint64 target = m_elapsed.elapsed() * m_fps / 1000 + 1;
    if (target <= m_framesWritten)
        return; // ahead of schedule
    const qint64 n = qMin<qint64>(target - m_framesWritten, m_fps); // ≤1 s burst
    for (qint64 i = 0; i < n; ++i)
        m_ffmpeg->write(encoded);
    m_framesWritten += n;
#endif
}

void GifRecorder::stop()
{
    if (m_state != Recording) {
        // fail() (not bare abort()): AppContext must get a signal so the QML
        // recording state refreshes; "cancelled" is filtered from toasts.
        if (m_state == Starting)
            fail(QStringLiteral("cancelled"));
        return;
    }
    m_state = Converting;
    m_sampler.stop();
    m_maxTimer.stop();
    m_elapsedTick.stop();
    emit converting();
#ifdef HAVE_PIPEWIRE
    if (m_grabber) {
        // Detach and stop on a worker thread: pw_thread_loop_stop JOINS the
        // capture thread, and doing that on the GUI thread blocks the very
        // repaint that should show the "Encoding…" state. Once detached,
        // abort()/fail()/the destructor see m_grabber == nullptr and cannot
        // double-stop it; only the worker touches the object until its
        // deleteLater lands back on the GUI thread.
        PipeWireGrabber *g = m_grabber;
        m_grabber = nullptr;
        g->disconnect(this);
        // Un-parent, or ~GifRecorder at app quit deletes the child on the GUI
        // thread while the worker is still inside g->stop() (use-after-free).
        g->setParent(nullptr);
        (void)QtConcurrent::run([g] {
            g->stop();
            QMetaObject::invokeMethod(g, "deleteLater", Qt::QueuedConnection);
        });
    }
#endif
    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
        m_session = nullptr;
    }

    if (!m_ffmpeg) {
        fail(tr("Recording encoder is not running"));
        return;
    }
    m_ffmpeg->closeWriteChannel(); // EOF -> ffmpeg finalizes the file
    m_lastFrame.clear(); // don't pin a full raw frame through the whole conversion
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

    // True two-pass. A single split-graph command (`split[a][b];[a]palettegen…`)
    // buffers every decoded [b] frame in RAM until palettegen hits EOF — ~3 GB
    // for 30 s of 1080p, >10 GB for 4K — driving the machine into swap. The
    // temp file is on disk anyway; decoding the lossless intermediate twice
    // is cheap by comparison.
    m_palettePath = m_tempPath + QStringLiteral(".palette.png");
    const QString vf = QStringLiteral("fps=%1,palettegen=stats_mode=%2").arg(fps).arg(statsMode);

    auto *conv = new QProcess(this);
    m_converter = conv;
    conv->setProcessChannelMode(QProcess::MergedChannels);
    connect(conv, &QProcess::finished, this, [this, conv, fps, dither](int code, QProcess::ExitStatus) {
        if (m_converter != conv)
            return;
        const QByteArray out = conv->readAll();
        m_converter = nullptr;
        conv->deleteLater();
        if (code != 0) {
            qWarning() << out;
            QFile::remove(m_tempPath);
            QFile::remove(m_palettePath);
            fail(tr("GIF conversion failed"));
            return;
        }
        convertToGifRender(fps, dither);
    });
    connect(conv, &QProcess::errorOccurred, this, [this, conv](QProcess::ProcessError error) {
        if (m_converter != conv || error != QProcess::FailedToStart)
            return;
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    });
    conv->start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-y"), QStringLiteral("-nostats"),
                 QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-i"), m_tempPath,
                 QStringLiteral("-vf"), vf, m_palettePath});
    if (!conv->waitForStarted(3000) && m_converter == conv) {
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    }
}

void GifRecorder::convertToGifRender(int fps, const QString &dither)
{
    const QString lavfi = QStringLiteral("fps=%1[x];[x][1:v]paletteuse=dither=%2:diff_mode=rectangle")
        .arg(fps).arg(dither);

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
        QFile::remove(m_palettePath);
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
        QFile::remove(m_palettePath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    });
    conv->start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-y"), QStringLiteral("-nostats"),
                 QStringLiteral("-loglevel"), QStringLiteral("error"),
                 QStringLiteral("-i"), m_tempPath,
                 QStringLiteral("-i"), m_palettePath,
                 QStringLiteral("-lavfi"), lavfi, m_outPath});
    if (!conv->waitForStarted(3000) && m_converter == conv) {
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        QFile::remove(m_palettePath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    }
}

void GifRecorder::convertVideo()
{
    // Re-encode the lossless temp into a shareable MP4 (H.264) or WebM (VP9).
    const int crf = qBound(0, m_settings->videoQuality(), 51);
    QStringList args{QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-i"), m_tempPath};
    if (m_output == WebM) {
        // Without -deadline/-cpu-used libvpx-vp9 defaults to good/cpu-used 0,
        // i.e. 0.1–0.3× realtime — minutes of conversion for a 1-minute clip.
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9")
             << QStringLiteral("-crf") << QString::number(crf)
             << QStringLiteral("-b:v") << QStringLiteral("0")
             << QStringLiteral("-deadline") << QStringLiteral("good")
             << QStringLiteral("-cpu-used") << QStringLiteral("4")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-row-mt") << QStringLiteral("1");
    } else if (ffmpegEncoders().contains(QStringLiteral("libx264"))
               || ffmpegEncoders().isEmpty()) {
        args << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("veryfast")
             << QStringLiteral("-crf") << QString::number(crf)
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    } else {
        // No GPL x264 (Flatpak KDE runtime): OpenH264. It has no CRF mode, so
        // approximate the quality setting with a bitrate.
        const int mbps = qBound(2, (51 - crf) / 3, 16);
        args << QStringLiteral("-c:v") << QStringLiteral("libopenh264")
             << QStringLiteral("-b:v") << QStringLiteral("%1M").arg(mbps)
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    // Carry the FLAC intermediate audio into the shareable container: Opus for
    // WebM, AAC for MP4. The '?' keeps it a no-op if no audio was recorded.
    if (m_hasAudio) {
        args << QStringLiteral("-map") << QStringLiteral("0:v:0")
             << QStringLiteral("-map") << QStringLiteral("0:a:0?");
        if (m_output == WebM)
            args << QStringLiteral("-c:a") << QStringLiteral("libopus")
                 << QStringLiteral("-b:a") << QStringLiteral("128k");
        else
            args << QStringLiteral("-c:a") << QStringLiteral("aac")
                 << QStringLiteral("-b:a") << QStringLiteral("192k");
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
        fail(tr("ffmpeg could not be started. Is it installed?"));
    });
    conv->start(QStringLiteral("ffmpeg"), args);
    if (!conv->waitForStarted(3000) && m_converter == conv) {
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    }
}

void GifRecorder::stopProcess(QProcess *&process)
{
    if (!process)
        return;
    QProcess *p = process;
    process = nullptr;
    QObject::disconnect(p, nullptr, this, nullptr);
    if (p->state() == QProcess::NotRunning) {
        p->deleteLater();
        return;
    }
    // Non-blocking escalation: the old terminate + waitForFinished(1000) +
    // kill + waitForFinished(3000) froze the GUI for up to 4 s per process on
    // every cancel/failure (ffmpeg can be slow to flush after SIGTERM). The
    // detached process reaps itself via finished -> deleteLater; removing the
    // temp files right after stays correct on Linux (ffmpeg keeps writing to
    // the unlinked inode, the space is reclaimed when it exits). The singleShot
    // is parented to p, so it auto-cancels if the process dies sooner.
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    p->closeWriteChannel();
    p->terminate();
    QTimer::singleShot(1000, p, [p] {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
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
    // fail() (and thus abort()) can run inside a ScreenCastSession signal
    // emission — deleting the sender there is UB, so defer.
    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
        m_session = nullptr;
    }
    stopProcess(m_ffmpeg);
    stopProcess(m_converter);
    if (!m_tempPath.isEmpty())
        QFile::remove(m_tempPath);
    if (!m_palettePath.isEmpty())
        QFile::remove(m_palettePath);
    // Aborting mid-conversion leaves a truncated file in the save directory.
    if (m_state == Converting && !m_outPath.isEmpty())
        QFile::remove(m_outPath);
    cleanup();
}

void GifRecorder::cleanup()
{
    m_state = Idle;
    m_tempPath.clear();
    m_palettePath.clear();
    m_outPath.clear();
    m_encodeCrop = {};
    m_encodeSize = {};
    m_targetScreen = nullptr;
    m_hasAudio = false;
    m_lastFrame.clear();
    m_elapsed.invalidate();
    emit elapsedChanged();
}

void GifRecorder::fail(const QString &msg)
{
    // Single-fire: a streamError/formatReady queued from the PipeWire thread
    // just before abort() is still delivered after it (disconnect/deleteLater
    // don't cancel already-posted queued calls) — without this guard a cancel
    // during Starting could re-enter here and emit a second failed().
    if (m_state == Idle)
        return;
    qWarning() << "GifRecorder:" << msg;
    abort();
    emit failed(msg);
}

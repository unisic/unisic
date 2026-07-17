#include "GifRecorder.h"
#include "ClickCapture.h"
#include "InputPermission.h"
#include <QPainter>
#include <ctime>
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
#include <QSaveFile>
#include <QUuid>
#include <QScreen>
#include <QSet>
#include <QDebug>
#include <QtConcurrent>
#include <climits>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <sys/stat.h>

// The ffmpeg found in PATH varies: some builds ship one without GPL
// x264. Probe the available video encoders once so both the lossless
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

bool GifRecorder::encoderUsable(const QString &name)
{
    return ffmpegEncoders().contains(name) || ffmpegEncoders().isEmpty();
}

QString GifRecorder::gifPaletteGenFilter(int quality)
{
    const int q = qBound(0, quality, 2);
    return QStringLiteral("palettegen=stats_mode=%1")
        .arg(q == 2 ? QStringLiteral("diff") : QStringLiteral("full"));
}

QString GifRecorder::gifPaletteUseFilter(int quality)
{
    const int q = qBound(0, quality, 2);
    const QString dither = q == 0 ? QStringLiteral("bayer:bayer_scale=3")
                                  : (q == 1 ? QStringLiteral("bayer:bayer_scale=5")
                                            : QStringLiteral("sierra2_4a"));
    return QStringLiteral("paletteuse=dither=%1:diff_mode=rectangle").arg(dither);
}

bool GifRecorder::hardwareEncoderAvailable(const QString &id)
{
    if (id == QLatin1String("vaapi"))
        return ffmpegEncoders().contains(QStringLiteral("h264_vaapi"))
               && QFileInfo::exists(QStringLiteral("/dev/dri/renderD128"));
    if (id == QLatin1String("nvenc"))
        return ffmpegEncoders().contains(QStringLiteral("h264_nvenc"));
    return false;
}

// Does the encoder actually ENCODE, not just appear in -encoders? Measured
// necessity, not caution: on this developer's box ffmpeg lists vp9_vaapi and
// the render node exists, yet the encode fails outright — the listing describes
// the ffmpeg build, the hardware behind it may not implement the codec. The
// "auto" default rides on this, so a listing-only check would hand a broken
// encoder to every user who never picked one.
//
// Encodes two frames of a tiny synthetic clip to /dev/null. ~0.5 s, run at most
// once per encoder per process.
bool GifRecorder::hardwareEncoderWorks(const QString &id)
{
    static QHash<QString, bool> cache;
    const auto it = cache.constFind(id);
    if (it != cache.constEnd())
        return *it;
    if (!hardwareEncoderAvailable(id)) {
        cache.insert(id, false);
        return false;
    }
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                     QStringLiteral("error"), QStringLiteral("-y")};
    if (id == QLatin1String("vaapi")) {
        args << QStringLiteral("-vaapi_device") << QStringLiteral("/dev/dri/renderD128")
             << QStringLiteral("-f") << QStringLiteral("lavfi")
             << QStringLiteral("-i") << QStringLiteral("testsrc2=size=320x240:rate=30:duration=0.1")
             << QStringLiteral("-vf") << QStringLiteral("format=nv12,hwupload")
             << QStringLiteral("-c:v") << QStringLiteral("h264_vaapi");
    } else {
        args << QStringLiteral("-f") << QStringLiteral("lavfi")
             << QStringLiteral("-i") << QStringLiteral("testsrc2=size=320x240:rate=30:duration=0.1")
             << QStringLiteral("-c:v") << QStringLiteral("h264_nvenc")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    }
    args << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(QStringLiteral("ffmpeg"), args);
    const bool ok = probe.waitForFinished(8000) && probe.exitStatus() == QProcess::NormalExit
                    && probe.exitCode() == 0;
    if (!ok) {
        // Log WHY: "works=n" alone is undiagnosable in the field (session limits,
        // driver mismatch, a missing device node all look identical without it).
        const QString why = QString::fromUtf8(probe.readAll()).right(400).trimmed();
        qInfo().noquote() << "GifRecorder: hardware encoder" << id
                          << "is listed but does not encode here"
                          << (why.isEmpty() ? QString() : QStringLiteral("— %1").arg(why));
    }
    cache.insert(id, ok);
    return ok;
}

// "auto" (the default) picks a hardware encoder that really works, else falls
// back to software. An explicit choice is honoured as-is — including software.
QString GifRecorder::resolvedVideoEncoder() const
{
    const QString choice = m_settings->videoEncoder();
    if (choice != QLatin1String("auto"))
        return choice;
    if (hardwareEncoderWorks(QStringLiteral("nvenc")))
        return QStringLiteral("nvenc");
    if (hardwareEncoderWorks(QStringLiteral("vaapi")))
        return QStringLiteral("vaapi");
    return QStringLiteral("software");
}

GifRecorder::GifRecorder(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    m_sampler.setTimerType(Qt::PreciseTimer);
    connect(&m_sampler, &QTimer::timeout, this, &GifRecorder::sampleFrame);
    m_maxTimer.setSingleShot(true);
    connect(&m_maxTimer, &QTimer::timeout, this, &GifRecorder::stop);
    // 1 s — the elapsed clock displays whole seconds (sidebar pill, REC
    // badge), so ticking 4x/s only woke the GUI thread three extra times per
    // second for identical text, recording-long, even with the window hidden.
    m_elapsedTick.setInterval(1000);
    m_elapsedTick.setTimerType(Qt::CoarseTimer);
    connect(&m_elapsedTick, &QTimer::timeout, this, &GifRecorder::elapsedChanged);
}

GifRecorder::~GifRecorder()
{
    abort();
}

static QString restoreTokenKey(GifRecorder::SourceType source, QScreen *screen)
{
    if (source == GifRecorder::Window)
        return QStringLiteral("record/portalRestoreTokenWindow");
    // Region: per-monitor token. A restore token silently replays the monitor
    // it was created with — reusing one recorded on another screen streams the
    // WRONG monitor and the region crop lands elsewhere (dual-monitor bug).
    if (source == GifRecorder::Region && screen && !screen->name().isEmpty())
        return QStringLiteral("record/portalRestoreTokenMonitor@") + screen->name();
    return QStringLiteral("record/portalRestoreTokenMonitor");
}

int GifRecorder::elapsedSeconds() const
{
    if (!m_elapsed.isValid())
        return 0;
    // The readout shows RECORDED time, not wall-clock: it freezes at the moment
    // of a pause and excludes every completed pause, matching the final file
    // (which has the paused spans excised).
    const qint64 base = m_paused ? m_pauseStartMs : m_elapsed.elapsed();
    return int(qMax<qint64>(0, base - m_pausedTotalMs) / 1000);
}

void GifRecorder::togglePause()
{
    if (m_paused) {
        if (m_state != Recording)
            return;
        const qint64 now = m_elapsed.elapsed();
        m_pauseIntervals.append({m_pauseStartMs, now});
        m_pausedTotalMs += now - m_pauseStartMs;
        m_paused = false;
        m_elapsedTick.start();
        if (m_maxRemainingMs > 0) {
            m_maxTimer.start(int(m_maxRemainingMs));
            m_maxRemainingMs = 0;
        }
        emit pausedChanged();
        emit elapsedChanged();
    } else {
        if (!canPause())
            return;
        m_paused = true;
        m_pauseStartMs = m_elapsed.elapsed();
        // Freeze the recorded-time readout and hold the max-duration budget so a
        // long pause can't trip the auto-stop.
        m_elapsedTick.stop();
        if (m_maxTimer.isActive()) {
            // remainingTime() truncates to int ms and reads 0 for an overdue-but-
            // undispatched timer; clamp so resume still re-arms (and immediately
            // trips the already-exhausted budget) instead of losing the auto-stop.
            m_maxRemainingMs = qMax(1, m_maxTimer.remainingTime());
            m_maxTimer.stop();
        }
        emit pausedChanged();
        emit elapsedChanged();
    }
}

void GifRecorder::start(Output output, SourceType source, const QRect &cropPhysical, QScreen *screen,
                        bool holdForCommit)
{
#ifndef HAVE_PIPEWIRE
    Q_UNUSED(output) Q_UNUSED(source) Q_UNUSED(cropPhysical) Q_UNUSED(screen) Q_UNUSED(holdForCommit)
    emit failed(tr("Unisic was built without PipeWire support, so recording is unavailable"));
    return;
#else
    if (m_state != Idle)
        return;
    // Sweep temp intermediates orphaned by a crash/SIGKILL (multi-GB each in
    // ~/.cache/unisic) once, here rather than in the constructor: one-shot CLI
    // invocations (--export-settings etc.) construct a recorder but never
    // record, and sweeping there would unlink the *live* temp of a concurrent
    // recording instance that shares the cache dir.
    if (!m_orphansSwept) {
        m_orphansSwept = true;
        QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
        const QStringList stale = cache.entryList({QStringLiteral("unisic-rec-*")}, QDir::Files);
        for (const QString &f : stale)
            cache.remove(f);
    }
    // Warm the ffmpeg encoder probe off the GUI thread while the portal
    // dialog / stream negotiation runs — without this the first beginEncoding
    // blocks the UI for up to 5 s on the synchronous "ffmpeg -encoders"
    // round-trip. Keep the watcher so beginEncoding can gate on it (a portal
    // restore token can beat the warm-up) instead of touching the blocking
    // magic-static on the GUI thread. One-shot CLI invocations never reach
    // start(), so the global pool never holds process exit for the probe.
    if (!m_probeWarmed) {
        m_probeWarmed = true;
        m_probeWatcher.setFuture(QtConcurrent::run([] { (void)ffmpegEncoders(); }));
    }
    m_state = Starting;
    m_holdForCommit = holdForCommit;
    m_armed = false;
    m_committed = false;
    m_heldStreamSize = QSize();
    m_output = output;
    m_source = source;
    m_crop = (source == Region) ? cropPhysical : QRect();
    m_encodeCrop = {};
    m_encodeSize = {};
    m_targetScreen = screen;
    m_lastFrame.clear();
    m_lastSampledSeq = 0;
    m_monitorRetryDone = false;

    openPortalSession();
#endif
}

// CLOCK_MONOTONIC now, in ns — the one clock shared by PipeWire frame pts,
// CursorSample::tMonoNs and libinput's click timestamps.
static qint64 monoNowNs()
{
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

QByteArray GifRecorder::compositeCursorOverlay(const QByteArray &encoded, qint64 nowNs)
{
#ifdef HAVE_PIPEWIRE
    if (!m_cursorOverlayActive || encoded.isEmpty() || !m_cursorOverlay.hasContent(nowNs))
        return encoded;

    // The frames are kept in the stream's native byte order (see
    // PipeWireGrabber::pixelFormat) — wrap them in the QImage format whose
    // in-memory bytes match, so nothing has to be swizzled per frame.
    const QString pf = m_grabber ? m_grabber->pixelFormat() : QString();
    QImage::Format fmt;
    if (pf == QLatin1String("bgra"))      fmt = QImage::Format_ARGB32;
    else if (pf == QLatin1String("bgr0")) fmt = QImage::Format_RGB32;
    else if (pf == QLatin1String("rgba")) fmt = QImage::Format_RGBA8888;
    else if (pf == QLatin1String("rgb0")) fmt = QImage::Format_RGBX8888;
    else return encoded;   // unknown order: ship the frame rather than corrupt it

    const qsizetype need = qsizetype(m_encodeSize.width()) * m_encodeSize.height() * 4;
    if (encoded.size() != need)
        return encoded;

    // Composite into a REUSED scratch buffer: resize only when the geometry
    // changes, so a 4K recording does not realloc ~33 MB every sample.
    if (m_overlayFrame.size() != need)
        m_overlayFrame.resize(need);
    memcpy(m_overlayFrame.data(), encoded.constData(), size_t(need));

    QImage img(reinterpret_cast<uchar *>(m_overlayFrame.data()),
               m_encodeSize.width(), m_encodeSize.height(),
               m_encodeSize.width() * 4, fmt);
    QPainter p(&img);
    m_cursorOverlay.paint(p, nowNs);
    p.end();
    return m_overlayFrame;
#else
    Q_UNUSED(nowNs)
    return encoded;
#endif
}

void GifRecorder::startClickCapture()
{
    if (!m_settings->cursorClickRipple() || m_clicks)
        return;
    // Reading global pointer buttons needs access to /dev/input, i.e. the
    // `input` group. Most desktop users are NOT in it, so this has to be a
    // graceful degrade to halo-only, never an error: the ripple is a garnish,
    // the recording is the point.
    if (InputPermission::probe() != InputPermission::Available)
        return;
    m_clicks = new ClickCapture(this);
    connect(m_clicks, &ClickCapture::buttonEvent, this,
            [this](qint64 tUsec, int button, bool pressed) {
                Q_UNUSED(button)
                if (!pressed || m_paused)
                    return;
                // libinput reports microseconds; everything else here is ns.
                m_cursorOverlay.addClick(tUsec * 1000LL);
            });
    m_clicks->start();
}

void GifRecorder::stopClickCapture()
{
    if (!m_clicks)
        return;
    m_clicks->stop();
    m_clicks->deleteLater();
    m_clicks = nullptr;
}

void GifRecorder::openPortalSession()
{
#ifdef HAVE_PIPEWIRE
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
    connect(m_session, &ScreenCastSession::restoreTokenChanged, this, [this](const QString &token) {
        const QString key = restoreTokenKey(m_source, m_targetScreen);
        if (token.isEmpty())
            m_settings->raw()->remove(key);
        else
            m_settings->raw()->setValue(key, token);
    });
    // Cursor mode. Highlighting the pointer means asking for CursorMetadata,
    // which STOPS the compositor drawing the pointer into the stream — from
    // then on m_cursorOverlay is the only thing that draws it. Metadata is
    // optional in the portal spec and an unsupported mode fails SelectSources
    // outright, so a portal without it silently degrades to the plain embedded
    // cursor rather than killing the recording.
    m_cursorOverlayActive = false;
    ScreenCastSession::CursorMode cursorMode = m_settings->includeCursor()
                                                   ? ScreenCastSession::CursorEmbedded
                                                   : ScreenCastSession::CursorHidden;
    if (m_settings->includeCursor() && m_settings->cursorHighlight()) {
        if (ScreenCastSession::availableCursorModes() & ScreenCastSession::CursorMetadata) {
            cursorMode = ScreenCastSession::CursorMetadata;
            m_cursorOverlayActive = true;
        } else {
            qWarning() << "GifRecorder: portal has no metadata cursor mode,"
                          " recording with the plain embedded cursor";
        }
    }

    // The overlay draws the system cursor 1:1 with a halo and click ripples.
    // Pointer size / custom images / glide are deliberately NOT here — that
    // styling is Unisic Studio's job; a recorder keeps the pointer true to the
    // desktop. The pointer is smoothed only to kill the raw metadata jitter,
    // using CursorSmoother's default (Studio-tuned) parameters.
    CursorOverlayPainter::Style style;
    style.highlight = m_settings->cursorHighlightHalo();
    style.highlightColor = QColor(m_settings->cursorHighlightColor());
    style.ripple = m_settings->cursorClickRipple();
    m_cursorOverlay.setStyle(style);
    m_cursorOverlay.reset();
    m_cursorSmoother = CursorSmoother();

    // Window source → portal WINDOW picker; otherwise a monitor.
    const QString restoreToken =
        m_settings->raw()->value(restoreTokenKey(m_source, m_targetScreen)).toString();
    m_session->start(cursorMode, m_source == Window ? 2u : 1u, restoreToken);
#endif
}

void GifRecorder::commit()
{
#ifdef HAVE_PIPEWIRE
    if (m_state != Starting || !m_armed || m_committed)
        return;
    m_committed = true;
    beginEncoding(m_heldStreamSize);
#endif
}

void GifRecorder::onStreamReady(int fd, uint nodeId, const QSize &, const QPoint &pos)
{
#ifdef HAVE_PIPEWIRE
    // Wrong-monitor guard (dual-monitor): the region crop is LOCAL to the
    // screen it was drawn on, so a stream of a different monitor records a
    // misplaced area. Happens when a stale restore token replays a previously
    // picked monitor, or the user shares the wrong one in the portal dialog.
    // The portal's stream "position" (logical workspace coords) exposes it.
    if (m_source == Region && m_targetScreen && pos.x() != INT_MIN
        && pos != m_targetScreen->geometry().topLeft()) {
        ::close(fd); // dup'd for us — nobody else will
        m_settings->raw()->remove(restoreTokenKey(m_source, m_targetScreen));
        m_session->disconnect(this);
        m_session->deleteLater();
        m_session = nullptr;
        if (!m_monitorRetryDone) {
            // One self-heal round: re-open without the token so the picker
            // shows and the user can share the monitor the region is on.
            m_monitorRetryDone = true;
            openPortalSession();
            return;
        }
        fail(tr("The shared screen doesn't match the one the region was selected on — pick \"%1\" in the sharing dialog")
                 .arg(m_targetScreen->name()));
        return;
    }
    m_grabber = new PipeWireGrabber(this);
    connect(m_grabber, &PipeWireGrabber::formatReady, this, &GifRecorder::beginEncoding);
    // Guard by state: a streamError queued from the PipeWire thread just before
    // stop() is still delivered during Converting (disconnect doesn't cancel
    // already-posted metacalls) and would abort the finalizing recording.
    connect(m_grabber, &PipeWireGrabber::streamError, this, [this](const QString &e) {
        if (m_state == Starting || m_state == Recording)
            fail(e);
    });
    // Cap the negotiated stream framerate at the rate the sampler consumes so
    // the compositor throttles delivery instead of running onProcess's full-
    // frame copy at monitor refresh (most of those copies would be discarded).
    const int targetFps = qBound(1, m_output == Gif ? m_settings->gifFps()
                                                    : m_settings->videoFps(), 60);
    if (m_cursorOverlayActive) {
        // Queued by default (emitted from the PipeWire thread).
        connect(m_grabber, &PipeWireGrabber::cursorShapeChanged, this,
                [this](int id, const QImage &img, const QPoint &hotspot) {
                    m_cursorOverlay.setShape(id, img, hotspot);
                });
        startClickCapture();
    }
    if (!m_grabber->start(fd, nodeId, targetFps, m_cursorOverlayActive))
        fail(tr("Failed to connect to the PipeWire stream"));
#else
    Q_UNUSED(fd) Q_UNUSED(nodeId)
#endif
}

void GifRecorder::beginEncoding(const QSize &streamSize)
{
    if (m_state != Starting)
        return;
    // The ffmpeg encoder probe (blocking magic-static, warmed off-thread in
    // start()) can lose the race to a portal restore-token fast path; defer the
    // whole setup until it finishes rather than freezing the GUI on the probe.
    // Re-entry is safe: a spurious extra call sees a non-Starting state above.
    if (m_probeWarmed && !m_probeWatcher.isFinished()) {
        connect(&m_probeWatcher, &QFutureWatcher<void>::finished, this,
                [this, streamSize] { beginEncoding(streamSize); });
        return;
    }
    // Commit hold. The portal share dialog is resolved FIRST; reaching here means
    // it was approved and the stream is live. Announce arming (the caller runs
    // the countdown / start cue) and WAIT — encoding begins only on commit(), so
    // no countdown number and no start sound can land in the recording.
    if (m_holdForCommit && !m_committed) {
        if (!m_armed) {
            m_armed = true;
            m_heldStreamSize = streamSize;
            emit armed();
        }
        return;
    }
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
    m_paused = false;
    m_pauseStartMs = 0;
    m_pausedTotalMs = 0;
    m_maxRemainingMs = 0;
    m_pauseIntervals.clear();
    const QString ext = m_output == Gif ? QStringLiteral("gif")
                      : m_output == WebM ? QStringLiteral("webm")
                                         : QStringLiteral("mp4");
    const QDateTime nowDt = QDateTime::currentDateTime();
    const QString stamp = nowDt.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    // Optional per-month subfolders (yyyy-MM) — same knob as screenshots
    // (Settings::dateSubfolders), applied to recordings too so the option
    // actually buckets GIFs/videos and not just images. mkpath creates the
    // subfolder as well.
    QString outDir = m_settings->videoSaveDirectory();
    if (m_settings->dateSubfolders())
        outDir += QLatin1Char('/') + nowDt.toString(QStringLiteral("yyyy-MM"));
    QDir().mkpath(outDir);
    // Lossless intermediate goes to disk-backed XDG cache, NOT TempLocation:
    // /tmp is tmpfs on Fedora, and minutes of lossless 4K would
    // exhaust RAM (max duration defaults to unlimited) and lose the recording
    // when ffmpeg's write hits ENOSPC.
    const QString tmpBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(tmpBase);
    if (m_output == Replay) {
        m_replayDir = tmpBase + QStringLiteral("/unisic-replay-ring");
        QDir(m_replayDir).removeRecursively();
        QDir().mkpath(m_replayDir);
        m_tempPath = m_replayDir + QStringLiteral("/segment-%03d.mkv");
        m_outPath.clear();
    } else {
        m_tempPath = tmpBase + QStringLiteral("/unisic-rec-%1.mkv").arg(stamp);
        m_outPath = outDir + QStringLiteral("/Unisic_%1.%2").arg(stamp, ext);
    }

    // Feed ffmpeg the stream's native byte order (bgr0/bgra/rgb0/rgba) so the
    // grabber's onProcess stays a plain row memcpy; the encoder's swscale does
    // any conversion far faster than a per-pixel swizzle on the PipeWire thread.
    QString pixFmt = QStringLiteral("bgra");
#ifdef HAVE_PIPEWIRE
    if (m_grabber)
        pixFmt = m_grabber->pixelFormat();
#endif

    // -nostats: with MergedChannels the progress spam would otherwise grow
    // unbounded in the QProcess read buffer for the whole recording.
    QStringList args{QStringLiteral("-y"),
                     QStringLiteral("-nostats"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"),
                     QStringLiteral("-pix_fmt"), pixFmt,
                     QStringLiteral("-video_size"),
                     QStringLiteral("%1x%2").arg(m_encodeSize.width()).arg(m_encodeSize.height()),
                     QStringLiteral("-framerate"), QString::number(fps),
                     // A raw packet is a whole frame. 512 here can make ffmpeg
                     // retain tens of gigabytes independently of QProcess's
                     // bounded writeCap; two frames absorb hand-off jitter
                     // without recreating a second large frame reservoir.
                     QStringLiteral("-thread_queue_size"), QStringLiteral("2"),
                     QStringLiteral("-i"), QStringLiteral("-")};

    // Audio inputs are added after raw screen input 0.
    int nextInput = 1;

    // Optional audio — video output only (GIF has none). Pulse sources are
    // followed by an optional PipeWire application node captured through a
    // bounded kernel FIFO (pw-record writes raw PCM; no audio accumulates in RAM).
    m_hasAudio = false;
    QStringList audioSources;
    QVector<int> audioInputs;
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
        audioInputs << nextInput++;
    }
    const QString appNode = m_output == Gif ? QString()
                                             : m_settings->recordAppAudioNode().trimmed();
    if (!appNode.isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("pw-record")).isEmpty()) {
        m_audioFifoPath = tmpBase + QStringLiteral("/unisic-audio-%1.fifo").arg(stamp);
        QFile::remove(m_audioFifoPath);
        if (::mkfifo(QFile::encodeName(m_audioFifoPath).constData(), 0600) == 0) {
            args << QStringLiteral("-thread_queue_size") << QStringLiteral("1024")
                 << QStringLiteral("-f") << QStringLiteral("s16le")
                 << QStringLiteral("-ar") << QStringLiteral("48000")
                 << QStringLiteral("-ac") << QStringLiteral("2")
                 << QStringLiteral("-i") << m_audioFifoPath;
            audioInputs << nextInput++;
        } else {
            m_audioFifoPath.clear();
        }
    }

    // Lossless RGB intermediate: libx264rgb (fastest) when the ffmpeg has GPL
    // x264, else utvideo (fast intra-only RGB), else FFV1 — fallbacks for an
    // ffmpeg built without GPL x264.
    const QSet<QString> &encoders = ffmpegEncoders();
    if (m_output == Replay) {
        if (encoders.contains(QStringLiteral("libx264")) || encoders.isEmpty()) {
            args << QStringLiteral("-c:v") << QStringLiteral("libx264")
                 << QStringLiteral("-preset") << QStringLiteral("ultrafast")
                 << QStringLiteral("-crf") << QString::number(qBound(0, m_settings->videoQuality(), 40));
        } else if (encoders.contains(QStringLiteral("libopenh264"))) {
            args << QStringLiteral("-c:v") << QStringLiteral("libopenh264")
                 << QStringLiteral("-b:v") << QStringLiteral("6M");
        } else {
            args << QStringLiteral("-c:v") << QStringLiteral("mpeg4")
                 << QStringLiteral("-q:v") << QStringLiteral("3");
        }
        args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    } else if (encoders.contains(QStringLiteral("libx264rgb")) || encoders.isEmpty()) {
        args << QStringLiteral("-c:v") << QStringLiteral("libx264rgb")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-qp") << QStringLiteral("0");
    } else if (encoders.contains(QStringLiteral("utvideo"))) {
        args << QStringLiteral("-c:v") << QStringLiteral("utvideo");
    } else {
        args << QStringLiteral("-c:v") << QStringLiteral("ffv1");
    }

    QStringList filters;
    QString videoMap = QStringLiteral("0:v");

    QString audioMap;
    if (audioInputs.size() == 1) {
        audioMap = QStringLiteral("%1:a").arg(audioInputs.first());
    } else if (audioInputs.size() >= 2) {
        QString labels;
        for (int index : std::as_const(audioInputs))
            labels += QStringLiteral("[%1:a]").arg(index);
        filters << labels + QStringLiteral("amix=inputs=%1:duration=longest:normalize=0[aout]")
                             .arg(audioInputs.size());
        audioMap = QStringLiteral("[aout]");
    }
    if (!filters.isEmpty())
        args << QStringLiteral("-filter_complex") << filters.join(QLatin1Char(';'));

    args << QStringLiteral("-map") << videoMap;

    // Audio mux: one source maps straight through, two or more are mixed. Stored as
    // lossless FLAC in the intermediate (convertVideo re-encodes to AAC/Opus).
    // -shortest ends the file when the video (pipe) stops so the live pulse
    // captures — which never EOF on their own — don't hang the encoder.
    if (audioInputs.size() == 1) {
        args << QStringLiteral("-map") << audioMap
             << QStringLiteral("-c:a") << QStringLiteral("flac");
        // -shortest lets a never-EOF pulse capture end together with the video
        // pipe. But an app-audio FIFO CAN EOF early (pw-record dies / the target
        // node vanishes); with -shortest that truncates the WHOLE recording to a
        // fraction of a second. When app audio is the SOLE source, drop it: our
        // explicit stop closes the video pipe and kills pw-record so ffmpeg still
        // ends, and a mid-recording pw-record death just ends the audio stream
        // instead of the file. (m_audioFifoPath non-empty ⟺ the sole input is
        // the FIFO, since any pulse source would add a second input.)
        if (m_audioFifoPath.isEmpty())
            args << QStringLiteral("-shortest");
        m_hasAudio = true;
    } else if (audioInputs.size() >= 2) {
        args << QStringLiteral("-map") << audioMap
             << QStringLiteral("-c:a") << QStringLiteral("flac")
             << QStringLiteral("-shortest");
        m_hasAudio = true;
    }
    if (m_output == Replay) {
        const int segments = replaySegmentCount(m_settings->instantReplaySeconds());
        // The segment muxer can only cut on a keyframe. libx264(rgb) at
        // -preset ultrafast has no forced GOP, so keyint defaults to 250 frames
        // (~4-8s) and every "2s" segment is really ~8s — the ring then holds far
        // more than the requested window and an early Save-replay (before two
        // segments exist) yields nothing. Force an IDR every 2s so segment_time
        // is honoured and replaySegmentCount's /2 maths hold.
        args << QStringLiteral("-force_key_frames") << QStringLiteral("expr:gte(t,n_forced*2)")
             << QStringLiteral("-f") << QStringLiteral("segment")
             << QStringLiteral("-segment_time") << QStringLiteral("2")
             << QStringLiteral("-segment_wrap") << QString::number(segments)
             << QStringLiteral("-reset_timestamps") << QStringLiteral("1")
             << m_tempPath;
    } else {
        args << m_tempPath;
    }

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
        if (m_output == Replay) {
            cleanup();
            emit finished({});
        } else {
            maybeExcisePauses([this] {
                if (m_output == Gif)
                    convertToGif();
                else
                    convertVideo();
            });
        }
    });
    auto *encoder = m_ffmpeg;
    encoder->start(QStringLiteral("ffmpeg"), args);
    if (!m_audioFifoPath.isEmpty()) {
        m_appAudio = new QProcess(this);
        connect(m_appAudio, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart
                && (m_state == Starting || m_state == Recording))
                fail(tr("pw-record could not capture the selected application audio"));
        });
        // A pw-record that exits mid-recording (target node closed) used to be
        // silent. Surface it as a warning; the recording keeps going without
        // -shortest driving the file end (see the audio-map branch above).
        connect(m_appAudio, &QProcess::finished, this,
                [](int code, QProcess::ExitStatus status) {
            if (status != QProcess::NormalExit || code != 0)
                qWarning() << "unisic: application-audio capture (pw-record) exited early, code" << code;
        });
        m_appAudio->start(QStringLiteral("pw-record"),
                          {QStringLiteral("--target"), appNode,
                           QStringLiteral("--rate"), QStringLiteral("48000"),
                           QStringLiteral("--channels"), QStringLiteral("2"),
                           QStringLiteral("--format"), QStringLiteral("s16"),
                           QStringLiteral("--raw"), m_audioFifoPath});
    }
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
    // so a momentary encoder stall would drop samples immediately. But keep the
    // ceiling TIGHT: when the encoder can't sustain the frame rate at all
    // (software encode in a VM / GNOME on weak hardware), the backlog fills to
    // whatever this cap allows and just sits there for the whole recording —
    // ×30 measured as a standing 462 MB RSS at 1440p (~1 GB at 4K), and every
    // buffered byte also delays stop(): closeWriteChannel() flushes the entire
    // backlog through the encoder before conversion can begin. A sustained
    // deficit drops samples at ANY cap (pacing duplicates frames afterwards),
    // so a big backlog buys nothing beyond burst absorption — ×6 (~200 ms at
    // 30 fps) absorbs the same stalls at a fraction of the memory.
    const qsizetype frameBytes = qsizetype(m_encodeSize.width()) * m_encodeSize.height() * 4;
    if (frameBytes <= 0)
        return;
    const qsizetype absoluteCap = qsizetype(192) * 1024 * 1024;
    const qsizetype queuedFrames = qBound(qsizetype(1), absoluteCap / frameBytes,
                                          qsizetype(6));
    const qsizetype writeCap = frameBytes * queuedFrames;
    if (m_ffmpeg->bytesToWrite() + frameBytes > writeCap)
        return;
    // ffmpeg's rawvideo demuxer slices stdin into fixed m_streamSize frames. If
    // the source renegotiated a different size mid-recording (e.g. a window was
    // resized), feeding the new-size frame would desync every subsequent frame
    // into corruption — hold the last good frame instead.
    const qsizetype expected = qsizetype(m_streamSize.width()) * m_streamSize.height() * 4;
    QByteArray frame;
    QByteArray encoded;
    quint64 seq = 0;
    // While paused, don't pull new frames: keep the pacing running (so video and
    // audio stay wall-clock-aligned in the intermediate) but repeat the last
    // frame — the whole paused span is excised later, so its content is moot and
    // a freeze is the cheapest thing to encode.
    if (!m_paused && m_grabber->latestFrame(frame, &seq) && frame.size() == expected) {
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

    // Cursor overlay. Drain every sample (not just the newest): the one-euro
    // filter is a time series, so skipping samples would change the smoothing.
    // While paused the pointer must freeze with the picture — the paused span
    // is excised later, so a pointer that kept moving would teleport across
    // the cut.
    if (m_cursorOverlayActive && m_grabber && !m_paused) {
        const QVector<CursorSample> samples = m_grabber->takeCursorSamples();
        for (const CursorSample &s : samples) {
            const QPointF sm = m_cursorSmoother.filter(s.x, s.y, s.tMonoNs);
            // Stream pixels -> encoded-frame pixels (region recording crops).
            m_cursorOverlay.setCursor(sm - QPointF(m_encodeCrop.topLeft()), s.visible, s.shapeId);
        }
    }
    if (m_cursorOverlayActive)
        encoded = compositeCursorOverlay(encoded, monoNowNs());

    // Wall-clock pacing: the timer interval truncates (1000/30 = 33 ms →
    // 30.3 fps) and backpressure drops ticks, while the container claims an
    // exact -framerate — pace by elapsed time, duplicating frames as needed,
    // or playback speed drifts from real time.
    const qint64 target = m_elapsed.elapsed() * m_fps / 1000 + 1;
    if (target <= m_framesWritten)
        return; // ahead of schedule
    qint64 n = qMin<qint64>(target - m_framesWritten, m_fps); // ≤1 s burst
    // Also clamp by the remaining write-buffer headroom: after an encoder stall
    // drained ticks, one catch-up tick could otherwise memcpy up to fps full
    // frames (~2 GB at 4K60) into the QProcess buffer in a single GUI-thread
    // loop. bytesToWrite() <= writeCap here (checked above), so headroom >= 0;
    // If there is not room for one whole frame, wait for the next tick; rawvideo
    // cannot safely accept a partial frame as a pacing unit.
    n = qMin<qint64>(n, (writeCap - m_ffmpeg->bytesToWrite()) / frameBytes);
    qint64 accepted = 0;
    for (; accepted < n; ++accepted) {
        qsizetype offset = 0;
        while (offset < encoded.size()) {
            const qint64 written = m_ffmpeg->write(encoded.constData() + offset,
                                                    encoded.size() - offset);
            if (written <= 0)
                break;
            offset += written;
        }
        if (offset != encoded.size())
            break;
    }
    m_framesWritten += accepted;
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
    // Stopped while paused: close the open span so its frozen tail (and the audio
    // that kept flowing under it) is excised like any other pause.
    if (m_paused) {
        m_pauseIntervals.append({m_pauseStartMs, m_elapsed.elapsed()});
        m_paused = false;
        emit pausedChanged();
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
    stopProcess(m_appAudio);
    stopClickCapture(); // release the libinput devices as soon as we stop drawing
    m_ffmpeg->closeWriteChannel(); // EOF -> ffmpeg finalizes the file
    m_lastFrame.clear(); // don't pin a full raw frame through the whole conversion
    m_overlayFrame.clear(); // ditto for the overlay scratch copy
}

void GifRecorder::saveInstantReplay()
{
    if (m_state != Recording || m_output != Replay || m_replayExporter)
        return;
    QDir ring(m_replayDir);
    QFileInfoList segments = ring.entryInfoList({QStringLiteral("segment-*.mkv")},
                                                QDir::Files, QDir::Time | QDir::Reversed);
    if (segments.size() < 2) {
        emit replayExportFailed(tr("Instant replay needs at least one completed segment"));
        return;
    }
    // The newest segment is still being written. COPY the completed files into
    // an independent snapshot before concatenating: a symlink (what QFile::link
    // makes on Unix) OR even a hard link is insufficient, because segment_wrap
    // reopens the ring names with O_TRUNC and rewrites the SAME inode — ffmpeg
    // would then read a truncated/mutating file. Only a byte copy isolates the
    // export from the live ring.
    segments.removeLast();
    const QString snapshot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/unisic-replay-save-")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(snapshot);
    QStringList linked;
    int number = 0;
    for (const QFileInfo &source : std::as_const(segments)) {
        const QString target = snapshot + QStringLiteral("/%1.mkv").arg(number++, 4, 10, QLatin1Char('0'));
        if (QFile::copy(source.absoluteFilePath(), target))
            linked << target;
    }
    if (linked.isEmpty()) {
        QDir(snapshot).removeRecursively();
        emit replayExportFailed(tr("Could not snapshot the replay segments"));
        return;
    }
    QSaveFile concat(snapshot + QStringLiteral("/concat.txt"));
    if (!concat.open(QIODevice::WriteOnly)) {
        QDir(snapshot).removeRecursively();
        emit replayExportFailed(tr("Could not prepare the replay export"));
        return;
    }
    for (QString path : std::as_const(linked)) {
        path.replace(QLatin1Char('\''), QLatin1String("'\\''"));
        concat.write("file '" + path.toUtf8() + "'\n");
    }
    const QString concatPath = concat.fileName();
    if (!concat.commit()) {
        QDir(snapshot).removeRecursively();
        emit replayExportFailed(tr("Could not prepare the replay export"));
        return;
    }

    QString outDir = m_settings->videoSaveDirectory();
    QDir().mkpath(outDir);
    const QString stem = QStringLiteral("Unisic_Replay_%1")
                             .arg(QDateTime::currentDateTime().toString(
                                 QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    QString out = outDir + QLatin1Char('/') + stem + QStringLiteral(".mp4");
    for (int i = 1; QFileInfo::exists(out); ++i)
        out = outDir + QLatin1Char('/') + stem + QStringLiteral("-%1.mp4").arg(i);
    auto *process = new QProcess(this);
    m_replayExporter = process;
    m_replaySnapshotDir = snapshot;
    m_replayExportPath = out;
    const auto completed = std::make_shared<bool>(false);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this,
            [this, process, snapshot, out, completed](int code, QProcess::ExitStatus status) {
        if (*completed)
            return;
        *completed = true;
        const QString diagnostic = QString::fromUtf8(process->readAll()).trimmed();
        if (m_replayExporter == process)
            m_replayExporter = nullptr;
        process->deleteLater();
        QDir(snapshot).removeRecursively();
        m_replaySnapshotDir.clear();
        m_replayExportPath.clear();
        if (code != 0 || status != QProcess::NormalExit) {
            QFile::remove(out);
            emit replayExportFailed(diagnostic.isEmpty() ? tr("Instant replay export failed")
                                                          : diagnostic);
            return;
        }
        emit finished(out, /*fromInstantReplay=*/true);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, snapshot, out, completed](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *completed)
            return;
        *completed = true;
        if (m_replayExporter == process)
            m_replayExporter = nullptr;
        process->deleteLater();
        QFile::remove(out);
        QDir(snapshot).removeRecursively();
        m_replaySnapshotDir.clear();
        m_replayExportPath.clear();
        emit replayExportFailed(tr("Instant replay export failed"));
    });
    process->start(QStringLiteral("ffmpeg"),
                   {QStringLiteral("-y"), QStringLiteral("-nostats"),
                    QStringLiteral("-loglevel"), QStringLiteral("error"),
                    QStringLiteral("-f"), QStringLiteral("concat"),
                    QStringLiteral("-safe"), QStringLiteral("0"),
                    QStringLiteral("-i"), concatPath,
                    QStringLiteral("-c:v"), QStringLiteral("copy"),
                    // Ring segments keep optional live audio as FLAC. MP4 does
                    // not support FLAC portably, so only audio is converted;
                    // the already-encoded video remains a zero-copy concat.
                    QStringLiteral("-c:a"), QStringLiteral("aac"),
                    QStringLiteral("-b:a"), QStringLiteral("192k"),
                    QStringLiteral("-movflags"), QStringLiteral("+faststart"), out});
}

QStringList GifRecorder::pauseExciseArgs(const QString &input, const QString &output,
                                         const QList<QPair<qint64, qint64>> &intervalsMs,
                                         bool hasAudio)
{
    // keep = a frame/sample whose timestamp is NOT inside any paused [t0,t1]
    // wall-clock span. Cutting the SAME ranges from video and audio preserves
    // their sync (both shift back by the cut length); setpts/asetpts restamp the
    // survivors onto a gapless timeline, so the container duration is the
    // recorded (un-paused) length. Commas inside the expression are protected by
    // the surrounding single quotes (there is no shell — ffmpeg sees them raw).
    QStringList inside;
    for (const auto &iv : intervalsMs) {
        inside << QStringLiteral("between(t,%1,%2)")
                      .arg(iv.first / 1000.0, 0, 'f', 3)
                      .arg(iv.second / 1000.0, 0, 'f', 3);
    }
    const QString keep = QStringLiteral("not(%1)").arg(inside.join(QLatin1Char('+')));
    const QString vf = QStringLiteral("select='%1',setpts=N/FRAME_RATE/TB").arg(keep);
    const QString af = QStringLiteral("aselect='%1',asetpts=N/SR/TB").arg(keep);

    QStringList args{QStringLiteral("-y"), QStringLiteral("-nostats"),
                     QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-i"), input,
                     QStringLiteral("-map"), QStringLiteral("0:v:0"),
                     QStringLiteral("-vf"), vf};
    // Re-encode losslessly with the same RGB codec the intermediate used, so the
    // final conversion is bit-for-bit unaffected by the excise pass.
    const QSet<QString> &encoders = ffmpegEncoders();
    if (encoders.contains(QStringLiteral("libx264rgb")) || encoders.isEmpty())
        args << QStringLiteral("-c:v") << QStringLiteral("libx264rgb")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-qp") << QStringLiteral("0");
    else if (encoders.contains(QStringLiteral("utvideo")))
        args << QStringLiteral("-c:v") << QStringLiteral("utvideo");
    else
        args << QStringLiteral("-c:v") << QStringLiteral("ffv1");
    if (hasAudio)
        args << QStringLiteral("-map") << QStringLiteral("0:a:0?")
             << QStringLiteral("-af") << af
             << QStringLiteral("-c:a") << QStringLiteral("flac");
    args << output;
    return args;
}

void GifRecorder::maybeExcisePauses(std::function<void()> thenConvert)
{
    if (m_pauseIntervals.isEmpty()) {
        thenConvert();
        return;
    }

    const QString excised = m_tempPath + QStringLiteral(".cut.mkv");
    QFile::remove(excised);
    const QStringList args = pauseExciseArgs(m_tempPath, excised, m_pauseIntervals, m_hasAudio);

    auto *conv = new QProcess(this);
    m_converter = conv;
    conv->setProcessChannelMode(QProcess::MergedChannels);
    connect(conv, &QProcess::finished, this,
            [this, conv, excised, thenConvert](int code, QProcess::ExitStatus status) {
        if (m_converter != conv)
            return;
        const QByteArray out = conv->readAll();
        m_converter = nullptr;
        conv->deleteLater();
        if (code != 0 || status == QProcess::CrashExit || !QFile::exists(excised)) {
            qWarning() << out;
            QFile::remove(excised);
            QFile::remove(m_tempPath);
            fail(tr("Removing the paused sections failed"));
            return;
        }
        QFile::remove(m_tempPath);
        m_tempPath = excised; // conversion now reads the paused-sections-removed file
        thenConvert();
    });
    connect(conv, &QProcess::errorOccurred, this,
            [this, conv, excised](QProcess::ProcessError error) {
        if (m_converter != conv || error != QProcess::FailedToStart)
            return;
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(excised);
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    });
    conv->start(QStringLiteral("ffmpeg"), args);
    if (!conv->waitForStarted(3000) && m_converter == conv) {
        m_converter = nullptr;
        conv->deleteLater();
        QFile::remove(excised);
        QFile::remove(m_tempPath);
        fail(tr("ffmpeg could not be started. Is it installed?"));
    }
}

void GifRecorder::convertToGif()
{
    // Captured at recording start (beginEncoding): the intermediate was encoded
    // with -framerate m_fps, so reading a mid-recording gifFps() change here
    // would quadruple/drop every frame relative to the container rate.
    const int fps = m_fps;
    // quality: 0 = fast/small, 1 = balanced, 2 = best. The filters themselves
    // live in gifPaletteGenFilter/gifPaletteUseFilter — the trim editor renders
    // a GIF cut through the same two passes.
    const int q = qBound(0, m_settings->gifQuality(), 2);
    const QString dither = gifPaletteUseFilter(q);

    // True two-pass. A single split-graph command (`split[a][b];[a]palettegen…`)
    // buffers every decoded [b] frame in RAM until palettegen hits EOF — ~3 GB
    // for 30 s of 1080p, >10 GB for 4K — driving the machine into swap. The
    // temp file is on disk anyway; decoding the lossless intermediate twice
    // is cheap by comparison.
    m_palettePath = m_tempPath + QStringLiteral(".palette.png");
    const QString vf = QStringLiteral("fps=%1,%2").arg(fps).arg(gifPaletteGenFilter(q));

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

void GifRecorder::convertToGifRender(int fps, const QString &paletteUse)
{
    const QString lavfi = QStringLiteral("fps=%1[x];[x][1:v]%2").arg(fps).arg(paletteUse);

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
             // deadline=realtime + cpu-used 5, measured on an 11 s 1440p clip:
             // 14.5 s -> 4.8 s, SSIM 0.99866 -> 0.99844 (invisible), file +15%.
             // Do NOT "improve" this to cpu-used 6 with deadline=good: that is
             // reproducibly 2.4x SLOWER than cpu-used 4, not faster.
             // There is no usable hardware path to fall back on either — a listed
             // vp9_vaapi failed to encode at all on the box this was tuned on.
             << QStringLiteral("-deadline") << QStringLiteral("realtime")
             << QStringLiteral("-cpu-used") << QStringLiteral("5")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-row-mt") << QStringLiteral("1");
    } else if (resolvedVideoEncoder() == QLatin1String("vaapi")
               && hardwareEncoderAvailable(QStringLiteral("vaapi"))) {
        const int qp = qBound(1, crf, 40);
        args << QStringLiteral("-vaapi_device") << QStringLiteral("/dev/dri/renderD128")
             << QStringLiteral("-vf") << QStringLiteral("format=nv12,hwupload")
             << QStringLiteral("-c:v") << QStringLiteral("h264_vaapi")
             << QStringLiteral("-qp") << QString::number(qp)
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    } else if (resolvedVideoEncoder() == QLatin1String("nvenc")
               && hardwareEncoderAvailable(QStringLiteral("nvenc"))) {
        args << QStringLiteral("-c:v") << QStringLiteral("h264_nvenc")
             << QStringLiteral("-preset") << QStringLiteral("p4")
             << QStringLiteral("-cq") << QString::number(crf)
             << QStringLiteral("-b:v") << QStringLiteral("0")
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    } else if (ffmpegEncoders().contains(QStringLiteral("libx264"))
               || ffmpegEncoders().isEmpty()) {
        args << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("veryfast")
             << QStringLiteral("-crf") << QString::number(crf)
             << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    } else {
        // No GPL x264: OpenH264. It has no CRF mode, so
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
    stopClickCapture();
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
    stopProcess(m_appAudio);
    stopProcess(m_replayExporter);
    if (!m_tempPath.isEmpty())
        QFile::remove(m_tempPath);
    if (!m_palettePath.isEmpty())
        QFile::remove(m_palettePath);
    // Aborting mid-conversion leaves a truncated file in the save directory.
    if (m_state == Converting && !m_outPath.isEmpty())
        QFile::remove(m_outPath);
    if (!m_audioFifoPath.isEmpty())
        QFile::remove(m_audioFifoPath);
    cleanup();
}

void GifRecorder::cleanup()
{
    m_state = Idle;
    // Return the exposed pause state to un-paused on EVERY termination (a fail()
    // while paused reaches here via abort() without going through togglePause),
    // so recordingPaused() can't report a stale "paused" after we go Idle.
    if (m_paused) {
        m_paused = false;
        emit pausedChanged();
    }
    m_pauseStartMs = 0;
    m_pausedTotalMs = 0;
    m_maxRemainingMs = 0;
    m_pauseIntervals.clear();
    m_tempPath.clear();
    m_palettePath.clear();
    m_outPath.clear();
    if (!m_audioFifoPath.isEmpty())
        QFile::remove(m_audioFifoPath);
    // Never delete an in-flight replay export's inputs/output: the Replay stop
    // path reaches cleanup() via the segment-muxer finished handler while a Save
    // may still be running. Its own finished/errorOccurred handlers own the
    // snapshot dir + output file; only reclaim them here when no exporter is live.
    if (!m_replayExporter) {
        if (!m_replayExportPath.isEmpty())
            QFile::remove(m_replayExportPath);
        if (!m_replaySnapshotDir.isEmpty())
            QDir(m_replaySnapshotDir).removeRecursively();
        m_replayExportPath.clear();
        m_replaySnapshotDir.clear();
    }
    m_audioFifoPath.clear();
    if (!m_replayDir.isEmpty())
        QDir(m_replayDir).removeRecursively();
    m_replayDir.clear();
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

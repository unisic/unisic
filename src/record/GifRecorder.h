#pragma once
#include <QObject>
#include <QRect>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QPointer>
#include <QFutureWatcher>
#include <QList>
#include <functional>

#include "CursorOverlayPainter.h"
#include "CursorSmoother.h"
#include "KeystrokeOverlayPainter.h"

class ScreenCastSession;
class PipeWireGrabber;
class ClickCapture;
class KeyCapture;
class Settings;
class QScreen;
class KWinScreencasting;
class KWinScreencastStream;

// GIF (and MP4) screen recording:
//   portal ScreenCast -> PipeWireGrabber (latest frame) -> fixed-FPS sampler
//   -> ffmpeg #1 (rawvideo stdin -> lossless temp .mkv)
//   -> on stop: ffmpeg #2 (palettegen/paletteuse two-pass -> .gif)
// Region recording records the full monitor stream and crops in ffmpeg.
//
// On KWin (HAVE_KWIN_SCREENCAST + granted zkde_screencast interface) the
// portal is skipped entirely — the app names the source itself (Spectacle's
// model): Screen streams the monitor under the cursor (or the explicit
// target), Region gets a compositor-side cropped stream (no ffmpeg crop),
// Window resolves a uuid via KWin's interactive picker. No share dialog, no
// restore tokens. Any native-path failure falls back to the portal, mirroring
// KWinScreenShot2 -> PortalScreenshot for stills.
class GifRecorder : public QObject
{
    Q_OBJECT
public:
    enum Output { Gif, Mp4, WebM, Replay };
    enum SourceType { Screen, Region, Window };

    explicit GifRecorder(Settings *settings, QObject *parent = nullptr);
    ~GifRecorder() override;

    bool recording() const { return m_state != Idle; }
    SourceType sourceType() const { return m_source; } // valid while recording
    bool instantReplayActive() const { return m_state != Idle && m_output == Replay; }
    // Pause/resume: the frame sampler keeps writing (frozen) frames and audio
    // keeps flowing so the intermediate stays A/V-synced through the gap; the
    // paused wall-clock spans are excised from BOTH streams before conversion.
    // Not offered for instant replay (the ring records continuously).
    bool paused() const { return m_paused; }
    bool canPause() const { return m_state == Recording && m_output != Replay; }
    void togglePause();
    int elapsedSeconds() const;
    // Encoder probes and the GIF palette filters live in unisic-kit's
    // FfmpegUtil (media/FfmpegUtil.h) — shared with Unisic Studio.
    // videoEncoder(), with "auto" resolved to a working hardware encoder or
    // software.
    QString resolvedVideoEncoder() const;
    // ffmpeg args that excise the given paused wall-clock spans (ms) from `input`
    // into `output`, cutting the SAME ranges from video and audio so they stay
    // synced. Public so the dev/smoke harness exercises the exact filtergraph.
    static QStringList pauseExciseArgs(const QString &input, const QString &output,
                                       const QList<QPair<qint64, qint64>> &intervalsMs,
                                       bool hasAudio);
    static int replaySegmentCount(int seconds)
    { return qBound(3, qBound(10, seconds, 600) / 2 + 2, 302); }

    // cropPhysical: region in stream (physical) pixels; empty = full stream.
    // For Window source the portal picks the window; crop is ignored.
    // holdForCommit: negotiate the portal immediately (so the GNOME share dialog
    // is resolved first), then emit armed() when the stream is live and WAIT —
    // encoding does not begin until commit() is called. The caller runs the
    // countdown / start cue in between, so nothing (no countdown number, no start
    // sound) lands in the recording.
    void start(Output output, SourceType source = Screen, const QRect &cropPhysical = {},
               QScreen *screen = nullptr, bool holdForCommit = false);
    // Keystroke-badge colors (bg, text) resolved from the ACTIVE theme when key
    // capture starts. The Theme singleton lives in QML and the recorder cannot
    // reach it; a provider dodges the engine-vs-recorder construction order —
    // recording always starts long after the engine is up. Invalid colors from
    // the provider keep the built-in defaults.
    void setKeystrokeThemeProvider(std::function<QPair<QColor, QColor>()> provider)
    { m_keystrokeTheme = std::move(provider); }
    // Release a holdForCommit start: begin encoding now. No-op unless armed.
    void commit();
    // Stream size stashed at armed() (physical px). Lets the pre-recording
    // countdown scale its disc to a window recording's actual size.
    QSize armedStreamSize() const { return m_heldStreamSize; }
    void stop();     // finalize -> converting -> finished()
    // Dev harness only: SIGSTOPs the live recording encoder so a following
    // stop() exercises the stop-flush watchdog + temp-salvage path for real.
    bool devFreezeEncoderForTest();
    void abort();    // discard everything
    void saveInstantReplay();

signals:
    // Portal sharing approved and the stream is live, but encoding is HELD until
    // commit(). The UI runs the countdown / start cue on this, so the share
    // dialog is resolved BEFORE the countdown and nothing leaks into the file.
    void armed();
    void started();
    void converting();
    // fromInstantReplay: the file came out of the replay ring's export, not a
    // recording the user started and stopped. Both produce an .mp4, so the path
    // alone cannot tell them apart, and history categorizes them separately.
    void finished(const QString &filePath, bool fromInstantReplay = false);
    void failed(const QString &error);
    void replayExportFailed(const QString &error);
    void elapsedChanged();
    void pausedChanged();

private:
    enum State { Idle, Starting, Recording, Converting };

    void onStreamReady(int fd, uint nodeId, const QSize &size, const QPoint &pos);
    // Create the ScreenCast session with the (per-monitor) restore token; used
    // by start() and by the wrong-monitor retry in onStreamReady. Tries the
    // KWin-native path first (tryOpenKWinStream), then the portal.
    void openPortalSession();
    // KWin-native zkde_screencast: true = a stream was requested (or is being
    // resolved) and the portal must NOT be opened; false = unavailable here,
    // caller continues with the portal.
    bool tryOpenKWinStream();
#if defined(HAVE_PIPEWIRE) && defined(HAVE_KWIN_SCREENCAST)
    // Wire created/failed/closed of a requested native stream.
    void adoptKWinStream(KWinScreencastStream *stream);
    // Interactive KWin window pick (queryWindowInfo) -> stream_window(uuid).
    void openKWinWindowStream(uint cursorMode);
#endif
    // Resolve the requested cursor mode (Hidden/Embedded/Metadata wire value,
    // shared by the portal and the KWin protocol) and arm m_cursorOverlayActive.
    uint resolveCursorMode();
    // Attach the PipeWireGrabber to a negotiated stream. fd = the portal's
    // OpenPipeWireRemote fd, or -1 for the KWin-native path (default daemon).
    void attachStream(int fd, uint nodeId);
    void beginEncoding(const QSize &streamSize);
    void sampleFrame();
    void startClickCapture();
    void stopClickCapture();
    void startKeyCapture();
    void stopKeyCapture();
    // Draws the pointer/halo/ripples and the keystroke badge into a copy of
    // `encoded` and returns it, or returns `encoded` untouched when there is
    // nothing to draw.
    QByteArray compositeCursorOverlay(const QByteArray &encoded, qint64 nowNs);
    // If the user paused during the recording, excise those wall-clock spans
    // from the intermediate (video + audio together) before conversion, then run
    // `thenConvert`. With no pauses it calls `thenConvert` straight away.
    void maybeExcisePauses(std::function<void()> thenConvert);
    void convertToGif();                                     // pass 1: palettegen
    void convertToGifRender(int fps, const QString &paletteUse); // pass 2: paletteuse
    void convertVideo();          // resolves the encoder off-thread, then…
    void convertVideoWith(const QString &encoder); // …builds and runs ffmpeg
    void cleanup();
    void fail(const QString &msg);
    void stopWatchdogTick();

    Settings *m_settings;
    bool m_holdForCommit = false; // wait for commit() before encoding
    bool m_armed = false;         // stream live, waiting for commit()
    bool m_committed = false;     // commit() received; encoding may proceed
    QSize m_heldStreamSize;       // stream size stashed while holding
    bool m_probeWarmed = false; // ffmpeg encoder probe kicked off once
    bool m_orphansSwept = false; // stale-temp sweep runs once, on first start()
    QFutureWatcher<void> m_probeWatcher; // gates beginEncoding until the probe warms
    ScreenCastSession *m_session = nullptr;
    PipeWireGrabber *m_grabber = nullptr;
    QProcess *m_ffmpeg = nullptr;
    QProcess *m_converter = nullptr;
    QProcess *m_appAudio = nullptr;
    QTimer m_sampler;
    QTimer m_maxTimer;
    QElapsedTimer m_elapsed;
    QTimer m_elapsedTick;
    // Stop-flush watchdog (see stop()): kills a recording encoder whose drain
    // makes no progress, so a wedged ffmpeg can't hold "Encoding…" forever.
    QTimer m_stopWatchdog;
    qint64 m_stallBytes = -1; // last observed bytesToWrite()
    qint64 m_stallSize = -1;  // last observed temp file size
    int m_stallMs = 0;        // how long both have been frozen
    bool m_stopKillSent = false; // stage 1 (SIGKILL) done; next stall = never-reaped child

    State m_state = Idle;
    Output m_output = Gif;
    SourceType m_source = Screen;
    bool m_hasAudio = false; // pulse audio captured into the temp (video only)
    bool m_paused = false;
    qint64 m_pauseStartMs = 0;   // m_elapsed.elapsed() when the current pause began
    qint64 m_pausedTotalMs = 0;  // accumulated paused wall-clock (excluded from the readout)
    qint64 m_maxRemainingMs = 0; // max-duration budget held across a pause
    QVector<QPair<qint64, qint64>> m_pauseIntervals; // completed [startMs,endMs] spans to excise
    int m_fps = 15;
    qint64 m_framesWritten = 0; // wall-clock pacing (see sampleFrame)
    QRect m_crop;
    QRect m_encodeCrop;
    QSize m_streamSize;
    QSize m_encodeSize;
    QPointer<QScreen> m_targetScreen;
    bool m_monitorRetryDone = false; // one wrong-monitor self-heal per start()
    // KWin-native screencast (zkde_screencast). m_kwinCast binds the global
    // once and is reused across recordings; m_kwinStream is per-recording.
    // m_nativeStream marks the active recording as native: Region streams
    // arrive pre-cropped (skip the ffmpeg crop + geometry checks) and the
    // grabber connects to the default PipeWire daemon (fd -1).
    KWinScreencasting *m_kwinCast = nullptr;
    KWinScreencastStream *m_kwinStream = nullptr;
    bool m_nativeStream = false;
    QByteArray m_lastFrame;
    quint64 m_lastSampledSeq = 0; // grabber seq of m_lastFrame (skip re-crop when unchanged)

    // Cursor overlay. Only live when the portal granted CursorMetadata: in that
    // mode the compositor stops painting the pointer into the stream, so
    // m_cursorOverlay is what puts it back — alongside the halo and ripples.
    // m_lastFrame stays CLEAN (the overlay is composited into a separate
    // buffer): it is the sample-and-hold source, and baking a halo into it
    // would freeze that halo in place on every held frame.
    bool m_cursorOverlayActive = false;
    CursorOverlayPainter m_cursorOverlay;
    CursorSmoother m_cursorSmoother;
    ClickCapture *m_clicks = nullptr;
    // Keystroke badge (screenkey-style). Independent of the cursor overlay:
    // it works in every cursor mode (the badge draws over the frame whether
    // the compositor embedded the pointer or not). Active only when the
    // setting is on AND libinput access was actually granted.
    bool m_keystrokeOverlayActive = false;
    KeystrokeOverlayPainter m_keystrokes;
    KeyCapture *m_keys = nullptr;
    std::function<QPair<QColor, QColor>()> m_keystrokeTheme;
    QByteArray m_overlayFrame;   // scratch buffer for the composited frame
    QString m_tempPath;
    QString m_palettePath;
    QString m_outPath;
    QString m_audioFifoPath;
    QString m_replayDir;
    QString m_replaySnapshotDir;
    QString m_replayExportPath;
    QProcess *m_replayExporter = nullptr;
};

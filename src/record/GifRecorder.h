#pragma once
#include <QObject>
#include <QRect>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QPointer>
#include <QFutureWatcher>

class ScreenCastSession;
class PipeWireGrabber;
class Settings;
class QScreen;

// GIF (and MP4) screen recording:
//   portal ScreenCast -> PipeWireGrabber (latest frame) -> fixed-FPS sampler
//   -> ffmpeg #1 (rawvideo stdin -> lossless temp .mkv)
//   -> on stop: ffmpeg #2 (palettegen/paletteuse two-pass -> .gif)
// Region recording records the full monitor stream and crops in ffmpeg.
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
    int elapsedSeconds() const;
    static bool hardwareEncoderAvailable(const QString &id);
    // True when this ffmpeg can encode with `name` — or when the encoder probe
    // itself failed (empty set), where callers keep their preferred encoder and
    // let the "ffmpeg could not be started" path report the real problem.
    static bool encoderUsable(const QString &name);
    // The two halves of the palettegen/paletteuse GIF pipeline, without the
    // fps/trim filters in front of them. Shared with the trim editor, which cuts
    // a GIF by re-rendering the selection through the same quality settings.
    // quality: 0 = fast/small, 1 = balanced, 2 = best.
    static QString gifPaletteGenFilter(int quality);
    static QString gifPaletteUseFilter(int quality);
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
    // Release a holdForCommit start: begin encoding now. No-op unless armed.
    void commit();
    void stop();     // finalize -> converting -> finished()
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

private:
    enum State { Idle, Starting, Recording, Converting };

    void onStreamReady(int fd, uint nodeId, const QSize &size, const QPoint &pos);
    // Create the ScreenCast session with the (per-monitor) restore token; used
    // by start() and by the wrong-monitor retry in onStreamReady.
    void openPortalSession();
    void beginEncoding(const QSize &streamSize);
    void sampleFrame();
    void convertToGif();                                     // pass 1: palettegen
    void convertToGifRender(int fps, const QString &paletteUse); // pass 2: paletteuse
    void convertVideo();
    void stopProcess(QProcess *&process);
    void cleanup();
    void fail(const QString &msg);

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

    State m_state = Idle;
    Output m_output = Gif;
    SourceType m_source = Screen;
    bool m_hasAudio = false; // pulse audio captured into the temp (video only)
    int m_fps = 15;
    qint64 m_framesWritten = 0; // wall-clock pacing (see sampleFrame)
    QRect m_crop;
    QRect m_encodeCrop;
    QSize m_streamSize;
    QSize m_encodeSize;
    QPointer<QScreen> m_targetScreen;
    bool m_monitorRetryDone = false; // one wrong-monitor self-heal per start()
    QByteArray m_lastFrame;
    quint64 m_lastSampledSeq = 0; // grabber seq of m_lastFrame (skip re-crop when unchanged)
    QString m_tempPath;
    QString m_palettePath;
    QString m_outPath;
    QString m_audioFifoPath;
    QString m_replayDir;
    QString m_replaySnapshotDir;
    QString m_replayExportPath;
    QProcess *m_replayExporter = nullptr;
};

#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <QMutex>
#include <QByteArray>
#include <QVector>
#include <QImage>
#include <QPoint>
#include <QHash>
#include <atomic>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

// One captured cursor position, in the PipeWire stream's own pixel space and
// clock. Plain value struct — travels out via takeCursorSamples(), never a
// signal, so it needs no Q_DECLARE_METATYPE. `shapeId` is the spa_meta_cursor
// id of the bitmap worn at this sample (0 = unknown); pair it with
// cursorShapeChanged().
struct CursorSample {
    qint64 tMonoNs = 0;   // CLOCK_MONOTONIC ns
    double x = 0.0;       // stream pixels
    double y = 0.0;       // stream pixels
    bool visible = true;
    int shapeId = 0;      // spa_meta_cursor id; 0 = no/unknown shape this sample
};

// Consumes a portal ScreenCast PipeWire stream (SHM buffers, BGRx/BGRA)
// on PipeWire's own thread and keeps the most recent frame; the recorder
// samples it at a fixed FPS (sample-and-hold gives GIF a constant rate).
//
// Cursor-metadata capture (opt in with start(..., wantCursorMeta=true), and
// only meaningful after ScreenCastSession negotiated CursorMetadata — in any
// other cursor mode no cursor meta is attached to the buffers).
class PipeWireGrabber : public QObject
{
    Q_OBJECT
public:
    explicit PipeWireGrabber(QObject *parent = nullptr);
    ~PipeWireGrabber() override;

    // wantCursorMeta requests SPA_META_Header + SPA_META_Cursor on the buffers
    // and enables the cursor sampling / shape path. Defaulted off: existing
    // callers are byte-for-byte unaffected.
    bool start(int pipewireFd, uint nodeId, int maxFps, bool wantCursorMeta = false);
    void stop();

    // ffmpeg rawvideo pix_fmt for the negotiated byte order (valid after
    // formatReady): frames are kept in native order, not swizzled to BGRA.
    QString pixelFormat() const;

    // Hands out the latest frame (tightly packed in pixelFormat() and the
    // negotiated size) as a cheap implicitly-shared reference. Returns false if
    // none arrived yet.
    // `seq` (same mutex, so atomic with the frame) increments once per new
    // stream frame — compositor streams are damage-driven, so on a static
    // screen it lets the sampler skip re-cropping an unchanged frame.
    bool latestFrame(QByteArray &out, quint64 *seq = nullptr);

    // Drains all cursor samples captured since the last call (empty when
    // wantCursorMeta was false). Call from the GUI/recorder thread.
    QVector<CursorSample> takeCursorSamples();

signals:
    void formatReady(const QSize &size);
    void streamError(const QString &message);
    // A cursor shape (identified by spa_meta_cursor id) was seen for the first
    // time: `image` is a deep-copied RGBA bitmap, `hotspot` its click point.
    // Emitted from the PipeWire thread — connect QUEUED (or Auto).
    void cursorShapeChanged(int id, const QImage &image, const QPoint &hotspot);

public: // called from PipeWire C callbacks
    void onParamChanged(uint32_t id, const void *param);
    void onProcess();

private:

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_stream *m_stream = nullptr;
    void *m_listener = nullptr;

    QMutex m_mutex;
    QByteArray m_latest;   // front buffer: replaced by assignment only, never written in place
    QByteArray m_pool[3];  // rotating back buffers: PipeWire thread only
    quint64 m_seq = 0;     // frame sequence, guarded by m_mutex
    std::atomic<bool> m_haveFrame{false};
    QSize m_size;
    uint32_t m_format = 0;

    // Cursor-metadata path. m_cursorSamples is drained by takeCursorSamples();
    // the rest below is PipeWire-thread-only state.
    bool m_wantCursorMeta = false;
    QVector<CursorSample> m_cursorSamples;   // guarded by m_mutex
    bool m_cursorOverflowWarned = false;     // guarded by m_mutex
    double m_lastCursorX = 0.0;              // PipeWire thread only: last visible position,
    double m_lastCursorY = 0.0;              //   carried into hidden samples so interpolation holds
};

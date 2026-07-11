#pragma once
#include <QObject>
#include <QSize>
#include <QString>
#include <QMutex>
#include <QByteArray>
#include <atomic>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

// Consumes a portal ScreenCast PipeWire stream (SHM buffers, BGRx/BGRA)
// on PipeWire's own thread and keeps the most recent frame; the recorder
// samples it at a fixed FPS (sample-and-hold gives GIF a constant rate).
class PipeWireGrabber : public QObject
{
    Q_OBJECT
public:
    explicit PipeWireGrabber(QObject *parent = nullptr);
    ~PipeWireGrabber() override;

    bool start(int pipewireFd, uint nodeId, int maxFps);
    void stop();

    // Negotiated stream size (valid after formatReady).
    QSize frameSize() const { return m_size; }

    // ffmpeg rawvideo pix_fmt for the negotiated byte order (valid after
    // formatReady): frames are kept in native order, not swizzled to BGRA.
    QString pixelFormat() const;

    // Hands out the latest frame (tightly packed in pixelFormat(), frameSize())
    // as a cheap implicitly-shared reference. Returns false if none arrived yet.
    // `seq` (same mutex, so atomic with the frame) increments once per new
    // stream frame — compositor streams are damage-driven, so on a static
    // screen it lets the sampler skip re-cropping an unchanged frame.
    bool latestFrame(QByteArray &out, quint64 *seq = nullptr);

signals:
    void formatReady(const QSize &size);
    void streamError(const QString &message);

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
};

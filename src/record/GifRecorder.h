#pragma once
#include <QObject>
#include <QRect>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QPointer>

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
    enum Output { Gif, Mp4, WebM };
    enum SourceType { Screen, Region, Window };

    explicit GifRecorder(Settings *settings, QObject *parent = nullptr);
    ~GifRecorder() override;

    bool recording() const { return m_state != Idle; }
    int elapsedSeconds() const;

    // cropPhysical: region in stream (physical) pixels; empty = full stream.
    // For Window source the portal picks the window; crop is ignored.
    void start(Output output, SourceType source = Screen, const QRect &cropPhysical = {},
               QScreen *screen = nullptr);
    void stop();     // finalize -> converting -> finished()
    void abort();    // discard everything

signals:
    void started();
    void converting();
    void finished(const QString &filePath);
    void failed(const QString &error);
    void elapsedChanged();

private:
    enum State { Idle, Starting, Recording, Converting };

    void onStreamReady(int fd, uint nodeId, const QSize &size, const QPoint &pos);
    void beginEncoding(const QSize &streamSize);
    void sampleFrame();
    void convertToGif();
    void convertVideo();
    void stopProcess(QProcess *&process);
    void cleanup();
    void fail(const QString &msg);

    Settings *m_settings;
    ScreenCastSession *m_session = nullptr;
    PipeWireGrabber *m_grabber = nullptr;
    QProcess *m_ffmpeg = nullptr;
    QProcess *m_converter = nullptr;
    QTimer m_sampler;
    QTimer m_maxTimer;
    QElapsedTimer m_elapsed;
    QTimer m_elapsedTick;

    State m_state = Idle;
    Output m_output = Gif;
    SourceType m_source = Screen;
    QRect m_crop;
    QRect m_encodeCrop;
    QSize m_streamSize;
    QSize m_encodeSize;
    QPointer<QScreen> m_targetScreen;
    QByteArray m_lastFrame;
    QString m_tempPath;
    QString m_outPath;
};

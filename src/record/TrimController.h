#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <qqmlregistration.h>

class QProcess;

// Per-window backend for the trim editor. Both of its jobs exist so the window
// can show what the saved file will really contain:
//
//  - the filmstrip: ONE ffmpeg pass renders N evenly spaced frames into a single
//    tiled PNG (`fps=N/duration,scale=-1:h,tile=Nx1`) that the timeline slices
//    with Image.sourceClipRect — one process and one decode instead of N, and no
//    regeneration when the window is resized (the strip is over-sampled and the
//    timeline picks the tile nearest each cell's time).
//  - the keyframe table: a stream-copy cut can only START on a keyframe, so the
//    lossless mode snaps the in-point onto this list and draws it as ticks.
//    Probed from packet flags (demux only, no decoding) and only when the user
//    actually turns lossless on.
class TrimController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppContext")
    Q_PROPERTY(QString sourcePath READ sourcePath CONSTANT)
    Q_PROPERTY(qreal duration READ duration CONSTANT)
    // GIF has no keyframes to cut on and no stream-copy path — it is always
    // re-rendered through palettegen/paletteuse.
    Q_PROPERTY(bool gif READ gif CONSTANT)
    Q_PROPERTY(QString filmstrip READ filmstrip NOTIFY filmstripChanged)
    Q_PROPERTY(int filmstripTiles READ filmstripTiles NOTIFY filmstripChanged)
    Q_PROPERTY(int filmstripState READ filmstripState NOTIFY filmstripChanged)
    Q_PROPERTY(QVariantList keyframes READ keyframes NOTIFY keyframesChanged)
    Q_PROPERTY(int keyframeState READ keyframeState NOTIFY keyframesChanged)

public:
    enum ProbeState { Idle, Busy, Ready, Failed };
    Q_ENUM(ProbeState)

    TrimController(const QString &path, qreal duration, QObject *parent = nullptr);
    ~TrimController() override;

    QString sourcePath() const { return m_path; }
    qreal duration() const { return m_duration; }
    bool gif() const { return m_gif; }
    QString filmstrip() const { return m_stripUrl; }
    int filmstripTiles() const { return m_tiles; }
    int filmstripState() const { return m_stripState; }
    QVariantList keyframes() const { return m_keyframes; }
    int keyframeState() const { return m_keyframeState; }

    // Render the tiled strip. No-op once one exists or while one is rendering.
    Q_INVOKABLE void buildFilmstrip(int tiles, int tileHeight);
    // Probe the video keyframe timestamps. No-op for GIF, or once loaded.
    Q_INVOKABLE void loadKeyframes();
    // The keyframe at or before t — where a stream-copy cut would really start.
    // Snapping backwards (never forwards) can only keep frames the user asked
    // for; falls back to t when the table is empty or still loading.
    Q_INVOKABLE qreal snapStart(qreal t) const;

signals:
    void filmstripChanged();
    void keyframesChanged();

private:
    void setFilmstripState(int state);
    void setKeyframeState(int state);

    QString m_path;
    qreal m_duration = 0;
    bool m_gif = false;

    QString m_stripPath;      // temp PNG, removed with this object
    QString m_stripUrl;
    int m_tiles = 0;
    int m_stripState = Idle;
    QPointer<QProcess> m_stripProc;

    QVariantList m_keyframes; // seconds, ascending
    int m_keyframeState = Idle;
    QPointer<QProcess> m_keyframeProc;
};

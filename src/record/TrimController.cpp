#include "TrimController.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QtGlobal>
#include <algorithm>

TrimController::TrimController(const QString &path, qreal duration, qreal frameDuration,
                               QObject *parent)
    : QObject(parent), m_path(path), m_duration(duration), m_frameDuration(frameDuration)
{
    m_gif = QFileInfo(path).suffix().compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0;
}

TrimController::~TrimController()
{
    // The strip is scratch: kill a render still in flight, then drop the file.
    if (m_stripProc) {
        m_stripProc->disconnect(this);
        m_stripProc->kill();
        m_stripProc->waitForFinished(1000);
    }
    if (m_keyframeProc) {
        m_keyframeProc->disconnect(this);
        m_keyframeProc->kill();
        m_keyframeProc->waitForFinished(1000);
    }
    if (!m_stripPath.isEmpty())
        QFile::remove(m_stripPath);
}

void TrimController::setFilmstripState(int state)
{
    if (m_stripState == state)
        return;
    m_stripState = state;
    emit filmstripChanged();
}

void TrimController::setKeyframeState(int state)
{
    if (m_keyframeState == state)
        return;
    m_keyframeState = state;
    emit keyframesChanged();
}

void TrimController::buildFilmstrip(int tiles, int tileHeight)
{
    if (m_stripState == Busy || !m_stripUrl.isEmpty() || m_duration <= 0)
        return;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        setFilmstripState(Failed);
        return;
    }
    m_tiles = qBound(8, tiles, 96);
    const int height = qBound(32, tileHeight, 160);

    // One file per controller: QML's image cache is keyed by URL, so a reused
    // name would hand a second trim window the first one's strip.
    static quint32 serial = 0;
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    m_stripPath = QDir(cache).filePath(QStringLiteral("trim-strip-%1-%2.png")
                                       .arg(QCoreApplication::applicationPid()).arg(++serial));

    // fps = tiles/duration samples the clip evenly; tile= pads the last cell if
    // rounding lands one frame short, and -frames:v 1 drops a stray extra tile.
    const QString vf = QStringLiteral("fps=%1,scale=-1:%2,tile=%3x1")
                           .arg(QString::number(m_tiles / m_duration, 'f', 6))
                           .arg(height)
                           .arg(m_tiles);
    auto *proc = new QProcess(this);
    m_stripProc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        if (m_stripProc != proc)
            return;
        m_stripProc = nullptr;
        proc->deleteLater();
        if (code != 0 || status != QProcess::NormalExit || !QFileInfo::exists(m_stripPath)) {
            setFilmstripState(Failed);
            return;
        }
        m_stripUrl = QUrl::fromLocalFile(m_stripPath).toString();
        setFilmstripState(Ready);
        emit filmstripChanged();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (m_stripProc != proc || error != QProcess::FailedToStart)
            return;
        m_stripProc = nullptr;
        proc->deleteLater();
        setFilmstripState(Failed);
    });
    setFilmstripState(Busy);
    proc->start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-nostats"),
                         QStringLiteral("-loglevel"), QStringLiteral("error"),
                         QStringLiteral("-i"), m_path,
                         QStringLiteral("-vf"), vf,
                         QStringLiteral("-frames:v"), QStringLiteral("1"),
                         m_stripPath});
}

void TrimController::loadKeyframes()
{
    if (m_gif || m_keyframeState == Busy || m_keyframeState == Ready)
        return;
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty()) {
        setKeyframeState(Failed);
        return;
    }
    auto *proc = new QProcess(this);
    m_keyframeProc = proc;
    connect(proc, &QProcess::finished, this, [this, proc](int code, QProcess::ExitStatus status) {
        if (m_keyframeProc != proc)
            return;
        m_keyframeProc = nullptr;
        const QByteArray out = proc->readAllStandardOutput();
        proc->deleteLater();
        if (code != 0 || status != QProcess::NormalExit) {
            setKeyframeState(Failed);
            return;
        }
        m_keyframes.clear();
        // Sectioned CSV (csv=p=1): "packet,<pts_time>,<flags>" per packet — K
        // marks a keyframe — plus one "format,<start_time>" line. Reading flags
        // off packets never decodes anything. pts_time is ABSOLUTE: media with
        // a non-zero container start time (e.g. some phone/OBS files) number
        // their packets from start_time, while the window's timeline and
        // ffmpeg's input -ss are relative to the start of the file — so every
        // keyframe is rebased by subtracting start_time before use.
        qreal startTime = 0;
        QVector<qreal> raw;
        const QList<QByteArray> lines = out.split('\n');
        for (const QByteArray &line : lines) {
            const QList<QByteArray> cols = line.trimmed().split(',');
            if (cols.size() >= 2 && cols[0] == "format") {
                bool ok = false;
                const qreal s = cols[1].toDouble(&ok);   // "N/A" fails → keep 0
                if (ok)
                    startTime = s;
            } else if (cols.size() >= 3 && cols[0] == "packet"
                       && cols[2].trimmed().startsWith('K')) {
                bool ok = false;
                const qreal t = cols[1].toDouble(&ok);
                if (ok)
                    raw.append(t);
            }
        }
        for (const qreal t : raw) {
            const qreal rel = t - startTime;
            if (rel >= -0.001)
                m_keyframes.append(qMax<qreal>(0, rel));
        }
        std::sort(m_keyframes.begin(), m_keyframes.end(),
                  [](const QVariant &a, const QVariant &b) { return a.toDouble() < b.toDouble(); });
        setKeyframeState(m_keyframes.isEmpty() ? Failed : Ready);
        emit keyframesChanged();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (m_keyframeProc != proc || error != QProcess::FailedToStart)
            return;
        m_keyframeProc = nullptr;
        proc->deleteLater();
        setKeyframeState(Failed);
    });
    setKeyframeState(Busy);
    proc->start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                          QStringLiteral("-show_entries"),
                          QStringLiteral("packet=pts_time,flags:format=start_time"),
                          QStringLiteral("-of"), QStringLiteral("csv=p=1"),
                          m_path});
}

qreal TrimController::snapStart(qreal t) const
{
    if (m_keyframeState != Ready || m_keyframes.isEmpty())
        return t;
    qreal best = m_keyframes.first().toDouble();
    for (const QVariant &v : m_keyframes) {
        const qreal k = v.toDouble();
        if (k > t + 0.001)
            break;
        best = k;
    }
    return qBound(qreal(0), best, m_duration);
}

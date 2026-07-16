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

TrimController::TrimController(const QString &path, qreal duration, QObject *parent)
    : QObject(parent), m_path(path), m_duration(duration)
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
        // Lines are "<pts_time>,<flags>", e.g. "8.333333,K__" — K marks a
        // keyframe packet. Reading flags off packets never decodes anything.
        const QList<QByteArray> lines = out.split('\n');
        for (const QByteArray &line : lines) {
            const int comma = line.indexOf(',');
            if (comma <= 0 || !line.mid(comma + 1).trimmed().startsWith('K'))
                continue;
            bool ok = false;
            const qreal t = line.left(comma).toDouble(&ok);
            if (ok && t >= 0)
                m_keyframes.append(t);
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
                          QStringLiteral("-show_entries"), QStringLiteral("packet=pts_time,flags"),
                          QStringLiteral("-of"), QStringLiteral("csv=p=0"),
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

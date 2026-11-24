#include "MicMonitor.h"
#include "AudioCapture.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

MicMonitor::MicMonitor(QObject *parent) : QObject(parent), m_restart(new QTimer(this))
{
    // A slider drag changes the volume many times a second; restart once it rests.
    m_restart->setSingleShot(true);
    m_restart->setInterval(300);
    connect(m_restart, &QTimer::timeout, this, [this] {
        if (!running())
            return;
        stop();
        start();
    });
}

MicMonitor::~MicMonitor()
{
    stop();
}

bool MicMonitor::available() const
{
    return !AudioCapture::monitorArgs(QString(), 1.0).isEmpty();
}

void MicMonitor::setDevice(const QString &device)
{
    if (device == m_device)
        return;
    m_device = device;
    emit deviceChanged();
    restartSoon();
}

void MicMonitor::setVolume(int volume)
{
    volume = qBound(0, volume, 200);
    if (volume == m_volume)
        return;
    m_volume = volume;
    emit volumeChanged();
    restartSoon();
}

void MicMonitor::restartSoon()
{
    if (running())
        m_restart->start();
}

void MicMonitor::start()
{
    if (m_proc)
        return;
    const QStringList args = AudioCapture::monitorArgs(m_device, m_volume / 100.0);
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (args.isEmpty() || ffmpeg.isEmpty()) {
        m_error = tr("ffmpeg could not be found");
        emit runningChanged();
        return;
    }
    m_error.clear();
    auto *proc = new QProcess(this);
    m_proc = proc;
    // Only an ffmpeg that dies on its own reports here; stop() disconnects first.
    const auto ended = [this, proc](const QString &why) {
        if (m_proc != proc)
            return;
        // The first line is the cause ("[in#0 @ 0x...] Error opening input: ..."),
        // the rest is ffmpeg unwinding.
        QString first = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed()
                            .section(QLatin1Char('\n'), 0, 0);
        first.remove(QRegularExpression(QStringLiteral("^\\[[^\\]]*\\]\\s*")));
        m_error = first.isEmpty() ? why : first.left(200);
        m_proc = nullptr;
        proc->deleteLater();
        emit runningChanged();
    };
    connect(proc, &QProcess::finished, this, [ended] { ended(tr("The microphone stopped")); });
    connect(proc, &QProcess::errorOccurred, this, [ended](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            ended(tr("ffmpeg could not be started"));
    });
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->start(ffmpeg, args);
    emit runningChanged();
}

void MicMonitor::stop()
{
    m_restart->stop();
    QProcess *proc = m_proc;
    if (!proc)
        return;
    m_proc = nullptr;
    proc->disconnect(this);
    proc->kill();
    proc->waitForFinished(1000);
    proc->deleteLater();
    emit runningChanged();
}

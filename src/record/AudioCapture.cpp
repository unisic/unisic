#include "AudioCapture.h"
#include "PwDump.h"
#include "media/FfmpegUtil.h"

#include <QStandardPaths>

struct AudioCapture::Private {
    QString device; // the pulse source ffmpeg opens
};

AudioCapture::AudioCapture(std::unique_ptr<Private> p) : d(std::move(p)) {}
AudioCapture::~AudioCapture() = default;

std::unique_ptr<AudioCapture> AudioCapture::open(Source source, const QString &device,
                                                 QString *)
{
    if (source == Source::Application)
        return nullptr; // never asked for: applicationTargets() is empty here
    auto p = std::make_unique<Private>();
    if (source == Source::System) {
        p->device = QStringLiteral("@DEFAULT_MONITOR@");
    } else {
        // A chosen source is its pipewire-pulse node.name; empty means the
        // system default input. A stale name (unplugged mic) makes ffmpeg
        // fail with a visible error rather than silently recording nothing.
        const QString mic = device.trimmed();
        p->device = mic.isEmpty() ? QStringLiteral("default") : mic;
    }
    return std::unique_ptr<AudioCapture>(new AudioCapture(std::move(p)));
}

QStringList AudioCapture::ffmpegInput() const
{
    return QStringList{QStringLiteral("-f"), QStringLiteral("pulse")}
           << FfmpegUtil::inputThreadQueueSize(1024) << QStringLiteral("-i") << d->device;
}

QStringList AudioCapture::monitorArgs(const QString &device, double gain)
{
    const QString mic = device.trimmed();
    // Small pulse buffers on both sides (10 ms in, 40 ms out) to keep the
    // echo short; the defaults leave the size to the server.
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
            QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-fflags"), QStringLiteral("nobuffer"),
            QStringLiteral("-f"), QStringLiteral("pulse"),
            QStringLiteral("-fragment_size"), QStringLiteral("1920"),
            QStringLiteral("-name"), QStringLiteral("Unisic"),
            QStringLiteral("-i"), mic.isEmpty() ? QStringLiteral("default") : mic,
            QStringLiteral("-af"), QStringLiteral("volume=%1").arg(QString::number(gain, 'f', 2)),
            QStringLiteral("-f"), QStringLiteral("pulse"),
            QStringLiteral("-buffer_duration"), QStringLiteral("40"),
            QStringLiteral("-name"), QStringLiteral("Unisic"),
            QStringLiteral("Microphone monitor")};
}

bool AudioCapture::inputListAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty();
}

QVariantList AudioCapture::inputDevices()
{
    return PwDump::inputDevices(PwDump::nodes());
}

bool AudioCapture::applicationListAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("pw-record")).isEmpty()
           && !QStandardPaths::findExecutable(QStringLiteral("pw-dump")).isEmpty();
}

QVariantList AudioCapture::applications()
{
    return PwDump::applications(PwDump::appStreams(PwDump::nodes()));
}

QList<AudioCapture::AppTarget> AudioCapture::applicationTargets(const QStringList &)
{
    return {};
}

QString AudioCapture::backend()
{
    return QStringLiteral("pipewire");
}

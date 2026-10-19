#include "AudioCapture.h"
#include "PwDump.h"

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
    return {QStringLiteral("-f"), QStringLiteral("pulse"),
            QStringLiteral("-thread_queue_size"), QStringLiteral("1024"),
            QStringLiteral("-i"), d->device};
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

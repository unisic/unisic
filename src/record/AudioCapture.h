#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

// One audio source of a video recording, as the ffmpeg input that reads it.
// Linux (AudioCapture.cpp) hands ffmpeg its own pulse input, so an object owns
// nothing. Windows (AudioCapture_win.cpp) records the endpoint through WASAPI
// on a thread of its own and writes raw PCM into a named pipe that ffmpeg
// reads, because the ffmpeg there has no input that reads a playback device.
//
// Keep the object alive until the ffmpeg reading it has exited: on Windows the
// audio ends when the object does, and ffmpeg would cut the file at that point
// (-shortest).
class AudioCapture
{
public:
    // Application: what one process tree plays (Windows only, see
    // applicationTargets()).
    enum class Source { System, Microphone, Application };

    // device: for Microphone, an id from inputDevices(), empty for the system
    // default input; for Application, an AppTarget::device; ignored for
    // System. nullptr with *error set when the source cannot be opened (a
    // microphone that is gone, no output device).
    static std::unique_ptr<AudioCapture> open(Source source, const QString &device,
                                              QString *error);
    ~AudioCapture();

    // ffmpeg input options, ending with "-i <url>".
    QStringList ffmpegInput() const;

    // Whether inputDevices() can list anything here.
    static bool inputListAvailable();
    // Capture-capable inputs as {id, label} maps. Blocking: run it off the
    // GUI thread (AppContext does).
    static QVariantList inputDevices();

    // Whether applications() can list anything and a recording can capture it.
    static bool applicationListAvailable();
    // Applications playing audio, as the picker model {id, label, icon,
    // streams}; id is what Settings::recordAppAudioApps stores. Blocking, like
    // inputDevices().
    static QVariantList applications();

    struct AppTarget {
        QString device; // for open(Source::Application, ...)
        QString label;  // the track title
    };
    // The Source::Application captures that record `apps` (application ids),
    // resolved now. Empty on Linux, where GifRecorder runs one pw-record per
    // stream itself.
    static QList<AppTarget> applicationTargets(const QStringList &apps);

    // The audio stack, so the UI can word its hints for it: "pipewire" or
    // "wasapi".
    static QString backend();

private:
    struct Private;
    explicit AudioCapture(std::unique_ptr<Private> d);
    std::unique_ptr<Private> d;
};

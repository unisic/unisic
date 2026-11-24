#pragma once
#include <QObject>
#include <QString>
#include <qqmlregistration.h>

class QProcess;
class QTimer;

// "Listen to yourself" in Settings > Clips: plays the microphone back through
// the default output at the clip microphone volume, so the user hears what a
// clip will record. One ffmpeg (AudioCapture::monitorArgs) while it runs,
// nothing otherwise. The QML item owns it, so leaving the page stops it.
class MicMonitor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY runningChanged)
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
public:
    explicit MicMonitor(QObject *parent = nullptr);
    ~MicMonitor() override;

    bool available() const;
    bool running() const { return m_proc != nullptr; }
    QString error() const { return m_error; }
    QString device() const { return m_device; }
    void setDevice(const QString &device);
    int volume() const { return m_volume; }
    void setVolume(int volume);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void runningChanged();
    void deviceChanged();
    void volumeChanged();

private:
    void restartSoon();

    QProcess *m_proc = nullptr;
    QTimer *m_restart = nullptr;
    QString m_device;
    QString m_error;
    int m_volume = 100;
};

#pragma once
#include <QObject>
#include <QImage>
#include <QRect>
#include <functional>
#include <qqmlregistration.h>

#include "Settings.h"
#include "upload/UploadManager.h"
#include "history/HistoryStore.h"
#include "record/GifRecorder.h"

class QQmlEngine;
class QSystemTrayIcon;
class CaptureManager;
class OverlayController;
class GlobalHotkeys;
class GifRecorder;

// Application facade exposed to QML as the "App" context property.
// Owns every subsystem and implements the after-capture pipeline
// (save / clipboard / editor / upload / history).
class AppContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Context property")

    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(UploadManager *uploads READ uploads CONSTANT)
    Q_PROPERTY(HistoryStore *history READ history CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool converting READ converting NOTIFY recordingChanged)
    Q_PROPERTY(int recordSeconds READ recordSeconds NOTIFY recordSecondsChanged)
    Q_PROPERTY(bool recordingAvailable READ recordingAvailable CONSTANT)
    Q_PROPERTY(QString toastText READ toastText NOTIFY toastChanged)

public:
    using UploadDone = std::function<void(const QString &url, const QString &error)>;

    explicit AppContext(QObject *parent = nullptr);
    ~AppContext() override;

    void initialize(QQmlEngine *engine);

    Settings *settings() const { return m_settings; }
    UploadManager *uploads() const { return m_uploads; }
    HistoryStore *history() const { return m_history; }
    CaptureManager *captureManager() const { return m_capture; }
    QQmlEngine *qmlEngine() const { return m_engine; }

    bool recording() const;
    bool converting() const;
    int recordSeconds() const;
    bool recordingAvailable() const;
    QString toastText() const { return m_toast; }

    // Capture entry points (also bound to hotkeys and tray).
    Q_INVOKABLE void captureFullScreen();
    Q_INVOKABLE void captureRegion();
    Q_INVOKABLE void captureWindow();
    Q_INVOKABLE void startGifRegion();
    Q_INVOKABLE void startGifFullScreen();
    Q_INVOKABLE void startVideoScreen();
    Q_INVOKABLE void startVideoRegion();
    Q_INVOKABLE void startVideoWindow();
    Q_INVOKABLE void stopRecording();

    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openDirectory(const QString &path);
    Q_INVOKABLE void showToast(const QString &text);
    Q_INVOKABLE void applyHotkeys();
    Q_INVOKABLE QString exportSettings(const QUrl &file);   // "" on success, else error
    Q_INVOKABLE QString importSettings(const QUrl &file);
    Q_INVOKABLE QString filenamePreview() const;

    // Used by EditorSession.
    QString saveImageAuto(const QImage &img);
    QString saveImageTo(const QImage &img, const QString &dir);
    void copyImageToClipboard(const QImage &img);
    void uploadImage(const QImage &img, UploadDone done);

signals:
    void recordingChanged();
    void recordSecondsChanged();
    void toastChanged();
    void showMainWindowRequested();

private:
    void finishCapture(const QImage &img);
    void openEditor(const QImage &img);
    void setupTray();
    void defineHotkeys();
    void onRecordingFinished(const QString &path);
    void withDelay(std::function<void()> fn);
    QString makeFileName() const;                    // template + extension
    QByteArray encodeImage(const QImage &img, QString *mime) const;
    void afterUploadActions(const QString &url);
    GifRecorder::Output videoOutput() const;

    QQmlEngine *m_engine = nullptr;
    Settings *m_settings;
    CaptureManager *m_capture;
    OverlayController *m_overlay;
    UploadManager *m_uploads;
    HistoryStore *m_history;
    GlobalHotkeys *m_hotkeys;
    GifRecorder *m_recorder;
    QSystemTrayIcon *m_tray = nullptr;
    QString m_toast;
    bool m_converting = false;
};

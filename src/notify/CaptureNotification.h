#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <qqmlregistration.h>

class AppContext;

// Backing object for one in-app capture-preview popup (NotificationPopup.qml).
// Holds the captured image plus its saved path and forwards the popup's action
// buttons to AppContext. Created by AppContext; it and its window self-destruct
// when the window hides.
class CaptureNotification : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppContext")

    Q_PROPERTY(QString thumbSource READ thumbSource CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString filePath READ filePath NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY stateChanged)
    Q_PROPERTY(QString url READ url NOTIFY stateChanged)
    Q_PROPERTY(bool uploading READ uploading NOTIFY stateChanged)

public:
    CaptureNotification(AppContext *app, const QImage &img, const QString &filePath,
                        const QString &kind, QObject *parent = nullptr);
    ~CaptureNotification() override;

    QString thumbSource() const { return m_thumbSource; }
    QString kind() const { return m_kind; }
    QString filePath() const { return m_filePath; }
    QString fileName() const;
    QString url() const { return m_url; }
    bool uploading() const { return m_uploading; }
    QString kindText() const { return m_kind; }
    QString thumbFilePath() const { return m_thumbFile; } // local path for the notification image

    void setUrl(const QString &url);
    void setUploading(bool on);

    Q_INVOKABLE void edit();
    Q_INVOKABLE void preview();       // open the floating pinnable preview window
    Q_INVOKABLE void copyImage();
    Q_INVOKABLE void copyUrl();
    Q_INVOKABLE void showInFolder();
    Q_INVOKABLE void openCapture();   // open the saved file (saving first if needed)
    Q_INVOKABLE void save();
    Q_INVOKABLE void upload();
    Q_INVOKABLE void deleteCapture();
    Q_INVOKABLE void ocr();
    Q_INVOKABLE void dismiss();

signals:
    void stateChanged();
    void closeRequested();

private:
    AppContext *m_app;
    QImage m_image;
    QString m_filePath;
    QString m_kind;
    QString m_url;
    QString m_thumbSource;
    QString m_thumbFile;    // cached on-disk thumbnail, removed in the destructor
    bool m_uploading = false;
};

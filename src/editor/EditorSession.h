#pragma once
#include <QObject>
#include <QImage>
#include <qqmlregistration.h>

class AnnotationCanvas;
class AppContext;

// One post-capture editing session == one editor window. Owns the captured
// image and performs the export actions (save / copy / upload) on the
// composited result of its AnnotationCanvas.
class EditorSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by AppContext")

    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString lastSavedPath READ lastSavedPath NOTIFY statusTextChanged)

public:
    explicit EditorSession(AppContext *app, const QImage &image, QObject *parent = nullptr);

    QImage image() const { return m_image; }
    QString statusText() const { return m_status; }
    QString lastSavedPath() const { return m_lastSavedPath; }

    Q_INVOKABLE void bindCanvas(AnnotationCanvas *canvas);
    Q_INVOKABLE QString save();               // returns saved path ("" on failure)
    Q_INVOKABLE QString saveAs(const QUrl &dir);
    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE void upload();
    Q_INVOKABLE void ocrCopyText();           // OCR the composited image, copy text

signals:
    void statusTextChanged();

private:
    QImage composited() const;
    void setStatus(const QString &s);

    AppContext *m_app;
    QImage m_image;
    AnnotationCanvas *m_canvas = nullptr;
    QString m_status;
    QString m_lastSavedPath;
};

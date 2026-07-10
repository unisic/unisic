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
    // True when editing an existing capture from history: save() OVERWRITES that
    // file instead of creating a new one. The UI shows an overwrite confirmation.
    Q_PROPERTY(bool overwriteMode READ overwriteMode CONSTANT)

public:
    explicit EditorSession(AppContext *app, const QImage &image,
                           const QString &overwritePath = {}, QObject *parent = nullptr);

    QImage image() const { return m_image; }
    QString statusText() const { return m_status; }
    QString lastSavedPath() const { return m_lastSavedPath; }
    bool overwriteMode() const { return !m_overwritePath.isEmpty(); }

    Q_INVOKABLE void bindCanvas(AnnotationCanvas *canvas);
    Q_INVOKABLE QString save();               // returns saved path ("" on failure)
    Q_INVOKABLE QString saveAs(const QUrl &dir);
    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE void upload();
    Q_INVOKABLE void ocrCopyText();           // OCR the composited image, copy text
    // Enter the canvas's OCR text-pick mode: recognize words asynchronously and
    // hand them to the canvas so the user can select + copy a subset.
    Q_INVOKABLE void startOcrPick();
    Q_INVOKABLE void copyOcrSelection();      // copy the canvas's selected words
    // Remove the background from the whole image (U-2-Net) → transparent PNG.
    Q_INVOKABLE void removeBackground();

signals:
    void statusTextChanged();

private:
    QImage composited() const;
    void setStatus(const QString &s);

    AppContext *m_app;
    QImage m_image;
    QString m_overwritePath;       // non-empty when editing an existing file
    AnnotationCanvas *m_canvas = nullptr;
    QString m_status;
    QString m_lastSavedPath;
};

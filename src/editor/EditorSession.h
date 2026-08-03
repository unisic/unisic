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
    // True when editing an existing capture from history: save() OVERWRITES that
    // file instead of creating a new one. The UI shows an overwrite confirmation.
    Q_PROPERTY(bool overwriteMode READ overwriteMode CONSTANT)

public:
    explicit EditorSession(AppContext *app, const QImage &image,
                           const QString &overwritePath = {}, quint64 historyId = 0,
                           QObject *parent = nullptr);

    QString statusText() const { return m_status; }
    bool overwriteMode() const { return !m_overwritePath.isEmpty(); }

    Q_INVOKABLE void bindCanvas(AnnotationCanvas *canvas);
    Q_INVOKABLE QString save();               // returns saved path ("" on failure)
    // One-off save in `format` (png/jpg/webp/gif) whatever Settings says, and
    // always into a NEW file: a different format means a different extension,
    // so overwriting is not on the table even in overwrite mode.
    Q_INVOKABLE QString saveAs(const QString &format);
    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE void upload();
    Q_INVOKABLE void ocrCopyText();           // OCR the composited image, copy text
    // Enter the canvas's OCR text-pick mode: recognize words asynchronously and
    // hand them to the canvas so the user can select + copy a subset.
    Q_INVOKABLE void startOcrPick();
    Q_INVOKABLE void copyOcrSelection();      // copy the canvas's selected words
    Q_INVOKABLE void highlightOcrSelection(); // make the selected text permanent
    Q_INVOKABLE void redactOcrSelection();    // black out the selected text permanently
    // Black out every recognized run matching `pattern` — no selection needed.
    Q_INVOKABLE void redactTextMatching(const QString &pattern);
    // Remove the background from the whole image (U-2-Net) → transparent PNG.

signals:
    void statusTextChanged();

private:
    QImage composited() const;
    void setStatus(const QString &s);

    AppContext *m_app;
    QImage m_image;
    QString m_overwritePath;       // non-empty when editing an existing file
    // The history entry this capture already owns (0 = none, e.g. a pasted or
    // dropped image). Save and Upload update THAT entry instead of adding a
    // second one, so one capture stays one tile.
    quint64 m_historyId = 0;
    AnnotationCanvas *m_canvas = nullptr;
    QString m_status;
};

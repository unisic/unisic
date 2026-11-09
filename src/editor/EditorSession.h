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
    // False for a status that reports an edit rather than an export, so it
    // does not count as exported for the discard prompt.
    Q_PROPERTY(bool statusExported READ statusExported NOTIFY statusTextChanged)
    // True when editing an existing capture from history: save() OVERWRITES that
    // file instead of creating a new one. The UI shows an overwrite confirmation.
    Q_PROPERTY(bool overwriteMode READ overwriteMode CONSTANT)
    // The file this picture is (or was last saved as), for the editor header;
    // empty while it exists nowhere on disk. Every save sets a status, so it
    // shares that signal.
    Q_PROPERTY(QString fileName READ fileName NOTIFY statusTextChanged)

public:
    explicit EditorSession(AppContext *app, const QImage &image,
                           const QString &overwritePath = {}, quint64 historyId = 0,
                           QObject *parent = nullptr);

    QString statusText() const { return m_status; }
    bool statusExported() const { return m_statusExported; }
    bool overwriteMode() const { return !m_overwritePath.isEmpty(); }
    QString fileName() const;

    Q_INVOKABLE void bindCanvas(AnnotationCanvas *canvas);
    Q_INVOKABLE QString save();               // returns saved path ("" on failure)
    // One-off save in `format` (png/jpg/webp/gif) whatever Settings says, and
    // always into a NEW file: a different format means a different extension,
    // so overwriting is not on the table even in overwrite mode.
    Q_INVOKABLE QString saveAs(const QString &format);
    // Prompts the user with a native file dialog (QFileDialog) to choose a destination path and format.
    Q_INVOKABLE QString saveAsDialog();
    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE void upload();
    Q_INVOKABLE void print();                 // the image as edited, through the print dialog
    Q_INVOKABLE void ocrCopyText();           // OCR the composited image, copy text
    // Native file picker, then the picture goes onto the canvas centred on the
    // given image point (a toast when it does not decode).
    Q_INVOKABLE void insertImageFile(qreal imgX, qreal imgY);
    // Enter the canvas's OCR text-pick mode: recognize words asynchronously and
    // hand them to the canvas so the user can select + copy a subset.
    Q_INVOKABLE void startOcrPick();
    Q_INVOKABLE void copyOcrSelection();      // copy the canvas's selected words
    Q_INVOKABLE void highlightOcrSelection(); // make the selected text permanent
    Q_INVOKABLE void redactOcrSelection();    // black out the selected text permanently
    // Auto-redact's scan: recognizes the text when OCR mode has no words yet
    // and reports what AnnotationCanvas::sensitiveCounts() finds, covering
    // nothing; the user then picks the kinds to cover. endSensitiveScan()
    // leaves OCR mode again when the scan was the one to enter it.
    Q_INVOKABLE void scanSensitive();
    Q_INVOKABLE void endSensitiveScan();
    // Cut the subject out (BackgroundRemover) as one undo step, annotations
    // baked in. Dropped with a status line if the picture changed meanwhile.
    Q_INVOKABLE void removeBackground();

signals:
    void statusTextChanged();

private:
    QImage composited() const;
    void setStatus(const QString &s, bool exported = true);
    void reportSensitive(const QString &ocrError);

    AppContext *m_app;
    QImage m_image;
    QString m_overwritePath;       // non-empty when editing an existing file
    // The history entry this capture already owns (0 = none, e.g. a pasted or
    // dropped image). Save and Upload update THAT entry instead of adding a
    // second one, so one capture stays one tile.
    quint64 m_historyId = 0;
    AnnotationCanvas *m_canvas = nullptr;
    QString m_status;
    bool m_statusExported = true;
    bool m_scanPending = false;     // report once startOcrPick's words land
    bool m_scanLeavesOcr = false;   // ...and leave the OCR mode it entered
    quint64 m_edits = 0;           // canvas history changes, a staleness guard
};

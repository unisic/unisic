#include "EditorSession.h"
#include "AnnotationCanvas.h"
#include "AppContext.h"
#include <QPointer>
#include <QUrl>
#include <QFileInfo>
#include <QVector>

EditorSession::EditorSession(AppContext *app, const QImage &image,
                             const QString &overwritePath, QObject *parent)
    : QObject(parent), m_app(app), m_image(image), m_overwritePath(overwritePath)
{
}

void EditorSession::bindCanvas(AnnotationCanvas *canvas)
{
    m_canvas = canvas;
    if (m_canvas) {
        m_canvas->setImage(m_image);
        // setImage converts to premultiplied ARGB32, detaching into a NEW
        // buffer — keeping the original here pinned TWO full-res copies for
        // the editor window's lifetime. Re-share the canvas's buffer instead
        // (pixel-identical; only the internal format differs).
        m_image = m_canvas->image();
    }
}

QImage EditorSession::composited() const
{
    return m_canvas ? m_canvas->rendered() : m_image;
}

void EditorSession::setStatus(const QString &s)
{
    m_status = s;
    emit statusTextChanged();
}

QString EditorSession::save()
{
    const QImage img = composited();
    // Editing an existing capture from history: overwrite the original file in
    // place and refresh its history thumbnail, rather than making a new file.
    if (!m_overwritePath.isEmpty()) {
        if (img.save(m_overwritePath)) {
            m_lastSavedPath = m_overwritePath;
            m_app->history()->refreshEntry(m_overwritePath, img);
            setStatus(tr("Saved (overwrote %1)").arg(QFileInfo(m_overwritePath).fileName()));
            return m_overwritePath;
        }
        setStatus(tr("Save failed"));
        return {};
    }
    const QString path = m_app->saveImageAuto(img);
    if (!path.isEmpty()) {
        m_lastSavedPath = path;
        m_app->history()->addEntry(path, img, QStringLiteral("image"));
        setStatus(tr("Saved to %1").arg(path));
    } else {
        setStatus(tr("Save failed"));
    }
    return path;
}

QString EditorSession::saveAs(const QUrl &dir)
{
    const QString path = m_app->saveImageTo(composited(), dir.toLocalFile());
    if (!path.isEmpty()) {
        m_lastSavedPath = path;
        setStatus(tr("Saved to %1").arg(path));
    }
    return path;
}

void EditorSession::copyToClipboard()
{
    m_app->copyImageToClipboard(composited());
    setStatus(tr("Copied to clipboard"));
}

void EditorSession::upload()
{
    setStatus(tr("Uploading…"));
    // The session dies with the editor window; the upload may outlive both.
    QPointer<EditorSession> self(this);
    m_app->uploadImage(composited(), [self](const QString &url, const QString &error) {
        if (!self)
            return;
        self->setStatus(error.isEmpty() ? tr("Uploaded, link copied: %1").arg(url)
                                        : tr("Upload failed: %1").arg(error));
    });
}

void EditorSession::ocrCopyText()
{
    m_app->ocrImage(composited());
}

void EditorSession::startOcrPick()
{
    if (!m_canvas)
        return;
    m_canvas->setOcrMode(true);
    m_canvas->setOcrBusy(true);
    setStatus(tr("Recognizing text…"));
    // OCR the clean base (annotations would interfere with recognition); word
    // boxes are in image-pixel space, which is the canvas's own coordinate
    // system, so they overlay exactly.
    QPointer<EditorSession> self(this);
    QPointer<AnnotationCanvas> canvas(m_canvas);
    // Capture the OCR sequence: if the user dismisses this pick (Escape),
    // starts another, or the base image changes before recognition returns,
    // the sequence advances and this now-stale result must be dropped rather
    // than repopulating a different/dismissed session with misaligned boxes.
    const quint64 seq = m_canvas->ocrSeq();
    m_app->ocrBoxes(m_canvas->image(), [self, canvas, seq](const QVector<OcrWord> &words, const QString &err) {
        if (!canvas || canvas->ocrSeq() != seq || !canvas->ocrMode())
            return;
        canvas->setOcrWords(words);
        if (self) {
            if (!err.isEmpty())
                self->setStatus(err);
            else if (words.isEmpty())
                self->setStatus(tr("No text found"));
            else
                self->setStatus(tr("Text recognized — click a line, double-click a word, or drag for letters · Ctrl+A all · Ctrl+C copy"));
        }
    });
}

void EditorSession::copyOcrSelection()
{
    if (!m_canvas)
        return;
    const QString text = m_canvas->ocrSelectedText();
    if (text.isEmpty()) {
        setStatus(tr("No text selected"));
        return;
    }
    m_app->copyText(text);
    setStatus(tr("Copied selected text"));
}

void EditorSession::removeBackground()
{
    if (!m_canvas)
        return;
    setStatus(tr("Removing background…"));
    const QImage base = m_canvas->image();
    QPointer<EditorSession> self(this);
    QPointer<AnnotationCanvas> canvas(m_canvas);
    m_app->segmentForeground(base, base.rect(), [self, canvas](const QImage &mask, const QString &err) {
        if (!canvas)
            return;
        if (mask.isNull()) {
            if (self)
                self->setStatus(err.isEmpty() ? tr("Background removal failed") : err);
            return;
        }
        canvas->applyBaseMask(mask);
        if (self)
            self->setStatus(tr("Background removed — save as PNG or WebP to keep transparency"));
    });
}

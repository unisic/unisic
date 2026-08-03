#include "EditorSession.h"
#include "AnnotationCanvas.h"
#include "AppContext.h"
#include "FilenameTemplate.h"
#include "Settings.h"
#include <QPointer>
#include <QFileInfo>
#include <QVector>

EditorSession::EditorSession(AppContext *app, const QImage &image,
                             const QString &overwritePath, quint64 historyId, QObject *parent)
    : QObject(parent), m_app(app), m_image(image), m_overwritePath(overwritePath),
      m_historyId(historyId)
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
        // Text-aware Highlight: when the user selects the Highlight tool the
        // canvas asks (once) for glyph boxes. OCR the clean base off-thread and
        // feed them back so a plain highlight drag over text snaps to the line.
        // If OCR is unavailable, the highlighter simply stays a plain rectangle.
        connect(m_canvas, &AnnotationCanvas::glyphBoxesRequested, this, [this] {
            if (!m_canvas || !m_app->ocrAvailable())
                return;
            QPointer<AnnotationCanvas> canvas(m_canvas);
            m_app->ocrBoxes(m_canvas->image(),
                            [canvas](const QVector<OcrWord> &words, const QString &) {
                if (canvas)
                    canvas->setGlyphBoxes(words);
            });
        });
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
        const QFileInfo fi(m_overwritePath);
        const QString want =
            FilenameTemplate::extensionFor(m_app->settings()->imageFormat());
        // "Convert opened images to my format": a file that arrived in some
        // other format leaves as a NEW file in the chosen one, beside the
        // original. Never in place - the extension would then disagree with the
        // bytes, and a lossy re-encode over someone's only copy of the source
        // is not something a Save button should be able to do silently.
        if (m_app->settings()->convertIncoming()
            && !FilenameTemplate::sameFormat(fi.suffix(), want)) {
            const QString path = m_app->saveImageTo(img, fi.absolutePath(),
                                                    fi.completeBaseName() + QLatin1Char('.') + want);
            if (path.isEmpty()) {
                setStatus(tr("Save failed"));
                return {};
            }
            // Its own file, so its own tile - the original keeps the one it has.
            m_app->history()->addEntry(path, img, QStringLiteral("image"));
            setStatus(tr("Saved as %1").arg(QFileInfo(path).fileName()));
            return path;
        }
        // Encoded for the extension the file already has, so a .jpg keeps the
        // quality setting instead of Qt's default and a .gif goes through
        // ffmpeg instead of failing silently.
        if (m_app->overwriteImageFile(img, m_overwritePath)) {
            m_app->history()->refreshEntry(m_overwritePath, img);
            setStatus(tr("Saved (overwrote %1)").arg(fi.fileName()));
            return m_overwritePath;
        }
        setStatus(tr("Save failed"));
        return {};
    }
    const QString path = m_app->saveImageAuto(img);
    if (!path.isEmpty()) {
        // The capture that opened this editor already has a history entry. If it
        // is still file-less (auto-save off), this save belongs to THAT entry -
        // adding another would leave the same capture on two tiles. An entry
        // that already points at a file gets a genuinely new one instead, since
        // this save produced a second file on disk.
        if (m_historyId != 0
            && m_app->history()->entryById(m_historyId)
                   .value(QStringLiteral("filePath")).toString().isEmpty()
            && m_app->history()->setFilePathById(m_historyId, path))
            m_app->history()->refreshEntry(path, img); // thumbnail of the EDITED image
        else
            m_app->history()->addEntry(path, img, QStringLiteral("image"));
        setStatus(tr("Saved to %1").arg(path));
    } else {
        setStatus(tr("Save failed"));
    }
    return path;
}

QString EditorSession::saveAs(const QString &format)
{
    const QImage img = composited();
    // The format is the point of the click, so the over-size auto-conversion
    // stays out of it.
    const QString path = m_app->saveImageAuto(img, m_app->makeFileName(format),
                                              /*allowAutoConvert=*/false);
    if (path.isEmpty()) {
        setStatus(tr("Save failed"));
        return {};
    }
    // Same one-capture-one-tile rule as save(): a capture whose entry has no
    // file yet claims this one; an entry that already points at a file gets a
    // second tile, because this really is a second file.
    if (m_historyId != 0
        && m_app->history()->entryById(m_historyId)
               .value(QStringLiteral("filePath")).toString().isEmpty()
        && m_app->history()->setFilePathById(m_historyId, path))
        m_app->history()->refreshEntry(path, img);
    else
        m_app->history()->addEntry(path, img, QStringLiteral("image"));
    setStatus(tr("Saved to %1").arg(path));
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
    }, m_historyId); // one capture, one tile: the link lands on its own entry
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
                self->setStatus(tr("Text recognized - click a line, double-click a word, or drag for letters · Ctrl+A all · Ctrl+C copy"));
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

void EditorSession::highlightOcrSelection()
{
    if (!m_canvas || !m_canvas->highlightOcrSelection()) {
        setStatus(tr("No text selected"));
        return;
    }
    setStatus(tr("Highlighted selected text"));
}

void EditorSession::redactOcrSelection()
{
    if (!m_canvas || !m_canvas->redactOcrSelection(false)) {
        setStatus(tr("No text selected"));
        return;
    }
    setStatus(tr("Redacted selected text"));
}

void EditorSession::redactTextMatching(const QString &pattern)
{
    if (!m_canvas)
        return;
    const int n = m_canvas->redactTextMatching(pattern);
    if (n < 0) {
        setStatus(tr("Not a valid search pattern"));
        return;
    }
    if (n == 0) {
        // Distinct from "nothing selected": the pattern ran and found nothing,
        // which on a noisy recognition is the expected answer often enough that
        // silence would read as a broken button.
        setStatus(tr("Nothing matched"));
        return;
    }
    setStatus(tr("Redacted %n match(es)", "", n));
}


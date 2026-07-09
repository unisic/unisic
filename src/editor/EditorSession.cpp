#include "EditorSession.h"
#include "AnnotationCanvas.h"
#include "AppContext.h"
#include <QPointer>
#include <QUrl>
#include <QFileInfo>

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
        self->setStatus(error.isEmpty() ? tr("Uploaded — link copied: %1").arg(url)
                                        : tr("Upload failed: %1").arg(error));
    });
}

void EditorSession::ocrCopyText()
{
    m_app->ocrImage(composited());
}

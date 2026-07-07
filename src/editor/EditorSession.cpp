#include "EditorSession.h"
#include "AnnotationCanvas.h"
#include "AppContext.h"
#include <QUrl>

EditorSession::EditorSession(AppContext *app, const QImage &image, QObject *parent)
    : QObject(parent), m_app(app), m_image(image)
{
}

void EditorSession::bindCanvas(AnnotationCanvas *canvas)
{
    m_canvas = canvas;
    if (m_canvas)
        m_canvas->setImage(m_image);
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
    m_app->uploadImage(composited(), [this](const QString &url, const QString &error) {
        setStatus(error.isEmpty() ? tr("Uploaded — link copied: %1").arg(url)
                                  : tr("Upload failed: %1").arg(error));
    });
}

void EditorSession::ocrCopyText()
{
    m_app->ocrImage(composited());
}

#include "ScrollPreviewProvider.h"
#include "AppContext.h"
#include "ScrollCaptureController.h"

ScrollPreviewProvider::ScrollPreviewProvider(AppContext *app)
    : QQuickImageProvider(QQuickImageProvider::Image), m_app(app)
{
}

QImage ScrollPreviewProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id);
    if (!m_app)
        return {};

    auto *ctl = m_app->scrollCaptureController();
    if (!ctl)
        return {};

    const int targetW = (requestedSize.width() > 0) ? requestedSize.width() : 140;
    const int targetH = (requestedSize.height() > 0) ? requestedSize.height() : 300;

    const QImage thumb = ctl->previewThumbnail(targetW, targetH);
    if (size)
        *size = thumb.size();
    return thumb;
}

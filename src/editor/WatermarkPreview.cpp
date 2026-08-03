#include "WatermarkPreview.h"

#include "AppContext.h"
#include "ImageEffects.h"

#include <QLoggingCategory>
#include <QThread>

WatermarkPreviewProvider::WatermarkPreviewProvider(const AppContext *app)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_app(app)
{
}

QImage WatermarkPreviewProvider::requestImage(const QString &id, QSize *size,
                                              const QSize &requestedSize)
{
    Q_UNUSED(id) // revision only: it exists to make QML re-request the image
    // GUI thread only, and checked rather than assumed. stampWatermark reads
    // AppContext's settings and its cached watermark image with no lock, which
    // is safe exactly as long as QML calls this on the thread that owns them -
    // and QML calls it on a worker thread the moment an Image asks for it with
    // `asynchronous: true`. An empty QImage there is a blank preview box, which
    // is a bug someone finds; a torn read of a QImage being replaced is a crash
    // nobody can reproduce.
    if (m_app && QThread::currentThread() != m_app->thread()) {
        qWarning("watermark preview: requested off the GUI thread (asynchronous Image?); "
                 "the provider is not thread-safe, returning nothing");
        return {};
    }
    const QSize target = requestedSize.width() > 0 && requestedSize.height() > 0
                             ? requestedSize
                             : QSize(440, 260);
    QImage img = UnisicImageEffects::mockCapture(target);
    if (m_app)
        img = m_app->stampWatermark(img);
    if (size)
        *size = img.size();
    return img;
}

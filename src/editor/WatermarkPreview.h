#pragma once

#include <QQuickImageProvider>

class AppContext;

// Serves UnisicImageEffects::mockCapture, watermarked with the LIVE settings, as
// image://watermark/<revision>. The revision only busts the QML pixmap cache;
// the pixels always come from the current settings.
//
// Cost: one ARGB frame at the requested size (about 460 kB at 440x260) built
// per request, freed when QML drops the pixmap. Nothing is cached here - a
// cache keyed on the settings would have to be invalidated by all eight of
// them, and re-rendering one small frame is cheaper than getting that wrong.
class WatermarkPreviewProvider : public QQuickImageProvider
{
public:
    explicit WatermarkPreviewProvider(const AppContext *app);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const AppContext *m_app;
};

#pragma once
#include <QQuickImageProvider>

class AppContext;

// Serves live preview thumbnails of scrolling capture as image://scrollpreview/<revision>.
class ScrollPreviewProvider : public QQuickImageProvider
{
public:
    explicit ScrollPreviewProvider(AppContext *app);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    AppContext *m_app;
};

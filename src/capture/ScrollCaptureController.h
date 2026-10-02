#pragma once
#include <QObject>
#include <QRect>
#include <QPointer>
#include <QTimer>
#include <QSize>
#include "ScrollStitcher.h"

class QQmlEngine;
class QQuickWindow;
class QScreen;
class AppContext;
class IScreenGrabber;
class ScreenCastSession;
class KWinScreencasting;
class KWinScreencastStream;

class ScrollCaptureController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int regionX READ regionX NOTIFY regionChanged)
    Q_PROPERTY(int regionY READ regionY NOTIFY regionChanged)
    Q_PROPERTY(int regionW READ regionW NOTIFY regionChanged)
    Q_PROPERTY(int regionH READ regionH NOTIFY regionChanged)
    Q_PROPERTY(int stitchedWidth READ stitchedWidth NOTIFY stitchedSizeChanged)
    Q_PROPERTY(int stitchedHeight READ stitchedHeight NOTIFY stitchedSizeChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewRevisionChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit ScrollCaptureController(AppContext *app, QQmlEngine *engine, QObject *parent = nullptr);
    ~ScrollCaptureController() override;

    int regionX() const { return m_regionX; }
    int regionY() const { return m_regionY; }
    int regionW() const { return m_regionW; }
    int regionH() const { return m_regionH; }

    int stitchedWidth() const { return m_stitcher.stitchedWidth(); }
    int stitchedHeight() const { return m_stitcher.stitchedHeight(); }
    int frameCount() const { return m_stitcher.frameCount(); }
    int previewRevision() const { return m_previewRevision; }
    bool active() const { return m_active; }

    QImage previewThumbnail(int maxW, int maxH) const;

    void start(const QRect &cropPhysical, QScreen *screen);
    void stop();

    Q_INVOKABLE void finish();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setInputRect(int x, int y, int w, int h);
    Q_INVOKABLE void setInputRects(int x1, int y1, int w1, int h1,
                                  int x2, int y2, int w2, int h2);

signals:
    void regionChanged();
    void stitchedSizeChanged();
    void frameCountChanged();
    void previewRevisionChanged();
    void activeChanged();
    void finished(const QImage &image);

private slots:
    void sampleTick();

private:
    void openGrabber();
    void openPortalSession();
    void openX11Session();
    void wireGrabber(IScreenGrabber *grabber);
    void createOverlayWindow();
    void closeOverlayWindow();

    AppContext *m_app;
    QQmlEngine *m_engine;
    QPointer<QScreen> m_screen;
    QRect m_cropPhysical;
    int m_regionX = 0;
    int m_regionY = 0;
    int m_regionW = 0;
    int m_regionH = 0;

    bool m_active = false;
    bool m_isRegionStream = false;
    quint64 m_lastSampledSeq = 0;
    int m_previewRevision = 0;
    QSize m_streamSize;

    ScrollStitcher m_stitcher;
    QTimer m_sampleTimer;

    IScreenGrabber *m_grabber = nullptr;
    ScreenCastSession *m_session = nullptr;
    KWinScreencasting *m_kwinCast = nullptr;
    KWinScreencastStream *m_kwinStream = nullptr;

    QPointer<QQuickWindow> m_window;
};

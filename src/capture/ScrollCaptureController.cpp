#include "ScrollCaptureController.h"
#include "AppContext.h"
#include "record/IScreenGrabber.h"
#include "record/PipeWireGrabber.h"
#include "record/X11ShmGrabber.h"
#include "capture/ScreenCastSession.h"
#include "capture/KWinScreencasting.h"

#include <cmath>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QScreen>
#include <QRegion>
#include <QMargins>
#include <QtConcurrent/QtConcurrent>
#include <LayerShellQt/window.h>

ScrollCaptureController::ScrollCaptureController(AppContext *app, QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_engine(engine)
{
    connect(&m_sampleTimer, &QTimer::timeout, this, &ScrollCaptureController::sampleTick);
}

ScrollCaptureController::~ScrollCaptureController()
{
    stop();
    closeOverlayWindow();
}

QImage ScrollCaptureController::previewThumbnail(int maxW, int maxH) const
{
    return m_stitcher.previewThumbnail(maxW, maxH);
}

void ScrollCaptureController::start(const QRect &cropPhysical, QScreen *screen)
{
    if (m_active)
        stop();

    m_screen = screen ? screen : QGuiApplication::primaryScreen();
    if (!m_screen)
        return;

    m_cropPhysical = cropPhysical;
    m_stitcher.reset();
    m_lastSampledSeq = 0;
    m_previewRevision = 0;
    m_streamSize = QSize();
    m_isRegionStream = false;
    m_unmatchedRun = 0;
    setStatus(false, false);

    // Convert physical crop coordinates to window-local logical pixels
    const qreal dpr = m_screen->devicePixelRatio() > 0 ? m_screen->devicePixelRatio() : 1.0;
    const int left   = qFloor(cropPhysical.x() / dpr);
    const int top    = qFloor(cropPhysical.y() / dpr);
    const int right  = qCeil((cropPhysical.x() + cropPhysical.width()) / dpr);
    const int bottom = qCeil((cropPhysical.y() + cropPhysical.height()) / dpr);

    // The badge must sit outside the captured area: anything of ours inside
    // it would be stitched into every slice. With no room above or below
    // (a full-height selection), the bottom of the region gives way.
    const int screenH = m_screen->geometry().height();
    int capBottom = std::min(bottom, screenH);
    if (top < kBadgeRoom && screenH - capBottom < kBadgeRoom)
        capBottom = screenH - kBadgeRoom;

    m_regionX = left;
    m_regionY = top;
    m_regionW = std::max(20, right - left);
    m_regionH = std::max(20, capBottom - top);
    m_cropPhysical.setBottom(std::min(m_cropPhysical.bottom(),
                                      int(std::floor((top + m_regionH) * dpr)) - 1));
    emit regionChanged();

    m_active = true;
    emit activeChanged();

    createOverlayWindow();
    openGrabber();
}

void ScrollCaptureController::stop()
{
    m_sampleTimer.stop();

    if (m_grabber) {
        IScreenGrabber *g = m_grabber;
        m_grabber = nullptr;
        g->disconnect(this);
        g->setParent(nullptr);
        (void)QtConcurrent::run([g] {
            g->stop();
            QMetaObject::invokeMethod(g, "deleteLater", Qt::QueuedConnection);
        });
    }

    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
        m_session = nullptr;
    }

    if (m_kwinStream) {
        m_kwinStream->disconnect(this);
        m_kwinStream->deleteLater();
        m_kwinStream = nullptr;
    }
}

void ScrollCaptureController::finish()
{
    if (!m_active)
        return;

    m_active = false;
    emit activeChanged();

    stop();
    const QImage result = m_stitcher.stitchedImage();
    m_stitcher.reset();   // the canvas can be hundreds of MB
    closeOverlayWindow();

    if (!result.isNull()) {
        emit finished(result);
    }
}

void ScrollCaptureController::cancel()
{
    if (!m_active)
        return;

    m_active = false;
    emit activeChanged();

    stop();
    m_stitcher.reset();
    closeOverlayWindow();
}

void ScrollCaptureController::setInputRect(int x, int y, int w, int h)
{
    if (!m_window)
        return;

    // Use a 1x1 region outside the surface for empty mask so clicks pass through
    m_window->setMask((w > 0 && h > 0) ? QRegion(x, y, w, h) : QRegion(-1, -1, 1, 1));
}

void ScrollCaptureController::setInputRects(int x1, int y1, int w1, int h1,
                                            int x2, int y2, int w2, int h2)
{
    if (!m_window)
        return;

    QRegion reg(-1, -1, 1, 1);
    if (w1 > 0 && h1 > 0)
        reg = reg.united(QRegion(x1, y1, w1, h1));
    if (w2 > 0 && h2 > 0)
        reg = reg.united(QRegion(x2, y2, w2, h2));

    m_window->setMask(reg);
}

void ScrollCaptureController::openGrabber()
{
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        openX11Session();
        return;
    }

    // Try KWin screencast stream first (silent, no portal share prompt on KDE)
    if (!m_kwinCast)
        m_kwinCast = new KWinScreencasting(this);

    if (m_kwinCast->isAvailable() && m_kwinCast->regionStreamsSupported() && m_screen) {
        const qreal dpr = m_screen->devicePixelRatio() > 0 ? m_screen->devicePixelRatio() : 1.0;
        const QRectF logical(m_screen->geometry().x() + m_regionX,
                             m_screen->geometry().y() + m_regionY,
                             m_regionW, m_regionH);
        KWinScreencastStream *stream = m_kwinCast->createRegionStream(
            logical.toAlignedRect(), dpr, KWinScreencasting::CursorMode::Hidden);
        if (stream) {
            m_isRegionStream = true;
            m_kwinStream = stream;
            connect(stream, &KWinScreencastStream::created, this, [this](quint32 nodeId) {
                auto *pw = new PipeWireGrabber(this);
                m_grabber = pw;
                wireGrabber(pw);
                pw->start(-1, nodeId, 30, false);
            });
            connect(stream, &KWinScreencastStream::failed, this, [this](const QString &) {
                m_isRegionStream = false;
                openPortalSession();
            });
            return;
        }
    }

    openPortalSession();
}

void ScrollCaptureController::openPortalSession()
{
    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
    }

    m_session = new ScreenCastSession(this);
    connect(m_session, &ScreenCastSession::ready, this, [this](int fd, uint nodeId, const QSize &, const QPoint &) {
        auto *pw = new PipeWireGrabber(this);
        m_grabber = pw;
        wireGrabber(pw);
        pw->start(fd, nodeId, 30, false);
    });
    connect(m_session, &ScreenCastSession::failed, this, [this](const QString &err) {
        qWarning() << "Scroll capture portal session failed:" << err;
        cancel();
    });

    m_session->start(ScreenCastSession::CursorMode::Hidden, 1u);
}

void ScrollCaptureController::openX11Session()
{
    if (!m_screen)
        return;

    const qreal dpr = m_screen->devicePixelRatio() > 0 ? m_screen->devicePixelRatio() : 1.0;
    const QRect g = m_screen->geometry();
    const QRect rootRect(qRound(g.x() * dpr), qRound(g.y() * dpr),
                         qRound(g.width() * dpr), qRound(g.height() * dpr));

    auto *x = new X11ShmGrabber(this);
    m_grabber = x;
    wireGrabber(x);
    x->start(rootRect, 30, false);
}

void ScrollCaptureController::wireGrabber(IScreenGrabber *grabber)
{
    connect(grabber, &IScreenGrabber::formatReady, this, [this](const QSize &sz) {
        m_streamSize = sz;
        qInfo().noquote() << QStringLiteral("Scroll capture: %1 stream %2x%3, selection %4x%5 physical")
                                 .arg(m_isRegionStream ? QStringLiteral("region") : QStringLiteral("monitor"))
                                 .arg(sz.width()).arg(sz.height())
                                 .arg(m_cropPhysical.width()).arg(m_cropPhysical.height());
        m_sampleTimer.start(33); // ~30 fps sampling
    });

    connect(grabber, &IScreenGrabber::streamError, this, [this](const QString &e) {
        qWarning() << "Scroll capture grabber error:" << e;
    });
}

void ScrollCaptureController::sampleTick()
{
    if (!m_grabber)
        return;

    QByteArray buf;
    quint64 seq = 0;
    if (!m_grabber->latestFrame(buf, &seq))
        return;

    if (seq == m_lastSampledSeq)
        return;
    m_lastSampledSeq = seq;

    if (m_streamSize.isEmpty() || buf.size() < m_streamSize.width() * m_streamSize.height() * 4)
        return;

    // The frames are in the stream's native byte order; wrap them in the
    // QImage format whose bytes match (as GifRecorder does), opaque for the
    // x formats whose padding byte is undefined.
    const QString pf = m_grabber->pixelFormat();
    QImage::Format fmt;
    if (pf == QLatin1String("bgra"))      fmt = QImage::Format_ARGB32;
    else if (pf == QLatin1String("bgr0")) fmt = QImage::Format_RGB32;
    else if (pf == QLatin1String("rgba")) fmt = QImage::Format_RGBA8888;
    else if (pf == QLatin1String("rgb0")) fmt = QImage::Format_RGBX8888;
    else return;

    const QImage fullImg(reinterpret_cast<const uchar *>(buf.constData()),
                         m_streamSize.width(), m_streamSize.height(), fmt);

    QImage frameImg;
    if (m_isRegionStream) {
        frameImg = fullImg.copy();
    } else {
        // A monitor stream should be the output's physical size; if the
        // compositor scaled it, scale the crop with it.
        QRect crop = m_cropPhysical;
        const QSize phys = m_screen ? m_screen->geometry().size() * m_screen->devicePixelRatio() : QSize();
        if (!phys.isEmpty() && phys != m_streamSize) {
            const double sx = double(m_streamSize.width()) / phys.width();
            const double sy = double(m_streamSize.height()) / phys.height();
            crop = QRectF(crop.x() * sx, crop.y() * sy, crop.width() * sx, crop.height() * sy).toAlignedRect();
        }
        crop = crop.intersected(QRect(QPoint(0, 0), m_streamSize));
        if (crop.width() < 10 || crop.height() < 10)
            return;
        frameImg = fullImg.copy(crop);
    }

    switch (m_stitcher.addFrame(frameImg)) {
    case ScrollStitcher::Result::Started:
    case ScrollStitcher::Result::Stitched:
        m_unmatchedRun = 0;
        setStatus(false, false);
        ++m_previewRevision;
        emit previewRevisionChanged();
        emit stitchedSizeChanged();
        emit frameCountChanged();
        break;
    case ScrollStitcher::Result::Unchanged:
        m_unmatchedRun = 0;
        setStatus(false, m_stitcher.isFull());
        break;
    case ScrollStitcher::Result::Unmatched:
        // ~0.3 s of frames that moved but fit nowhere: the user scrolled
        // further than one viewport between two samples.
        if (++m_unmatchedRun >= 10)
            setStatus(true, false);
        break;
    case ScrollStitcher::Result::Full:
        setStatus(false, true);
        break;
    }
}

void ScrollCaptureController::setStatus(bool lostTrack, bool full)
{
    if (lostTrack == m_lostTrack && full == m_full)
        return;
    m_lostTrack = lostTrack;
    m_full = full;
    emit statusChanged();
}

void ScrollCaptureController::createOverlayWindow()
{
    closeOverlayWindow();
    if (!m_screen || !m_engine)
        return;

    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qt/qml/Unisic/qml/ScrollCaptureOverlay.qml")));
    if (component.isError()) {
        qWarning() << "ScrollCaptureOverlay component error:" << component.errorString();
        return;
    }

    auto *ctx = new QQmlContext(m_engine->rootContext(), this);
    ctx->setContextProperty(QStringLiteral("scrollCtl"), this);

    QObject *obj = component.create(ctx);
    auto *win = qobject_cast<QQuickWindow *>(obj);
    if (!win) {
        delete obj;
        delete ctx;
        return;
    }

    ctx->setParent(win);
    m_window = win;
    win->setScreen(m_screen);
    win->setGeometry(m_screen->geometry());

    if (m_app && m_app->layerShellAvailable()) {
        if (auto *ls = LayerShellQt::Window::get(win)) {
            using LW = LayerShellQt::Window;
            ls->setLayer(LW::LayerOverlay);
            ls->setScope(QStringLiteral("unisic-scroll-overlay"));
            ls->setExclusiveZone(-1);
            ls->setAnchors(LW::Anchors(LW::AnchorTop | LW::AnchorBottom
                                       | LW::AnchorLeft | LW::AnchorRight));
            ls->setMargins(QMargins(0, 0, 0, 0));
        }
        win->show();
    } else {
        win->showFullScreen();
    }
}

void ScrollCaptureController::closeOverlayWindow()
{
    if (m_window) {
        m_window->close();
        m_window->deleteLater();
        m_window = nullptr;
    }
}

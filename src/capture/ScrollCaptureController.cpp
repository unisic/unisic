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

void ScrollCaptureController::start(const QRect &cropPhysical, QScreen *screen, qreal streamScale)
{
    if (m_active)
        stop();

    m_screen = screen ? screen : QGuiApplication::primaryScreen();
    if (!m_screen)
        return;
    m_streamScale = streamScale;

    m_cropPhysical = cropPhysical;
    m_stitcher.reset();
    m_lastSampledSeq = 0;
    m_previewRevision = 0;
    m_streamSize = QSize();
    m_waitTicks = 0;
    m_startRows = QImage();
    m_finishing = false;
    m_finishRows = QImage();
    setControlsShown(false);
    m_isRegionStream = false;
    m_unmatchedRun = 0;
    setStatus(false, false);

    // Convert physical crop coordinates to window-local logical pixels
    const qreal dpr = m_screen->devicePixelRatio() > 0 ? m_screen->devicePixelRatio() : 1.0;
    const int left   = qFloor(cropPhysical.x() / dpr);
    const int top    = qFloor(cropPhysical.y() / dpr);
    const int right  = qCeil((cropPhysical.x() + cropPhysical.width()) / dpr);
    const int bottom = qCeil((cropPhysical.y() + cropPhysical.height()) / dpr);

    // The badge must stay out of the stitched rows: anything of ours in them
    // would be stitched into every slice. With no room above or below (a
    // full-height selection) it covers the bottom rows of the region, which
    // the frames leave out until the badge is gone (finishTick).
    const int screenH = m_screen->geometry().height();
    const int capBottom = std::min(bottom, screenH);
    m_coveredRows = (top < kBadgeRoom && screenH - capBottom < kBadgeRoom)
                    ? capBottom - (screenH - kBadgeRoom) : 0;

    m_regionX = left;
    m_regionY = top;
    m_regionW = std::max(20, right - left);
    m_regionH = std::max(20, capBottom - top);
    m_cropPhysical.setBottom(std::min(m_cropPhysical.bottom(),
                                      int(std::floor((top + m_regionH) * dpr)) - 1));
    emit regionChanged();

    m_active = true;
    emit activeChanged();

    // Mapped now, drawn from the first frame on (`controlsShown`): on the portal
    // path a share dialog is still waiting, and a badge saying "scroll down"
    // with a live Finish over it ends in nothing captured (#121). Mapping it
    // only when frames arrive put the region frame into the first slices.
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
    if (!m_active || m_finishing)
        return;

    // The rows under the badge show only once it is gone: hide it, and the
    // sample ticks finish on a frame without it (finishTick).
    if (m_coveredRows > 0 && m_window && m_grabber && !m_stitcher.isEmpty()) {
        m_finishing = true;
        m_waitTicks = 0;
        m_window->hide();
        return;
    }
    completeFinish(false);
}

void ScrollCaptureController::completeFinish(bool coveredRowsAdded)
{
    // Scrolled up, the image ends with the frame it started from.
    if (m_coveredRows > 0 && !coveredRowsAdded
        && !m_stitcher.appendBelow(0, m_startRows))
        qWarning() << "Scroll capture: the rows under the badge are missing"
                      " (finished away from the bottom of the image)";

    m_finishing = false;
    m_startRows = QImage();
    m_finishRows = QImage();
    m_active = false;
    emit activeChanged();

    stop();
    const QImage result = m_stitcher.stitchedImage();
    m_stitcher.reset();   // the canvas can be hundreds of MB
    closeOverlayWindow();

    if (!result.isNull())
        emit finished(result);
    else
        emit cancelled();
}

void ScrollCaptureController::cancel()
{
    if (!m_active)
        return;

    m_active = false;
    emit activeChanged();

    stop();
    m_stitcher.reset();
    m_finishing = false;
    m_startRows = QImage();
    m_finishRows = QImage();
    closeOverlayWindow();
    emit cancelled();
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
    // X11 and Windows have no ScreenCast portal: grab the monitor directly.
    if (X11ShmGrabber::isAvailable()) {
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
        // The real output scale (see GifRecorder::tryOpenKWinStream): the
        // screen's DPR upscales every slice on a fractional output.
        KWinScreencastStream *stream = m_kwinCast->createRegionStream(
            logical.toAlignedRect(), m_streamScale > 0 ? m_streamScale : dpr,
            KWinScreencasting::CursorMode::Hidden);
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

    auto *x = new X11ShmGrabber(this);
    m_grabber = x;
    wireGrabber(x);
    x->start(X11ShmGrabber::screenRect(m_screen), 30, false);
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
        // A badge over the region waits for the first frame, so the rows
        // under it are clean in that one (sampleTick).
        if (m_coveredRows == 0)
            setControlsShown(true);
    });

    connect(grabber, &IScreenGrabber::streamError, this, [this](const QString &e) {
        qWarning() << "Scroll capture grabber error:" << e;
    });
}

void ScrollCaptureController::sampleTick()
{
    if (!m_grabber)
        return;

    // A badge over the region waits until the start frame has stood for
    // ~0.15 s (before the first scroll a restart replaces it, e.g. while the
    // region overlay fades out), or a second without one; the capture can
    // then still be finished or cancelled, only the rows under it are lost.
    if (!m_controlsShown && !m_finishing && (++m_waitTicks >= 30
                                             || (!m_startRows.isNull() && m_waitTicks - m_startTick >= 5)))
        setControlsShown(true);
    if (m_finishing && ++m_waitTicks >= 45) {
        qWarning() << "Scroll capture: no frame after hiding the badge";
        completeFinish(false);
        return;
    }

    QByteArray buf;
    quint64 seq = 0;
    if (!m_grabber->latestFrame(buf, &seq))
        return;

    // Finishing reads every tick: the frame may never change.
    if (seq == m_lastSampledSeq && !m_finishing)
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

    QRect sel = fullImg.rect();
    if (!m_isRegionStream) {
        // A monitor stream should be the output's physical size; if the
        // compositor scaled it, scale the crop with it.
        sel = m_cropPhysical;
        const QSize phys = m_screen ? m_screen->geometry().size() * m_screen->devicePixelRatio() : QSize();
        if (!phys.isEmpty() && phys != m_streamSize) {
            const double sx = double(m_streamSize.width()) / phys.width();
            const double sy = double(m_streamSize.height()) / phys.height();
            sel = QRectF(sel.x() * sx, sel.y() * sy, sel.width() * sx, sel.height() * sy).toAlignedRect();
        }
        sel = sel.intersected(fullImg.rect());
        if (sel.width() < 10 || sel.height() < 10)
            return;
    }
    // Stream rows under the badge, rounded up so none of it is stitched.
    const int cut = (sel.height() * m_coveredRows + m_regionH - 1) / m_regionH;

    if (m_finishing) {
        finishTick(fullImg, sel, cut);
        return;
    }

    const ScrollStitcher::Result result = m_stitcher.addFrame(fullImg.copy(sel.adjusted(0, 0, 0, -cut)));
    if (cut > 0 && result == ScrollStitcher::Result::Started) {
        // The rows under the badge of the frame the image starts from, for a
        // capture scrolled upwards. Clean only while the badge is not drawn
        // yet: a restart later may have it in them.
        m_startRows = m_controlsShown ? QImage()
                                      : fullImg.copy(sel.x(), sel.bottom() - cut + 1, sel.width(), cut);
        m_startTick = m_waitTicks;
    }

    switch (result) {
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

// Finishing a capture whose badge covers the bottom of the region: the badge
// is hidden (finish), and a settled frame without it gives the rows it covered.
void ScrollCaptureController::finishTick(const QImage &screen, const QRect &sel, int cut)
{
    const QImage rows = screen.copy(sel.x(), sel.bottom() - cut + 1, sel.width(), cut);
    const bool settled = rows == m_finishRows;
    m_finishRows = rows;
    // A stream that shows the badge drops it a frame or two after the hide,
    // or fades it out over a few (KWin); one it was never in (the KWin
    // region stream) sends no new frame at all. So: ~0.3 s, then two samples
    // that agree, or whatever is there after a second.
    if (m_waitTicks < 10 || (!settled && m_waitTicks < 30))
        return;

    // Below the newest stitched frame, if the page has not moved since.
    const bool added = !ScrollStitcher::findShift(m_stitcher.lastFrame(),
                                                  screen.copy(sel.adjusted(0, 0, 0, -cut))).changed
                       && m_stitcher.appendBelow(m_stitcher.framePos(), rows);
    completeFinish(added);
}

void ScrollCaptureController::setControlsShown(bool shown)
{
    if (shown == m_controlsShown)
        return;
    m_controlsShown = shown;
    emit controlsShownChanged();
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
            // KWin types a layer surface by its scope, and both Better Blur
            // forks skip a dock unless "Blur docks" is on. Under any other
            // scope this is a normal window, which their "blur all except"
            // mode blurs, and with it the whole output under this transparent
            // frame, on screen and in the stream we stitch (#121).
            ls->setScope(QStringLiteral("dock"));
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

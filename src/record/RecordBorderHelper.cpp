#include "RecordBorderHelper.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRasterWindow>
#include <QRegion>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <vector>

namespace {

// The record-region frame under GNOME/mutter. Once ONE full-monitor
// override-redirect XWayland window drew the whole frame and masked itself down
// to the ring (XShape bounding). That works for plain clicks (empty input
// shape) but mutter picks the drop-target actor for its OWN Clutter
// drag-and-drop (Ubuntu Dock icon reorder) by the actor's allocation, NOT the
// XShape bounding — so the full-monitor surface still blocked the Dock drag
// even with the ring mask, and equally blocked any drag INSIDE the recorded
// region (bug #44). The dependency-free cure is to have NO large surface at
// all: draw the frame as four thin bar windows around the region, the REC clock
// as its own small window, and the pre-recording countdown number as a window
// over the region interior that is destroyed the instant recording begins.
// Nothing then overlays the Dock or the region, so there is no actor for
// mutter's DnD to catch.

const int kBw = 3;            // accent frame base thickness
const int kFramePad = kBw + 1; // the ring reaches this many px outside the region
const QColor kContrast(0, 0, 0, 140);

const Qt::WindowFlags kFrameFlags = Qt::BypassWindowManagerHint
                                    | Qt::FramelessWindowHint
                                    | Qt::WindowStaysOnTopHint
                                    | Qt::WindowDoesNotAcceptFocus
                                    | Qt::WindowTransparentForInput;

QSurfaceFormat argbFormat(const QSurfaceFormat &base)
{
    QSurfaceFormat fmt = base;
    fmt.setAlphaBufferSize(8); // ARGB visual — translucency needs it on X11
    return fmt;
}

// Concentric rings around `r` (window-local): from the region edge outward,
// [0..1) contrast, [1..3) accent, [3..4) contrast. Drawn strictly OUTSIDE r so
// the ffmpeg crop (= the region) never contains a frame pixel.
void paintRing(QPainter &p, const QRect &r, const QColor &accent)
{
    const auto ring = [&p, &r](int outPad, int inPad, const QColor &c) {
        QRegion reg(r.adjusted(-outPad, -outPad, outPad, outPad));
        reg -= QRegion(r.adjusted(-inPad, -inPad, inPad, inPad));
        for (const QRect &part : reg)
            p.fillRect(part, c);
    };
    ring(kBw + 1, kBw, kContrast); // outer contrast line
    ring(kBw, 1, accent);          // accent frame
    ring(1, 0, kContrast);         // inner contrast line
}

// One side of the frame. Its geometry is that side's ring segment; it paints the
// WHOLE ring (translated into window-local space) and lets its geometry clip the
// result to just this side — so the four bars together reproduce the exact ring
// the old single window drew, with no surface between them.
class BarWindow : public QRasterWindow
{
public:
    BarWindow(const QRect &absRegion, const QRect &absGeom, const QColor &accent)
        : m_localRegion(absRegion.translated(-absGeom.topLeft())), m_accent(accent)
    {
        setFlags(kFrameFlags);
        setFormat(argbFormat(format()));
        setGeometry(absGeom);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        paintRing(p, m_localRegion, m_accent);
    }

private:
    QRect m_localRegion;
    QColor m_accent;
};

// The pulsing "REC h:mm:ss" badge, a small window just outside the region. Its
// clock starts only when recording actually begins (countdown → 0).
class BadgeWindow : public QRasterWindow
{
public:
    explicit BadgeWindow(const QRect &absGeom)
    {
        // Unlike the frame bars, the badge must receive clicks (its stop/pause
        // controls) — so it is NOT input-transparent. Its input shape is masked
        // down to the visible pill; the transparent padding stays click-through.
        setFlags(kFrameFlags & ~Qt::WindowTransparentForInput);
        setFormat(argbFormat(format()));
        setGeometry(absGeom);
        updateInputShape();  // seed the shape before any expose (pre-create)
        m_maskW = -1;        // force the first post-create paint to re-apply it
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, [this] {
            if (m_paused) // hold the dot solid + the clock frozen while paused
                return;
            m_pulse = !m_pulse;
            update();
        });
        tick->start(500);
    }

    void startClock()
    {
        if (!m_clock.isValid())
            m_clock.start();
        update();
    }

    // Mirror the recorder: freeze the clock at the pause point and exclude every
    // paused span, and read "PAUSED" instead of "REC" with a solid (non-pulsing)
    // dot. Driven by the "p1"/"p0" stdin messages.
    void setPaused(bool p)
    {
        if (p == m_paused)
            return;
        const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
        if (p)
            m_pausedAtMs = now;
        else
            m_pausedTotalMs += now - m_pausedAtMs;
        m_paused = p;
        m_pulse = true; // solid dot while paused; toggling resumes with the tick
        update();
    }

    // Lay out dot | label | divider | pause | stop into window-local rects and
    // record the visible pill width. Shared by paint and the input mask so they
    // can never diverge. elapsedText() reads "00:00" before the clock starts.
    int layout()
    {
        QFont f;
        f.setPixelSize(12);
        f.setBold(true);
        m_label = (m_paused ? QStringLiteral("PAUSED  ") : QStringLiteral("REC  ")) + elapsedText();
        const int textW = QFontMetrics(f).horizontalAdvance(m_label);
        const int bh = height();
        int x = 8;
        m_dotRect = QRect(x, (bh - 8) / 2, 8, 8);
        x += 8 + 6;
        m_textRect = QRect(x, 0, textW, bh);
        x += textW + 8;
        m_divX = x;
        x += 1 + 8;
        const int btn = 20;
        m_pauseRect = QRect(x, (bh - btn) / 2, btn, btn);
        x += btn + 4;
        m_stopRect = QRect(x, (bh - btn) / 2, btn, btn);
        x += btn;
        m_pillW = x + 8;
        return m_pillW;
    }

    // Clip the input shape to the visible pill. On xcb setMask sets the BOUNDING
    // shape (visual+input): an empty QRegion would RESET to the full rectangle
    // (catching clicks in the padding), so an explicit pill rect is required —
    // recomputed only when the pill width actually changes.
    void updateInputShape()
    {
        const int bwid = layout();
        if (bwid != m_maskW) {
            m_maskW = bwid;
            setMask(QRegion(0, 0, bwid, height()));
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        updateInputShape(); // relayout + re-mask for the current label
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setRenderHint(QPainter::Antialiasing);

        QFont f;
        f.setPixelSize(12);
        f.setBold(true);
        const int bh = height();

        // Pill background.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawRoundedRect(QRect(0, 0, m_pillW, bh), bh / 2, bh / 2);

        // REC dot.
        QColor dot(0xff, 0x4d, 0x4d);
        dot.setAlphaF(m_pulse ? 1.0 : 0.25);
        p.setBrush(dot);
        p.drawEllipse(m_dotRect);

        // Label.
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(m_textRect, Qt::AlignVCenter | Qt::AlignLeft, m_label);

        // Divider before the controls.
        p.setPen(QPen(QColor(255, 255, 255, 56), 1));
        p.drawLine(m_divX, (bh - 16) / 2, m_divX, (bh + 16) / 2);

        // Pause bars, or a play triangle while paused.
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        if (m_paused) {
            const QRectF r(m_pauseRect);
            QPolygonF tri;
            tri << QPointF(r.left() + 6, r.top() + 5)
                << QPointF(r.left() + 6, r.bottom() - 5)
                << QPointF(r.right() - 5, r.center().y());
            p.drawPolygon(tri);
        } else {
            const QRect r = m_pauseRect;
            const int barW = 3, gap = 4, barH = 10;
            const int cy = r.center().y() - barH / 2;
            const int cx = r.center().x();
            p.drawRoundedRect(QRect(cx - gap / 2 - barW, cy, barW, barH), 1, 1);
            p.drawRoundedRect(QRect(cx + gap / 2, cy, barW, barH), 1, 1);
        }
        // Stop square.
        {
            const QRect r = m_stopRect;
            const int sq = 10;
            p.drawRoundedRect(QRect(r.center().x() - sq / 2, r.center().y() - sq / 2, sq, sq), 2, 2);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        // Request a toggle/stop from the parent; the resulting paused state comes
        // back over stdin ("p1"/"p0"), same as the KDE in-process border.
        const QPoint pos = e->position().toPoint();
        if (m_pauseRect.contains(pos)) {
            const char m[] = "pause\n";
            (void)::write(1, m, sizeof m - 1);
        } else if (m_stopRect.contains(pos)) {
            const char m[] = "stop\n";
            (void)::write(1, m, sizeof m - 1);
        }
    }

private:
    QString elapsedText() const
    {
        if (!m_clock.isValid())
            return QStringLiteral("00:00"); // countdown still running
        // Recorded (un-paused) time: freeze at the pause point, drop every span.
        const qint64 base = m_paused ? m_pausedAtMs : m_clock.elapsed();
        const qint64 s = qMax<qint64>(0, base - m_pausedTotalMs) / 1000;
        const qint64 h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
        const auto pad = [](qint64 v) {
            return QString::number(v).rightJustified(2, QLatin1Char('0'));
        };
        return (h > 0 ? QString::number(h) + QLatin1Char(':') + pad(m) : pad(m))
               + QLatin1Char(':') + pad(sec);
    }

    QElapsedTimer m_clock;
    bool m_pulse = true;
    bool m_paused = false;
    qint64 m_pausedAtMs = 0;    // clock reading when the current pause began
    qint64 m_pausedTotalMs = 0; // accumulated paused wall-clock, excluded above
    QString m_label;            // "REC/PAUSED  h:mm:ss", cached from layout()
    QRect m_dotRect;
    QRect m_textRect;
    int m_divX = 0;
    int m_pillW = 0;            // visible pill width (== input mask width)
    QRect m_pauseRect;          // hit rects (window-local), set by layout()
    QRect m_stopRect;
    int m_maskW = -1;           // last input-mask width (avoid redundant setMask)
};

// The pre-recording countdown: a big accent number in a circle, centered in the
// region. Lives only while the countdown ticks — destroyed on the transition to
// recording so the region interior becomes a genuine gap (no surface to catch
// mutter's DnD, and nothing over the content being recorded).
class CountdownWindow : public QRasterWindow
{
public:
    CountdownWindow(const QRect &absGeom, const QColor &accent, int n)
        : m_accent(accent), m_n(n)
    {
        setFlags(kFrameFlags);
        setFormat(argbFormat(format()));
        setGeometry(absGeom);
    }

    void setNumber(int n)
    {
        m_n = n;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        if (m_n <= 0)
            return;
        const QRect local(0, 0, width(), height());
        p.setRenderHint(QPainter::Antialiasing);
        const int d = int(qMin(width(), height()) * 0.42);
        const QRect circle(local.center().x() - d / 2, local.center().y() - d / 2, d, d);
        p.setPen(QPen(m_accent, 2));
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawEllipse(circle);
        QFont nf = p.font();
        nf.setPixelSize(qMax(24, int(qMin(width(), height()) * 0.26)));
        nf.setBold(true);
        p.setFont(nf);
        p.setPen(m_accent);
        p.drawText(local, Qt::AlignCenter, QString::number(m_n));
    }

private:
    QColor m_accent;
    int m_n;
};

} // namespace

int runRecordBorderHelper(int argc, char *argv[])
{
    // xcb BEFORE QGuiApplication — the whole point is an X11 (XWayland) window.
    qputenv("QT_QPA_PLATFORM", "xcb");
    // Work in raw X pixels: the region arrives as monitor FRACTIONS and is
    // mapped onto the X screen geometry below — a Qt HiDPI scale factor would
    // apply the monitor scale a second time.
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    qunsetenv("QT_SCALE_FACTOR");
    qunsetenv("QT_SCREEN_SCALE_FACTORS");
    QGuiApplication app(argc, argv);

    // --record-border-helper <name> <lx> <ly> <lw> <lh> <pw> <ph>
    //                        <fx> <fy> <fw> <fh> <#accent> [countdown]
    // name/l*/p*: the Wayland screen's name, logical geometry and physical size,
    // used only to pick the matching X screen. f*: region as fractions of the
    // monitor — XWayland's coordinate space (logical vs physical layout mode)
    // need not match either of the parent's, but the fraction is invariant.
    const QStringList args = app.arguments();
    const int i = args.indexOf(QStringLiteral("--record-border-helper"));
    if (i < 0 || i + 12 >= args.size())
        return 2;
    const QString name = args[i + 1];
    const QRect logical(args[i + 2].toInt(), args[i + 3].toInt(),
                        args[i + 4].toInt(), args[i + 5].toInt());
    const QSize physical(args[i + 6].toInt(), args[i + 7].toInt());
    const double fx = args[i + 8].toDouble(), fy = args[i + 9].toDouble();
    const double fw = args[i + 10].toDouble(), fh = args[i + 11].toDouble();
    QColor accent(args[i + 12]);
    if (!accent.isValid())
        accent = QColor(QStringLiteral("#C8ACD6"));
    // Optional 14th arg: initial pre-recording countdown (0 = none). Further
    // updates arrive over stdin as "c<N>\n".
    int countdown = (i + 13 < args.size()) ? args[i + 13].toInt() : 0;

    // Match the Wayland screen to an X screen. XWayland's RandR outputs often
    // carry synthetic names (XWAYLAND0…) under mutter, so fall through name →
    // exact logical geometry (scale-monitor-framebuffer layouts are identical)
    // → unique physical size → sole screen → primary.
    const QList<QScreen *> screens = app.screens();
    QScreen *target = nullptr;
    for (QScreen *s : screens)
        if (s->name() == name) { target = s; break; }
    if (!target)
        for (QScreen *s : screens)
            if (s->geometry() == logical) { target = s; break; }
    if (!target) {
        for (QScreen *s : screens) {
            if (s->geometry().size() == physical) {
                target = target ? nullptr : s; // ambiguous size match → give up
                if (!target)
                    break;
            }
        }
    }
    if (!target && screens.size() == 1)
        target = screens.first();
    if (!target)
        target = app.primaryScreen();
    if (!target)
        return 3;

    // 2px outward pad: fraction→pixel rounding across coordinate spaces (the X
    // layout may be logical while the ffmpeg crop is physical) must never let
    // the frame bleed INTO the crop; outside the region it is merely cosmetic.
    const QRect sg = target->geometry();
    const int x0 = sg.x() + int(std::floor(fx * sg.width())) - 2;
    const int y0 = sg.y() + int(std::floor(fy * sg.height())) - 2;
    const int x1 = sg.x() + int(std::ceil((fx + fw) * sg.width())) + 2;
    const int y1 = sg.y() + int(std::ceil((fy + fh) * sg.height())) + 2;

    // The padded region in absolute X coordinates. The frame is drawn OUTSIDE
    // it; nothing is drawn inside it.
    const QRect region(x0, y0, x1 - x0, y1 - y0);
    const QRect frame = region.adjusted(-kFramePad, -kFramePad, kFramePad, kFramePad);

    // Four thin bars covering the ring (top/bottom span the corners; left/right
    // fill between them). Each paints the whole ring translated and is clipped
    // by its geometry.
    std::vector<std::unique_ptr<BarWindow>> bars;
    const auto addBar = [&](const QRect &g) {
        auto w = std::make_unique<BarWindow>(region, g, accent);
        w->show();
        bars.push_back(std::move(w));
    };
    addBar(QRect(frame.left(), frame.top(), frame.width(), kFramePad));
    addBar(QRect(frame.left(), region.bottom() + 1, frame.width(), kFramePad));
    addBar(QRect(frame.left(), region.top(), kFramePad, region.height()));
    addBar(QRect(region.right() + 1, region.top(), kFramePad, region.height()));

    // Countdown number over the region interior (only while it ticks).
    std::unique_ptr<CountdownWindow> countdownWin;
    if (countdown > 0) {
        countdownWin = std::make_unique<CountdownWindow>(region, accent, countdown);
        countdownWin->show();
    }

    // REC badge just above the region (or below if there is no room above),
    // shown only once recording has begun. A fixed generous width means the
    // ticking clock never needs the window resized.
    std::unique_ptr<BadgeWindow> badgeWin;
    {
        const int bh = 28, maxW = 280; // wider: the badge now carries pause+stop
        const bool roomAbove = (region.top() - bh - 6) >= sg.top();
        const bool roomBelow = (region.bottom() + 6 + bh) <= sg.bottom();
        if (roomAbove || roomBelow) {
            const int by = roomAbove ? region.top() - bh - 6 : region.bottom() + 6;
            const int bx = std::max(sg.left(),
                                    std::min(sg.right() - maxW + 1, region.left()));
            badgeWin = std::make_unique<BadgeWindow>(QRect(bx, by, maxW, bh));
            if (countdown == 0) {
                badgeWin->show();
                badgeWin->startClock();
            }
        }
    }

    // stdin carries both the countdown ("c<N>\n") and the lifetime signal:
    // hideRecordBorder() closes it (or the parent dies) → EOF → quit. No
    // signals, no D-Bus, no polling.
    QSocketNotifier stdinWatch(0, QSocketNotifier::Read);
    QObject::connect(&stdinWatch, &QSocketNotifier::activated, &app,
                     [&app, &countdownWin, &badgeWin] {
                         char buf[64];
                         const ssize_t n = ::read(0, buf, sizeof buf - 1);
                         if (n <= 0) {
                             app.quit();
                             return;
                         }
                         buf[n] = '\0';
                         // Pause toggles arrive as "p1"/"p0"; apply the last one.
                         const char *lastP = nullptr;
                         for (const char *c = buf; *c; ++c)
                             if (*c == 'p')
                                 lastP = c;
                         if (lastP && badgeWin)
                             badgeWin->setPaused(lastP[1] == '1');
                         // Apply the last "c<N>" in the chunk (ticks can batch).
                         const char *last = nullptr;
                         for (const char *c = buf; *c; ++c)
                             if (*c == 'c')
                                 last = c;
                         if (!last)
                             return;
                         const int val = std::atoi(last + 1);
                         if (val > 0) {
                             if (countdownWin)
                                 countdownWin->setNumber(val);
                             return;
                         }
                         // val == 0: recording has begun. Drop the countdown
                         // number (frees the region interior) and raise the clock.
                         if (countdownWin) {
                             countdownWin->hide();
                             countdownWin.reset();
                         }
                         if (badgeWin) {
                             badgeWin->show();
                             badgeWin->startClock();
                         }
                     });

    return app.exec();
}

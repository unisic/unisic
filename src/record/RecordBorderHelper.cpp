#include "RecordBorderHelper.h"

#include <QElapsedTimer>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QRasterWindow>
#include <QScreen>
#include <QSocketNotifier>
#include <QTimer>
#include <cmath>
#include <unistd.h>

namespace {

// Mirrors RecordBorder.qml: a 3px accent frame with a 1px dark contrast line on
// each side, all drawn strictly OUTSIDE the recorded rect (the ffmpeg crop must
// never contain a frame pixel), plus a pulsing "REC h:mm:ss" badge placed just
// outside the region.
class BorderWindow : public QRasterWindow
{
public:
    BorderWindow(const QRect &region, const QColor &accent)
        : m_region(region), m_accent(accent)
    {
        setFlags(Qt::BypassWindowManagerHint | Qt::FramelessWindowHint
                 | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
                 | Qt::WindowTransparentForInput);
        QSurfaceFormat fmt = format();
        fmt.setAlphaBufferSize(8); // ARGB visual — translucency needs it on X11
        setFormat(fmt);
        m_clock.start();
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, [this] {
            m_pulse = !m_pulse;
            update();
        });
        tick->start(500);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(QRect(QPoint(0, 0), size()), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);

        const QRect &r = m_region;
        const int bw = 3;
        const QColor contrast(0, 0, 0, 140);
        // Concentric rings around the region: [outPad..inPad) outside its edge.
        const auto ring = [&p, &r](int outPad, int inPad, const QColor &c) {
            QRegion reg(r.adjusted(-outPad, -outPad, outPad, outPad));
            reg -= QRegion(r.adjusted(-inPad, -inPad, inPad, inPad));
            for (const QRect &part : reg)
                p.fillRect(part, c);
        };
        ring(bw + 1, bw, contrast); // outer contrast line
        ring(bw, 1, m_accent);      // accent frame (inner edge on the region)
        ring(1, 0, contrast);       // inner contrast line

        // Badge — above the region if there is room, else below, else hidden.
        QFont f = p.font();
        f.setPixelSize(12);
        f.setBold(true);
        const QString text = QStringLiteral("REC  ") + elapsedText();
        const int textW = QFontMetrics(f).horizontalAdvance(text);
        const int bh = 24;
        const int bwid = 8 + 6 + textW + 16; // dot + spacing + text + padding
        const bool roomAbove = r.y() - bh - 6 >= 0;
        const bool roomBelow = r.y() + r.height() + 6 + bh <= height();
        if (!roomAbove && !roomBelow)
            return;
        const int bx = qMax(0, qMin(width() - bwid, r.x()));
        const int by = roomAbove ? r.y() - bh - 6 : r.y() + r.height() + 6;
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 184));
        p.drawRoundedRect(QRect(bx, by, bwid, bh), 12, 12);
        QColor dot(0xff, 0x4d, 0x4d);
        dot.setAlphaF(m_pulse ? 1.0 : 0.25);
        p.setBrush(dot);
        p.drawEllipse(QRect(bx + 8, by + (bh - 8) / 2, 8, 8));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRect(bx + 8 + 8 + 6, by, textW, bh), Qt::AlignVCenter, text);
    }

private:
    QString elapsedText() const
    {
        const qint64 s = m_clock.elapsed() / 1000;
        const qint64 h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
        const auto pad = [](qint64 v) {
            return QString::number(v).rightJustified(2, QLatin1Char('0'));
        };
        return (h > 0 ? QString::number(h) + QLatin1Char(':') + pad(m) : pad(m))
               + QLatin1Char(':') + pad(sec);
    }

    QRect m_region; // window-local
    QColor m_accent;
    QElapsedTimer m_clock;
    bool m_pulse = true;
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
    //                        <fx> <fy> <fw> <fh> <#accent>
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

    BorderWindow win(QRect(QPoint(x0 - sg.x(), y0 - sg.y()),
                           QSize(x1 - x0, y1 - y0)),
                     accent);
    win.setGeometry(sg); // fill the monitor; override-redirect maps it as-is
    win.show();

    // Lifetime is the parent's stdin pipe: hideRecordBorder() closes it (or the
    // parent dies) → EOF → quit. No signals, no D-Bus, no polling.
    QSocketNotifier stdinWatch(0, QSocketNotifier::Read);
    QObject::connect(&stdinWatch, &QSocketNotifier::activated, &app, [&app] {
        char buf[64];
        if (::read(0, buf, sizeof buf) <= 0)
            app.quit();
    });

    return app.exec();
}

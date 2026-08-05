#include "ImageEffects.h"

#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

namespace UnisicImageEffects {

namespace {

// Every pattern paints ONE prepared stamp image N times. The text is laid out
// once even for a tiled watermark: this runs synchronously on the after-capture
// path, before save, clipboard, upload, history and the editor all read the
// same frame, and a few hundred text layouts at 4K would be a visible stall
// where a few hundred small alpha blits are not.
//
// Upper bound on those blits. A 4K capture tiles into roughly 250 cells at the
// spacings below, so this is a backstop against a pathological stamp (a
// one-character text at a tiny font, a 1x1 logo), not a limit the presets
// approach. Past it the spacing grows until the count fits, which keeps the
// pattern looking like a pattern instead of dropping half of it.
constexpr int kMaxStamps = 1024;

// Rotation shared by the two diagonal presets. Negative reads bottom-left to
// top-right, the direction a document watermark is expected to run.
constexpr qreal kDiagonalDegrees = -30.0;

bool isTiled(const QString &pattern)
{
    return pattern == QLatin1String("tile") || pattern == QLatin1String("diagonal");
}

// The user's Size setting, applied to whatever the pattern picked for itself.
// It multiplies AFTER the pattern's own clamps, so 100 is bit-for-bit the size
// the pattern would have chosen and the sliders below are pure multipliers; the
// outer bound is only a backstop against a hand-edited config asking for a
// 100000 px font, which would allocate a stamp larger than the capture.
//
// The lower bound is 8 px, not 4: a 4 px stamp is not merely illegible, on a
// minimal font set it paints NOTHING. Measured in an Arch build chroot, where
// fc-list is empty and Qt falls back to its three generic families - "Unisic"
// bold at 4 px covered 0 pixels, at 8 px it covered 132. Every pattern's own
// floor is 12 px or more, so this only bites when the Size setting scales one
// down, and it never moves a stamp at 100.
constexpr int kMinStampPx = 8;

int applyScale(int px, int scale, int hardMax)
{
    const qint64 scaled = qint64(px) * qBound(10, scale, 400) / 100;
    return int(qBound(qint64(kMinStampPx), scaled, qint64(hardMax)));
}

// How big one stamp is, as a fraction of the capture's short side. A tiled
// pattern needs a small stamp (it repeats), a band needs a large one (it is the
// only mark on the picture) - and the band is drawn at that size rather than
// scaled up from a small stamp, which would be visibly soft.
int textPixelSize(const QString &pattern, int shortSide, int scale)
{
    int px = qBound(14, shortSide / 26, 64);
    if (pattern == QLatin1String("band"))
        px = qBound(24, shortSide / 8, 400);
    else if (isTiled(pattern))
        px = qBound(12, shortSide / 42, 40);
    else if (pattern == QLatin1String("corners"))
        px = qBound(12, shortSide / 34, 48);
    return applyScale(px, scale, 2000);
}

int logoMaxSide(const QString &pattern, int shortSide, int diagonal, int scale)
{
    int side = qBound(48, shortSide / 4, 512);
    if (pattern == QLatin1String("band"))
        side = qBound(64, diagonal * 3 / 5, 4096);
    else if (isTiled(pattern))
        side = qBound(24, shortSide / 9, 192);
    else if (pattern == QLatin1String("corners"))
        side = qBound(32, shortSide / 6, 320);
    return applyScale(side, scale, 8192);
}

// The text with its drop shadow, on transparency, at full alpha - the opacity
// setting is applied once when the stamp is blitted, so it dims the mark as a
// whole rather than the letters and their shadow separately.
QImage textStamp(const QString &text, int fontPx, int maxWidth)
{
    QFont font;
    font.setPixelSize(fontPx);
    font.setBold(true);
    const QFontMetrics metrics(font);
    // Elided so a malformed imported setting cannot lay out a line wider than
    // the capture itself.
    const QString label = metrics.elidedText(text.trimmed(), Qt::ElideRight, qMax(1, maxWidth));
    if (label.isEmpty())
        return {};
    const int shadow = qMax(1, fontPx / 18);
    const int w = metrics.horizontalAdvance(label) + shadow + 2;
    const int h = metrics.height() + shadow + 2;
    QImage stamp(w, h, QImage::Format_ARGB32_Premultiplied);
    if (stamp.isNull())
        return {};
    stamp.fill(Qt::transparent);
    QPainter p(&stamp);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(font);
    const QPoint origin(1, metrics.ascent() + 1);
    p.setPen(QColor(0, 0, 0, 153)); // 60% black: readable over a light capture
    p.drawText(origin + QPoint(shadow, shadow), label);
    p.setPen(QColor(255, 255, 255));
    p.drawText(origin, label);
    return stamp;
}

// Where a single stamp sits, in the six card positions the Position setting
// offers. Only "single" and "corners" use this; the other presets cover the
// whole picture and ignore the position, which is why the row greys out.
QPoint singleOrigin(const QSize &canvas, const QSize &stamp, const QString &position, int margin)
{
    int x = margin;
    int y = canvas.height() - margin - stamp.height();
    if (position == QLatin1String("top-center") || position == QLatin1String("bottom-center")
        || position == QLatin1String("center"))
        x = qMax(margin, (canvas.width() - stamp.width()) / 2);
    else if (position == QLatin1String("top-right") || position == QLatin1String("bottom-right"))
        x = qMax(margin, canvas.width() - margin - stamp.width());
    if (position.startsWith(QLatin1String("top-")))
        y = margin;
    else if (position == QLatin1String("center"))
        y = qMax(margin, (canvas.height() - stamp.height()) / 2);
    return QPoint(x, qMax(0, y));
}

// Tiles `stamp` over the area the painter currently covers. `span` is the side
// of the square that has to be filled, centred on the painter's origin for the
// rotated case and taken as the image rect for the upright one.
void tile(QPainter &p, const QImage &stamp, const QRect &area)
{
    qreal stepX = stamp.width() * 1.8;
    qreal stepY = stamp.height() * 3.0;
    if (stepX < 1 || stepY < 1)
        return;
    // Grow the spacing until the blit count fits the cap, rather than stopping
    // half way through and leaving a visibly unfinished pattern.
    for (int guard = 0; guard < 8; ++guard) {
        const qreal cols = area.width() / stepX + 1;
        const qreal rows = area.height() / stepY + 1;
        if (cols * rows <= kMaxStamps)
            break;
        const qreal grow = qSqrt(cols * rows / kMaxStamps);
        stepX *= grow;
        stepY *= grow;
    }
    int row = 0;
    for (qreal y = area.top(); y < area.bottom(); y += stepY, ++row) {
        // Odd rows shifted half a step: a straight grid reads as a table, a
        // staggered one reads as a watermark.
        const qreal offset = (row % 2) ? stepX / 2 : 0;
        for (qreal x = area.left() - offset; x < area.right(); x += stepX)
            p.drawImage(QPointF(x, y), stamp);
    }
}

void paintStamps(QImage &output, const QImage &stamp, int opacity,
                 const QString &position, const QString &pattern)
{
    if (output.isNull() || stamp.isNull())
        return;
    const int shortSide = qMin(output.width(), output.height());
    const int margin = qMax(12, shortSide / 50);
    QPainter p(&output);
    p.setOpacity(qBound(0, opacity, 100) / 100.0);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (pattern == QLatin1String("corners")) {
        const int right = qMax(margin, output.width() - margin - stamp.width());
        const int bottom = qMax(margin, output.height() - margin - stamp.height());
        for (const QPoint &at : { QPoint(margin, margin), QPoint(right, margin),
                                  QPoint(margin, bottom), QPoint(right, bottom) })
            p.drawImage(at, stamp);
        return;
    }
    if (pattern == QLatin1String("band")) {
        p.translate(output.rect().center());
        p.rotate(kDiagonalDegrees);
        p.drawImage(QPointF(-stamp.width() / 2.0, -stamp.height() / 2.0), stamp);
        return;
    }
    if (pattern == QLatin1String("tile")) {
        tile(p, stamp, output.rect().adjusted(margin, margin, 0, 0));
        return;
    }
    if (pattern == QLatin1String("diagonal")) {
        // Rotated about the centre, so the area that must be filled is the
        // square on the image's diagonal - anything smaller leaves bare
        // triangles in the corners.
        const int diag = int(qSqrt(qreal(output.width()) * output.width()
                                   + qreal(output.height()) * output.height()))
                         + 2;
        p.translate(output.rect().center());
        p.rotate(kDiagonalDegrees);
        tile(p, stamp, QRect(-diag / 2, -diag / 2, diag, diag));
        return;
    }
    p.drawImage(singleOrigin(output.size(), stamp.size(), position, margin), stamp);
}

// One writable frame, handed on to every downstream action. This is an explicit
// after-capture step, never a paint-loop effect, so the copy is paid once.
QImage writableCopy(const QImage &source)
{
    return source.format() == QImage::Format_ARGB32_Premultiplied
               ? source.copy()
               : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Preview sizes are small and fixed by the Settings card; this only guards
// against a QML binding asking for a degenerate or absurd one.
constexpr int kMinSide = 80;
constexpr int kMaxSide = 1600;

// The app palette, painted as picture content rather than as UI chrome. The
// preview has to look like something the app captured, and these are the
// colours the rest of the app is made of.
const QColor kInk(0x17, 0x15, 0x3B);
const QColor kPanel(0x2E, 0x23, 0x6C);
const QColor kRaised(0x43, 0x3D, 0x8B);
const QColor kAccent(0xC8, 0xAC, 0xD6);
const QColor kPaper(0xF4, 0xF2, 0xF8);

void paintTextBars(QPainter &p, const QRect &area, const QColor &color, int rows)
{
    const int gap = qMax(2, area.height() / (rows * 3));
    const int bar = qMax(2, (area.height() - gap * (rows - 1)) / rows);
    for (int i = 0; i < rows; ++i) {
        // Ragged line lengths: a block of equal bars reads as a table, and the
        // point is to see the mark over text-sized detail.
        const int w = area.width() * (100 - (i * 17) % 45) / 100;
        p.fillRect(QRect(area.left(), area.top() + i * (bar + gap), w, bar), color);
    }
}

} // namespace

QImage watermarkText(const QImage &source, const QString &text, int opacity,
                     const QString &position, const QString &pattern, int scale)
{
    if (source.isNull() || text.trimmed().isEmpty() || opacity <= 0)
        return source;
    const int shortSide = qMin(source.width(), source.height());
    const int margin = qMax(12, shortSide / 50);
    // A tiled stamp is measured against a cell, not the whole picture: eliding
    // it to the full width would let one long line become the tile.
    const int maxWidth = isTiled(pattern)
                             ? applyScale(qMax(1, source.width() / 3), scale, source.width())
                             : qMax(1, source.width() - 2 * margin);
    const QImage stamp = textStamp(text, textPixelSize(pattern, shortSide, scale), maxWidth);
    if (stamp.isNull())
        return source;
    QImage output = writableCopy(source);
    paintStamps(output, stamp, opacity, position, pattern);
    return output;
}

QImage watermarkImage(const QImage &source, const QImage &watermark, int opacity,
                      const QString &position, const QString &pattern, int scale)
{
    if (source.isNull() || watermark.isNull() || opacity <= 0)
        return source;
    const int shortSide = qMin(source.width(), source.height());
    const int diag = int(qSqrt(qreal(source.width()) * source.width()
                               + qreal(source.height()) * source.height()));
    const int maxSide = logoMaxSide(pattern, shortSide, diag, scale);
    const QImage logo = watermark.scaled(maxSide, maxSide, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    if (logo.isNull())
        return source;
    QImage output = writableCopy(source);
    paintStamps(output, logo, opacity, position, pattern);
    return output;
}

QImage mockCapture(const QSize &size)
{
    const int w = qBound(kMinSide, size.width(), kMaxSide);
    const int h = qBound(kMinSide, size.height(), kMaxSide);
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull())
        return {};
    img.fill(kPaper);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // A window: title bar, sidebar, a page of text.
    const int bar = qMax(8, h / 9);
    p.fillRect(QRect(0, 0, w, bar), kInk);
    const int dot = qMax(3, bar / 3);
    for (int i = 0; i < 3; ++i)
        p.fillRect(QRect(dot + i * dot * 2, (bar - dot) / 2, dot, dot), kRaised);

    const int side = qMax(24, w / 4);
    p.fillRect(QRect(0, bar, side, h - bar), kPanel);
    paintTextBars(p, QRect(side / 6, bar + h / 12, side * 2 / 3, h / 3), kAccent, 5);

    paintTextBars(p, QRect(side + w / 20, bar + h / 10, w - side - w / 10, h / 3),
                  QColor(0x8A, 0x86, 0x9E), 6);

    // A dark picture in the lower right, so the same stamp is judged over a
    // light and a dark background in one look.
    const QRect photo(side + w / 20, h * 6 / 10, w - side - w / 10, h * 3 / 10);
    QLinearGradient g(photo.topLeft(), photo.bottomRight());
    g.setColorAt(0.0, kRaised);
    g.setColorAt(1.0, kInk);
    p.fillRect(photo, g);
    return img;
}

} // namespace UnisicImageEffects

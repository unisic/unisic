#include "ImageEffects.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace UnisicImageEffects {

QImage watermarkText(const QImage &source, const QString &text, int opacity,
                     const QString &position)
{
    if (source.isNull() || text.trimmed().isEmpty() || opacity <= 0)
        return source;

    // This is an explicit after-capture action, never a paint-loop effect.
    // Make exactly one writable frame, then hand that same frame to every
    // downstream action (save, clipboard, upload, history, editor). The text
    // is elided before painting so a malformed imported setting cannot create
    // an oversized text layout for a full-resolution capture.
    QImage output = source.format() == QImage::Format_ARGB32_Premultiplied
                        ? source.copy()
                        : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int shortSide = qMin(output.width(), output.height());
    const int fontPx = qBound(14, shortSide / 26, 64);
    const int margin = qMax(12, fontPx);
    QFont font = painter.font();
    font.setPixelSize(fontPx);
    font.setBold(true);
    painter.setFont(font);

    const QFontMetrics metrics(font);
    const QString label = metrics.elidedText(text.trimmed(), Qt::ElideRight,
                                             qMax(1, output.width() - 2 * margin));
    const int textWidth = metrics.horizontalAdvance(label);
    const int baselineMin = margin + metrics.ascent();
    const int baselineMax = qMax(baselineMin, output.height() - margin - metrics.descent());
    int x = margin;
    int y = baselineMax;

    if (position == QLatin1String("top-center") || position == QLatin1String("bottom-center")
        || position == QLatin1String("center"))
        x = qMax(margin, (output.width() - textWidth) / 2);
    else if (position == QLatin1String("top-right") || position == QLatin1String("bottom-right"))
        x = qMax(margin, output.width() - margin - textWidth);

    if (position == QLatin1String("top-left") || position == QLatin1String("top-center")
        || position == QLatin1String("top-right"))
        y = baselineMin;
    else if (position == QLatin1String("center"))
        y = qBound(baselineMin,
                   (output.height() + metrics.ascent() - metrics.descent()) / 2,
                   baselineMax);

    const int alpha = qBound(0, opacity, 100) * 255 / 100;
    const int shadow = qMax(1, fontPx / 18);
    painter.setPen(QColor(0, 0, 0, alpha * 3 / 5));
    painter.drawText(QPoint(x + shadow, y + shadow), label);
    painter.setPen(QColor(255, 255, 255, alpha));
    painter.drawText(QPoint(x, y), label);
    return output;
}

QImage watermarkImage(const QImage &source, const QImage &watermark, int opacity,
                      const QString &position)
{
    if (source.isNull() || watermark.isNull() || opacity <= 0)
        return source;
    QImage output = source.format() == QImage::Format_ARGB32_Premultiplied
                        ? source.copy()
                        : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int shortSide = qMin(output.width(), output.height());
    const int maxSide = qBound(48, shortSide / 4, 512);
    const QImage logo = watermark.scaled(maxSide, maxSide, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
    const int margin = qMax(12, shortSide / 50);
    int x = margin, y = output.height() - margin - logo.height();
    if (position == QLatin1String("top-center") || position == QLatin1String("bottom-center")
        || position == QLatin1String("center"))
        x = (output.width() - logo.width()) / 2;
    else if (position == QLatin1String("top-right") || position == QLatin1String("bottom-right"))
        x = output.width() - margin - logo.width();
    if (position.startsWith(QLatin1String("top-")))
        y = margin;
    else if (position == QLatin1String("center"))
        y = (output.height() - logo.height()) / 2;
    QPainter painter(&output);
    painter.setOpacity(qBound(0, opacity, 100) / 100.0);
    painter.drawImage(QPoint(x, y), logo);
    return output;
}

} // namespace UnisicImageEffects

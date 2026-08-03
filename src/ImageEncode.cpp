#include "ImageEncode.h"

#include "FilenameTemplate.h"
#include "media/FfmpegUtil.h"

#include <QBuffer>
#include <QImage>

namespace ImageEncode {

bool hasTransparency(const QImage &img)
{
    if (!img.hasAlphaChannel())
        return false;
    const int stepY = qMax(1, img.height() / 256);
    const int stepX = qMax(1, img.width() / 256);
    // pixelColor() reads alpha correctly for any format (incl.
    // ARGB32_Premultiplied), so no full-frame convertToFormat() copy is needed
    // and the sampling stays ~free.
    for (int y = 0; y < img.height(); y += stepY)
        for (int x = 0; x < img.width(); x += stepX)
            if (img.pixelColor(x, y).alpha() < 255)
                return true;
    return false;
}

Result encode(const QImage &img, const QString &format, int quality)
{
    Result r;
    if (img.isNull())
        return r;
    // extensionFor() also normalizes "jpeg" and turns anything unknown into
    // png, so the switch below needs no default case of its own.
    const QString want = FilenameTemplate::extensionFor(format);
    const int q = qBound(1, quality, 100);

    if (want == QLatin1String("gif")) {
        // Qt writes no GIF at all; ffmpeg does. Empty means it is not installed
        // or the encode failed, and PNG is then the right answer for every
        // caller: bytes that decode beat the extension that was asked for.
        r.bytes = FfmpegUtil::encodeStillGif(img, q);
        if (!r.bytes.isEmpty()) {
            r.format = QStringLiteral("gif");
            r.mime = QStringLiteral("image/gif");
            return r;
        }
        r.fallbackReason = QStringLiteral("gif");
    } else if (want == QLatin1String("jpg")) {
        // JPEG has no alpha: a capture carrying transparency would flatten to
        // solid black, so it becomes a PNG instead of a black rectangle.
        if (hasTransparency(img)) {
            r.fallbackReason = QStringLiteral("alpha");
        } else {
            QBuffer buf(&r.bytes);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "JPG", q)) {
                r.format = QStringLiteral("jpg");
                r.mime = QStringLiteral("image/jpeg");
                return r;
            }
            // A plugin that is present but refuses this image (a size past the
            // format's own limit is the realistic one). PNG below still gets
            // the pixels out, and the reason keeps the name honest.
            r.bytes.clear();
            r.fallbackReason = QStringLiteral("encoder");
        }
    } else if (want == QLatin1String("webp")) {
        QBuffer buf(&r.bytes);
        buf.open(QIODevice::WriteOnly);
        if (img.save(&buf, "WEBP", q)) {
            r.format = QStringLiteral("webp");
            r.mime = QStringLiteral("image/webp");
            return r;
        }
        // WebP tops out at 16383 px per side, which a stitched multi-monitor
        // capture can exceed - so this branch is reachable, not theoretical.
        r.bytes.clear();
        r.fallbackReason = QStringLiteral("encoder");
    }

    // PNG: the asked format, or the fallback for everything above. A failure
    // here leaves bytes empty, which is the caller's "nothing was produced".
    QBuffer buf(&r.bytes);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "PNG")) {
        r.bytes.clear();
        return r;
    }
    r.format = QStringLiteral("png");
    r.mime = QStringLiteral("image/png");
    return r;
}

} // namespace ImageEncode

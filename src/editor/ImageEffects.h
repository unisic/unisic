#pragma once

#include <QImage>
#include <QString>

// Small, export-only image transforms shared by the after-capture pipeline.
// They deliberately operate in image pixels: capture output must not depend on
// the display DPR or on the QML scene graph.
namespace UnisicImageEffects {

// Adds a readable text or logo watermark. `position` is one of the six
// card-style spots and applies to the "single" pattern only; `pattern` is one
// of "single", "tile", "diagonal", "corners", "band" (anything else reads as
// "single"), and the patterns that cover the whole picture ignore the position.
// `scale` is a percent of the size the pattern picks for itself, so 100 is the
// pattern's own default and the number means the same on any capture size.
// Empty text, a null logo and a null source are no-ops (and preserve implicit
// sharing). The stamp is prepared once and blitted N times, so a tiled
// watermark costs small alpha blends rather than N text layouts.
QImage watermarkText(const QImage &source, const QString &text, int opacity,
                     const QString &position, const QString &pattern, int scale = 100);
QImage watermarkImage(const QImage &source, const QImage &watermark, int opacity,
                      const QString &position, const QString &pattern, int scale = 100);

// The stand-in capture the Settings watermark preview is stamped onto. It is
// NOT a UI surface, it plays the part of a screenshot, so it is painted in
// image pixels here rather than assembled from Theme tokens in QML: the point
// of the preview is that the code which stamps a real capture stamps this one,
// and that code takes a QImage.
//
// It carries a light half and a dark half plus text-sized detail on purpose. A
// mark that reads perfectly over one flat colour can vanish over the other, and
// opacity and size are exactly the two settings that decide which, so a flat
// backdrop would make the preview a liar.
QImage mockCapture(const QSize &size);

} // namespace UnisicImageEffects

#pragma once

#include <QImage>
#include <QString>

// Small, export-only image transforms shared by the after-capture pipeline.
// They deliberately operate in image pixels: capture output must not depend on
// the display DPR or on the QML scene graph.
namespace UnisicImageEffects {

// Adds a readable text watermark in one of the six card-style positions.
// Empty text and a null source are no-ops (and preserve implicit sharing).
QImage watermarkText(const QImage &source, const QString &text, int opacity,
                     const QString &position);
QImage watermarkImage(const QImage &source, const QImage &watermark, int opacity,
                      const QString &position);

} // namespace UnisicImageEffects

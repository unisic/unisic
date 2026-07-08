#pragma once
#include <QVector>
#include <QRect>
#include <QImage>

// Lightweight, dependency-free "pick an object" helpers. Pure CPU/Qt — no ML
// runtime — tuned to run in well under a second on screenshot-sized inputs.
namespace ObjectDetector {
    // Edge-detection candidate rects (windows, panels, cards, buttons) in
    // image-pixel coordinates, sorted by area ascending, so a point-in-rect
    // search hits the innermost (smallest containing) element.
    QVector<QRect> detect(const QImage &img);

    // Foreground/background segmentation inside `region`. Assumes the user's
    // rectangle surrounds the object, so the region's border ring is a
    // background prior; color models (k-means, GrabCut-style refinement
    // without the graph cut) label each pixel, then connectivity cleanup
    // keeps the dominant object and feathers the edge. Returns a
    // Format_Grayscale8 mask of region.size() (255 = keep / foreground,
    // 0 = remove), or a null image when segmentation degenerates — the
    // caller then falls back to the plain rectangular crop.
    QImage segment(const QImage &img, const QRect &region);
}

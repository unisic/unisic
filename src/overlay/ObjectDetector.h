#pragma once
#include <QVector>
#include <QRect>

class QImage;

// Lightweight, dependency-free "pick an object" helper. Runs an edge detector
// over the frozen screenshot and returns candidate rectangles (windows, panels,
// cards, buttons) so the overlay can highlight and capture the one under the
// cursor. Not a segmentation model — a pragmatic gradient/connected-component
// heuristic that runs in tens of milliseconds.
namespace ObjectDetector {
    // Candidate rects in image-pixel coordinates, sorted by area ascending, so a
    // point-in-rect search hits the innermost (smallest containing) element.
    QVector<QRect> detect(const QImage &img);
}

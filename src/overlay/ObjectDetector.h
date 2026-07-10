#pragma once
#include <QVector>
#include <QRect>
#include <QImage>

// Lightweight, dependency-free "pick an object" helpers. Pure CPU/Qt — no ML
// runtime — tuned to run in well under a second on screenshot-sized inputs.
namespace ObjectDetector {
    // Candidate metadata lets callers distinguish a tightly bounded control
    // from a content group or a window-sized container.  `confidence` is a
    // visual-evidence score in the [0, 1] range, not a claim about semantic
    // identity; Wayland deliberately does not expose another app's widget
    // tree, so pixels can never provide that guarantee.
    enum class CandidateKind { Element, Group, Container, Window, Screen };
    struct Candidate {
        QRect rect;
        float confidence = 0.0f;
        CandidateKind kind = CandidateKind::Element;
    };

    // Detailed variant used by the interactive picker.  It preserves the
    // source/evidence of every region so the UI can make a stable choice
    // instead of treating a faint patch in a video like a window border.
    QVector<Candidate> detectCandidates(const QImage &img);

    // Edge-detection candidate rects (windows, panels, cards, buttons) in
    // image-pixel coordinates, sorted by area ascending, so a point-in-rect
    // search hits the innermost (smallest containing) element. Kept as a
    // lightweight compatibility wrapper for non-interactive callers.
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

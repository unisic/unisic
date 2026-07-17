#pragma once
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QVector>

class QPainter;

// Draws the pointer, its highlight halo and click ripples into a recorded
// frame, in the frame's own pixel space.
//
// Why this exists as a painter at all: asking the ScreenCast portal for
// CursorMetadata means the compositor stops compositing the pointer into the
// stream, so the frames arrive with NO cursor. Everything the viewer sees of
// the pointer from then on is drawn here. (Unisic Studio solves the same
// problem in QML and renders it offscreen per frame — unusable here, where
// frames are burned live at the sample rate.)
//
// Not thread-safe and not meant to be: the recorder owns one and paints from
// the sampling timer.
class CursorOverlayPainter
{
public:
    struct Style {
        // The halo is the point of the feature: it is what makes the pointer
        // findable in a downscaled screen recording.
        bool highlight = true;
        QColor highlightColor = QColor(255, 214, 0);
        int highlightRadius = 26;   // px at 100% scale
        // Click feedback.
        bool ripple = true;
        QColor rippleColor = QColor(255, 255, 255);
        int rippleRadius = 46;      // px the ring grows to
        int rippleMs = 450;         // lifetime of one ripple
        // Drawing the pointer itself is NOT optional in metadata mode (the
        // frames have none), but stays switchable for a deliberate
        // pointer-less recording.
        bool drawCursor = true;
    };

    void setStyle(const Style &s) { m_style = s; }
    const Style &style() const { return m_style; }

    // Cursor bitmaps as they arrive from the grabber, keyed by spa shape id.
    void setShape(int id, const QImage &bitmap, const QPoint &hotspot);
    bool hasShape(int id) const { return m_shapes.contains(id); }

    // Latest known pointer state, in FRAME pixels (the caller subtracts any
    // crop offset). Feeding an invisible cursor keeps the last position.
    void setCursor(const QPointF &posInFrame, bool visible, int shapeId);

    // A click at `tNs` (CLOCK_MONOTONIC). Ripples are anchored to the pointer
    // position at the time of the click, not the live one — otherwise a ripple
    // slides across the screen when the user clicks and immediately moves.
    void addClick(qint64 tNs);

    // True when anything would actually be drawn — lets the recorder skip the
    // whole compositing copy on a frame with nothing on it.
    bool hasContent(qint64 nowNs) const;

    // Paints into `p`, which must already be set up in frame pixel space.
    void paint(QPainter &p, qint64 nowNs);

    void reset();

private:
    struct Ripple { qint64 tNs = 0; QPointF pos; };
    struct Shape { QImage image; QPoint hotspot; };

    void drawVectorPointer(QPainter &p) const;

    Style m_style;
    QHash<int, Shape> m_shapes;
    QVector<Ripple> m_ripples;
    QPointF m_pos;
    bool m_visible = false;
    bool m_havePos = false;
    int m_shapeId = 0;
};

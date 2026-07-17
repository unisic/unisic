#include "CursorOverlayPainter.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QtMath>

// A ripple older than this is dropped. Kept as one place so paint() and
// hasContent() can never disagree about what is still alive.
static bool rippleAlive(qint64 nowNs, qint64 tNs, int lifeMs)
{
    const qint64 ageMs = (nowNs - tNs) / 1000000LL;
    return ageMs >= 0 && ageMs <= lifeMs;
}

void CursorOverlayPainter::setShape(int id, const QImage &bitmap, const QPoint &hotspot)
{
    if (id == 0 || bitmap.isNull())
        return;
    Shape s;
    // Convert once, here: paint() runs at the sample rate and drawImage of a
    // non-premultiplied ARGB32 would convert on every single frame.
    s.image = bitmap.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    s.hotspot = hotspot;
    m_shapes.insert(id, s);
}

void CursorOverlayPainter::setCursor(const QPointF &posInFrame, bool visible, int shapeId)
{
    m_pos = posInFrame;
    m_visible = visible;
    m_havePos = true;
    if (shapeId != 0)
        m_shapeId = shapeId;
}

void CursorOverlayPainter::addClick(qint64 tNs)
{
    if (!m_havePos)
        return;
    m_ripples.append(Ripple{tNs, m_pos});
}

bool CursorOverlayPainter::hasContent(qint64 nowNs) const
{
    if (!m_havePos)
        return false;
    if (m_visible && (m_style.highlight || m_style.drawCursor))
        return true;
    if (!m_style.ripple)
        return false;
    for (const Ripple &r : m_ripples) {
        if (rippleAlive(nowNs, r.tNs, m_style.rippleMs))
            return true;
    }
    return false;
}

void CursorOverlayPainter::reset()
{
    m_ripples.clear();
    m_havePos = false;
    m_visible = false;
    m_shapeId = 0;
    // Shapes deliberately survive: ids stay valid for the compositor session
    // and re-decoding them costs a frame with no pointer on it.
}

void CursorOverlayPainter::drawVectorPointer(QPainter &p) const
{
    // Unisic's own pointer: chosen deliberately for a big cursor (a scaled
    // bitmap shows its pixels), and used as the fallback when the compositor
    // never sent a bitmap for the current shape id — in metadata mode nothing
    // else would draw a pointer at all.
    //
    // The hotspot is the path's origin (0,0), i.e. the tip, so the caller can
    // translate straight to the cursor position.
    static const QPointF pts[] = {
        {0, 0}, {0, 16.5}, {4.2, 12.6}, {6.9, 18.6}, {9.6, 17.4}, {6.9, 11.6}, {12.3, 11.4}
    };
    QPainterPath path(pts[0]);
    for (int i = 1; i < int(sizeof(pts) / sizeof(pts[0])); ++i)
        path.lineTo(pts[i]);
    path.closeSubpath();
    // A drop shadow, not just the outline: a white pointer on a white document
    // is otherwise a white hole, and this pointer exists to be seen.
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.translate(0.9, 1.2);
    p.drawPath(path);
    p.restore();
    p.setPen(QPen(Qt::black, 1.4));
    p.setBrush(Qt::white);
    p.drawPath(path);
}

void CursorOverlayPainter::paint(QPainter &p, qint64 nowNs)
{
    if (!m_havePos)
        return;

    // Expire ripples first so a long recording cannot accumulate them.
    for (int i = m_ripples.size() - 1; i >= 0; --i) {
        if (!rippleAlive(nowNs, m_ripples[i].tNs, m_style.rippleMs))
            m_ripples.remove(i);
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 1) Halo, UNDER everything: it is a backdrop for the pointer, so drawing
    //    it after would wash the pointer out.
    if (m_visible && m_style.highlight && m_style.highlightRadius > 0) {
        const qreal r = m_style.highlightRadius;
        QRadialGradient g(m_pos, r);
        QColor c = m_style.highlightColor;
        c.setAlpha(170);
        g.setColorAt(0.0, c);
        c.setAlpha(110);
        g.setColorAt(0.55, c);
        c.setAlpha(0);
        g.setColorAt(1.0, c);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(m_pos, r, r);
    }

    // 2) Ripples: a ring that grows and fades out of the click point.
    if (m_style.ripple) {
        for (const Ripple &rp : m_ripples) {
            const qreal t = qBound(0.0, qreal((nowNs - rp.tNs) / 1000000LL) / qMax(1, m_style.rippleMs), 1.0);
            // Ease-out: fast at the start, so a quick click still reads.
            const qreal eased = 1.0 - (1.0 - t) * (1.0 - t);
            const qreal radius = m_style.rippleRadius * eased;
            if (radius <= 0.5)
                continue;
            QColor ring = m_style.rippleColor;
            ring.setAlphaF(qBound(0.0, (1.0 - t) * 0.85, 1.0));
            QColor fill = m_style.rippleColor;
            fill.setAlphaF(qBound(0.0, (1.0 - t) * 0.25, 1.0));
            p.setBrush(fill);
            p.setPen(QPen(ring, qMax(1.5, m_style.rippleRadius * 0.06)));
            p.drawEllipse(rp.pos, radius, radius);
        }
    }

    // 3) The pointer itself, on top. The compositor's per-shape bitmap is used
    //    (so it switches arrow → hand → I-beam with the real cursor); the built-in
    //    vector pointer is only a FALLBACK for the brief moment before the first
    //    bitmap arrives — in metadata mode nothing else would draw a pointer.
    if (m_visible && m_style.drawCursor) {
        const auto it = m_shapes.constFind(m_shapeId);
        const bool haveBitmap = it != m_shapes.constEnd() && !it->image.isNull();
        if (haveBitmap) {
            // A 1:1 blit, snapped to the pixel grid. The smoothed position is
            // fractional by design, and blitting a bitmap at a fractional offset
            // resamples it — that is what made the system cursor look blurry.
            const QPoint topLeft(qRound(m_pos.x() - it->hotspot.x()),
                                 qRound(m_pos.y() - it->hotspot.y()));
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.drawImage(topLeft, it->image);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        } else {
            // No bitmap yet (first frames of a recording): the built-in vector
            // pointer stands in until the compositor sends the real one.
            p.translate(m_pos);
            drawVectorPointer(p);
        }
    }

    p.restore();
}

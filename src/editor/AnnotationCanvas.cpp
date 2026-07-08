#include "AnnotationCanvas.h"
#include "overlay/ObjectDetector.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QCursor>
#include <QtMath>
#include <QtConcurrent>
#include <QFutureWatcher>

AnnotationCanvas::AnnotationCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setCursor(Qt::CrossCursor);
}

void AnnotationCanvas::setImage(const QImage &img)
{
    m_base = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // Work in raw pixels: a non-1 DPR would make QPainter::drawImage rescale.
    m_base.setDevicePixelRatio(1.0);
    m_items.clear();
    m_undo.clear();
    m_redo.clear();
    m_stepCounter = 0;
    m_selection = {};
    // Object-pick state belongs to the previous image; stale rects would also
    // block re-detection (setTool only detects when candidates are empty).
    m_objectCandidates.clear();
    m_hoverObject = QRect();
    clearObjectMask();
    if (m_detectWatcher) {
        // Let the stale run finish detached; its lambda must not deliver.
        m_detectWatcher->disconnect(this);
        connect(m_detectWatcher, &QFutureWatcher<QVector<QRect>>::finished,
                m_detectWatcher, &QObject::deleteLater);
        m_detectWatcher = nullptr;
    }
    emit imageChanged();
    emit historyChanged();
    emit selectionRectChanged();
    emit renderScaleChanged();
    update();
}

qreal AnnotationCanvas::renderScale() const
{
    if (m_base.isNull() || width() <= 0) return 1.0;
    return width() / m_base.width();
}

void AnnotationCanvas::setTool(int t)
{
    if (m_tool == t) return;
    const bool leftObjectPick = (m_tool == ObjectPick);
    m_tool = t;
    emit toolChanged();
    if (leftObjectPick)
        clearObjectMask();
    if (t == ObjectPick) {
        m_hoverObject = QRect();
        // Detect candidate objects once, off the GUI thread (a few tens of ms).
        if (m_objectCandidates.isEmpty() && !m_base.isNull() && !m_detectWatcher) {
            m_detectWatcher = new QFutureWatcher<QVector<QRect>>(this);
            connect(m_detectWatcher, &QFutureWatcher<QVector<QRect>>::finished, this, [this] {
                m_objectCandidates = m_detectWatcher->result();
                m_detectWatcher->deleteLater();
                m_detectWatcher = nullptr;
                update();
            });
            m_detectWatcher->setFuture(QtConcurrent::run(ObjectDetector::detect, m_base));
        }
    }
    update();
}

void AnnotationCanvas::setStrokeColor(const QColor &c)
{
    if (m_color == c) return;
    m_color = c;
    emit strokeColorChanged();
}

void AnnotationCanvas::setShapeFillColor(const QColor &c)
{
    if (m_fillColor == c) return;
    m_fillColor = c;
    emit shapeFillColorChanged();
}

void AnnotationCanvas::setShapeFillEnabled(bool on)
{
    if (m_fillEnabled == on) return;
    m_fillEnabled = on;
    emit shapeFillEnabledChanged();
}

void AnnotationCanvas::setStrokeWidth(int w)
{
    if (m_strokeWidth == w) return;
    m_strokeWidth = w;
    emit strokeWidthChanged();
}

void AnnotationCanvas::setFontSize(int s)
{
    if (m_fontSize == s) return;
    m_fontSize = s;
    emit fontSizeChanged();
}

void AnnotationCanvas::setSelectionMode(bool on)
{
    if (m_selectionMode == on) return;
    m_selectionMode = on;
    emit selectionModeChanged();
    update();
}

void AnnotationCanvas::startSegmentation()
{
    if (m_base.isNull() || !hasSelection())
        return;
    const QRect r = m_selection.toAlignedRect().intersected(m_base.rect());
    m_objectMask = {};
    m_objectMaskRect = {};
    m_maskOverlay = {};
    const quint64 seq = ++m_segmentSeq; // invalidates any in-flight run
    if (r.width() < 12 || r.height() < 12) {
        emit segmentingChanged();
        update();
        return;
    }
    ++m_segmentActive;
    emit segmentingChanged();
    auto *w = new QFutureWatcher<QImage>(this);
    connect(w, &QFutureWatcher<QImage>::finished, this, [this, w, seq, r] {
        const QImage mask = w->result();
        w->deleteLater();
        --m_segmentActive;
        if (seq == m_segmentSeq && !mask.isNull()) {
            m_objectMask = mask;
            m_objectMaskRect = r;
            // Preview overlay: dark tint exactly where the mask removes pixels.
            QImage inv = mask;
            inv.invertPixels();
            m_maskOverlay = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
            m_maskOverlay.fill(QColor(23, 21, 59, 200));
            QPainter p(&m_maskOverlay);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.drawImage(0, 0, inv.convertToFormat(QImage::Format_Alpha8));
            p.end();
        }
        emit segmentingChanged();
        update();
    });
    const QImage base = m_base; // shared copy; the worker only reads it
    w->setFuture(QtConcurrent::run([base, r] { return ObjectDetector::segment(base, r); }));
}

void AnnotationCanvas::clearObjectMask()
{
    ++m_segmentSeq; // drop any in-flight result
    const bool had = !m_objectMask.isNull();
    m_objectMask = {};
    m_objectMaskRect = {};
    m_maskOverlay = {};
    if (had)
        emit segmentingChanged();
}

QPointF AnnotationCanvas::toImage(qreal itemX, qreal itemY) const
{
    const qreal s = renderScale();
    return s > 0 ? QPointF(itemX / s, itemY / s) : QPointF(itemX, itemY);
}

QRectF AnnotationCanvas::selectionInItemCoords() const
{
    const qreal s = renderScale();
    return QRectF(m_selection.x() * s, m_selection.y() * s,
                  m_selection.width() * s, m_selection.height() * s);
}

void AnnotationCanvas::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickPaintedItem::geometryChange(n, o);
    emit renderScaleChanged();
}

void AnnotationCanvas::pushUndo()
{
    m_undo.append({m_base, m_items, m_stepCounter});
    if (m_undo.size() > 100)
        m_undo.removeFirst();
    m_redo.clear();
    emit historyChanged();
}

void AnnotationCanvas::undo()
{
    if (m_undo.isEmpty()) return;
    m_redo.append({m_base, m_items, m_stepCounter});
    Snapshot s = m_undo.takeLast();
    const bool baseChanged = s.base.size() != m_base.size();
    m_base = s.base;
    m_items = s.items;
    m_stepCounter = s.stepCounter; // keep step numbering in sync with the items
    if (baseChanged) { emit imageChanged(); emit renderScaleChanged(); }
    emit historyChanged();
    update();
}

void AnnotationCanvas::redo()
{
    if (m_redo.isEmpty()) return;
    m_undo.append({m_base, m_items, m_stepCounter});
    Snapshot s = m_redo.takeLast();
    const bool baseChanged = s.base.size() != m_base.size();
    m_base = s.base;
    m_items = s.items;
    m_stepCounter = s.stepCounter;
    if (baseChanged) { emit imageChanged(); emit renderScaleChanged(); }
    emit historyChanged();
    update();
}

void AnnotationCanvas::clearAnnotations()
{
    if (m_items.isEmpty()) return;
    pushUndo();
    m_items.clear();
    m_stepCounter = 0;
    update();
}

void AnnotationCanvas::commitText(qreal imgX, qreal imgY, const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    pushUndo();
    Annot a;
    a.type = Text;
    a.rect = QRectF(imgX, imgY, 0, 0);
    a.color = m_color;
    a.text = text;
    a.fontSize = m_fontSize;
    m_items.append(a);
    update();
}

void AnnotationCanvas::nudgeSelection(qreal dx, qreal dy)
{
    if (!hasSelection()) return;
    // Clamp the translation instead of normalizeSelection(): intersecting at
    // the image edge would progressively *shrink* the selection.
    const QRectF bounds(QPointF(0, 0), QSizeF(m_base.size()));
    dx = qBound(bounds.left() - m_selection.left(), dx, bounds.right() - m_selection.right());
    dy = qBound(bounds.top() - m_selection.top(), dy, bounds.bottom() - m_selection.bottom());
    m_selection.translate(dx, dy);
    emit selectionRectChanged();
    // The mask was computed for the old rectangle — re-segment in place.
    if (m_tool == ObjectPick)
        startSegmentation();
    update();
}

void AnnotationCanvas::selectAll()
{
    m_selection = QRectF(QPointF(0, 0), QSizeF(m_base.size()));
    emit selectionRectChanged();
    update();
}

void AnnotationCanvas::normalizeSelection()
{
    m_selection = m_selection.normalized()
                      .intersected(QRectF(QPointF(0, 0), QSizeF(m_base.size())));
}

void AnnotationCanvas::applyCrop()
{
    if (!hasSelection()) return;
    pushUndo();
    const QRect r = m_selection.toAlignedRect().intersected(m_base.rect());
    m_base = m_base.copy(r);
    // Shift annotations into the new coordinate space.
    for (Annot &a : m_items) {
        a.rect.translate(-r.x(), -r.y());
        for (QPointF &p : a.points)
            p -= QPointF(r.x(), r.y());
    }
    m_selection = {};
    // Candidates and masks are in pre-crop coordinates now — invalidate.
    m_objectCandidates.clear();
    m_hoverObject = QRect();
    clearObjectMask();
    emit imageChanged();
    emit renderScaleChanged();
    emit selectionRectChanged();
    update();
}

// ---------------------------------------------------------------- painting

static qreal arrowHeadLen(qreal w) { return qMax(12.0, w * 4.5); }

// Draws the filled arrowhead with its apex exactly at `to`. Assumes the brush
// is already set to the fill color.
static void drawArrowHead(QPainter &p, const QPointF &from, const QPointF &to, qreal w)
{
    const qreal len = arrowHeadLen(w);
    QLineF line(to, from);
    if (line.length() < 1) return;
    line.setLength(len);
    const qreal angle = qDegreesToRadians(26.0);
    const QPointF d = line.p2() - line.p1(); // tip -> back-toward-shaft, length len
    auto rot = [&](qreal a) {
        return QPointF(d.x() * qCos(a) - d.y() * qSin(a),
                       d.x() * qSin(a) + d.y() * qCos(a)) + to;
    };
    QPolygonF head{to, rot(angle), rot(-angle)};
    p.setPen(Qt::NoPen);
    p.drawPolygon(head);
}

void AnnotationCanvas::drawAnnot(QPainter &p, const Annot &a) const
{
    p.save();
    QPen pen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (a.type) {
    case Pen: {
        QPainterPath path;
        if (!a.points.isEmpty()) {
            path.moveTo(a.points.first());
            for (int i = 1; i < a.points.size(); ++i)
                path.lineTo(a.points[i]);
        }
        p.drawPath(path);
        break;
    }
    case Line:
        p.drawLine(a.rect.topLeft(), a.rect.bottomRight());
        break;
    case Arrow: {
        const QPointF tail = a.rect.topLeft();
        const QPointF tip = a.rect.bottomRight();
        // Shaft: flat cap, stopped short of the tip so nothing pokes through
        // the arrowhead apex (the tip is the arrowhead, not a round line cap).
        QLineF shaft(tail, tip);
        const qreal headLen = arrowHeadLen(a.width);
        if (shaft.length() > headLen * 0.9) {
            QLineF s = shaft;
            s.setLength(shaft.length() - headLen * 0.82);
            QPen shaftPen(a.color, a.width, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
            p.setPen(shaftPen);
            p.drawLine(s);
        }
        p.setBrush(a.color);
        drawArrowHead(p, tail, tip, a.width);
        break;
    }
    case Rect:
        if (a.filled) p.setBrush(a.fillColor);
        p.drawRoundedRect(a.rect.normalized(), 2, 2);
        break;
    case Ellipse:
        if (a.filled) p.setBrush(a.fillColor);
        p.drawEllipse(a.rect.normalized());
        break;
    case SmartErase:
        p.setPen(Qt::NoPen);
        p.setBrush(a.color);
        p.drawRect(a.rect.normalized());
        break;
    case Text: {
        QFont f;
        f.setPixelSize(a.fontSize);
        f.setBold(true);
        p.setFont(f);
        // subtle contrast shadow so text is readable on any background
        p.setPen(QColor(0, 0, 0, 160));
        p.drawText(a.rect.topLeft() + QPointF(1.5, a.fontSize + 1.5), a.text);
        p.setPen(a.color);
        p.drawText(a.rect.topLeft() + QPointF(0, a.fontSize), a.text);
        break;
    }
    case Blur: {
        const QRect r = a.rect.normalized().toAlignedRect().intersected(m_base.rect());
        if (r.width() > 4 && r.height() > 4) {
            QImage region = m_base.copy(r);
            const int f = 10;
            QImage small = region.scaled(qMax(1, r.width() / f), qMax(1, r.height() / f),
                                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            small = small.scaled(qMax(1, r.width() / (f / 2)), qMax(1, r.height() / (f / 2)),
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            p.drawImage(r, small.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
        break;
    }
    case Pixelate: {
        const QRect r = a.rect.normalized().toAlignedRect().intersected(m_base.rect());
        if (r.width() > 4 && r.height() > 4) {
            const int px = qMax(8, int(a.width) * 3);
            QImage region = m_base.copy(r);
            QImage small = region.scaled(qMax(1, r.width() / px), qMax(1, r.height() / px),
                                         Qt::IgnoreAspectRatio, Qt::FastTransformation);
            p.drawImage(r, small.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
        }
        break;
    }
    case Highlight: {
        QColor c = a.color;
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        c.setAlpha(255);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(a.rect.normalized());
        break;
    }
    case Step: {
        const qreal r = qMax(14.0, a.fontSize * 0.9);
        const QPointF c = a.rect.topLeft();
        p.setPen(QPen(QColor(255, 255, 255, 230), 2));
        p.setBrush(a.color);
        p.drawEllipse(c, r, r);
        QFont f;
        f.setPixelSize(int(r));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter,
                   QString::number(a.number));
        break;
    }
    default:
        break;
    }
    p.restore();
}

void AnnotationCanvas::drawAll(QPainter &p) const
{
    for (const Annot &a : m_items)
        drawAnnot(p, a);
    if (m_drawing)
        drawAnnot(p, m_current);
}

void AnnotationCanvas::paint(QPainter *painter)
{
    if (m_base.isNull()) return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal s = renderScale();
    painter->save();
    painter->scale(s, s);
    painter->drawImage(0, 0, m_base);
    drawAll(*painter);

    if (m_tool == ObjectPick && !hasSelection()) {
        // Highlight the object under the cursor: dim the rest, outline it in accent.
        if (!m_hoverObject.isNull()) {
            QPainterPath full;
            full.addRect(QRectF(QPointF(0, 0), QSizeF(m_base.size())));
            QPainterPath hole;
            hole.addRect(QRectF(m_hoverObject));
            full = full.subtracted(hole);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(23, 21, 59, 150));
            painter->drawPath(full);
            QPen border(QColor(200, 172, 214), 2.0 / s); // accent
            painter->setPen(border);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRectF(m_hoverObject));
        }
    } else if (m_selectionMode) {
        // Dim everything outside the selection.
        QPainterPath full;
        full.addRect(QRectF(QPointF(0, 0), QSizeF(m_base.size())));
        if (hasSelection()) {
            QPainterPath sel;
            sel.addRect(m_selection);
            full = full.subtracted(sel);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(23, 21, 59, 150)); // primary tint dim
        painter->drawPath(full);

        if (hasSelection()) {
            // Object-pick preview: dim the pixels the mask will remove so the
            // user sees the exact cutout before confirming.
            if (m_tool == ObjectPick && !m_maskOverlay.isNull()
                && m_objectMaskRect == m_selection.toAlignedRect().intersected(m_base.rect()))
                painter->drawImage(m_objectMaskRect.topLeft(), m_maskOverlay);
            QPen border(QColor(200, 172, 214), 1.5 / s); // accent
            border.setStyle(Qt::SolidLine);
            painter->setPen(border);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(m_selection);
            // Handles
            painter->setBrush(QColor(200, 172, 214));
            painter->setPen(QPen(QColor(23, 21, 59), 1.0 / s));
            const qreal hs = 5.0 / s;
            const QRectF r = m_selection;
            const QPointF pts[8] = {
                r.topLeft(), {r.center().x(), r.top()}, r.topRight(),
                {r.left(), r.center().y()}, {r.right(), r.center().y()},
                r.bottomLeft(), {r.center().x(), r.bottom()}, r.bottomRight()};
            for (const QPointF &pt : pts)
                painter->drawEllipse(pt, hs, hs);
        }
    }
    painter->restore();
}

QImage AnnotationCanvas::rendered() const
{
    QImage out = m_base.copy();
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    drawAll(p);
    p.end();
    return out;
}

QImage AnnotationCanvas::renderedSelection() const
{
    QImage full = rendered();
    if (!hasSelection()) return full;
    const QRect r = m_selection.toAlignedRect().intersected(full.rect());
    QImage crop = full.copy(r);
    // Object pick: cut the segmented foreground out — everything the mask
    // rejects becomes fully transparent (survives png/webp/clipboard).
    if (m_tool == ObjectPick && !m_objectMask.isNull() && m_objectMaskRect == r) {
        QPainter p(&crop);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, m_objectMask.convertToFormat(QImage::Format_Alpha8));
        p.end();
    }
    return crop;
}

// ---------------------------------------------------------------- input

int AnnotationCanvas::hitHandle(const QPointF &imgPos) const
{
    if (!hasSelection()) return -1;
    const qreal tol = 12.0 / qMax(0.05, renderScale());
    const QRectF r = m_selection;
    const QPointF pts[8] = {
        r.topLeft(), {r.center().x(), r.top()}, r.topRight(),
        {r.left(), r.center().y()}, {r.right(), r.center().y()},
        r.bottomLeft(), {r.center().x(), r.bottom()}, r.bottomRight()};
    for (int i = 0; i < 8; ++i)
        if (QLineF(imgPos, pts[i]).length() <= tol)
            return i;
    return -1;
}

void AnnotationCanvas::mousePressEvent(QMouseEvent *e)
{
    const QPointF img = toImage(e->position().x(), e->position().y());
    m_dragStart = img;

    if (m_tool == ObjectPick) {
        // Click on a detected candidate, or drag a rectangle around the object.
        // Either way the foreground inside the region is segmented off-thread
        // and previewed; the user confirms with Enter / double-click / the
        // Capture button once the cutout looks right (a click no longer
        // captures blindly — the whole point is seeing what gets removed).
        if (!m_hoverObject.isNull()) {
            // Pad the tight bounding box so the border ring the segmenter
            // treats as background actually samples background pixels.
            const int pad = qMax(6, qMax(m_hoverObject.width(), m_hoverObject.height()) / 20);
            m_selection = QRectF(m_hoverObject.adjusted(-pad, -pad, pad, pad)
                                     .intersected(m_base.rect()));
            normalizeSelection();
            emit selectionRectChanged();
            startSegmentation();
            update();
        } else {
            m_drag = NewSelection;
            clearObjectMask();
            m_selection = QRectF(img, img);
            emit selectionRectChanged();
            update();
        }
        e->accept();
        return;
    }
    if (m_tool == Text) {
        emit textRequested(img.x(), img.y());
        e->accept();
        return;
    }
    if (m_tool == Step) {
        pushUndo();
        Annot a;
        a.type = Step;
        a.rect = QRectF(img, img);
        a.color = m_color;
        a.fontSize = m_fontSize;
        a.number = ++m_stepCounter;
        m_items.append(a);
        update();
        e->accept();
        return;
    }

    const bool selectionTool = (m_selectionMode && m_tool == None) || m_tool == Crop;
    if (selectionTool) {
        const int h = hitHandle(img);
        if (h >= 0) {
            m_drag = ResizeSelection;
            m_resizeHandle = h;
            m_selStart = m_selection;
        } else if (hasSelection() && m_selection.contains(img)) {
            m_drag = MoveSelection;
            m_selStart = m_selection;
        } else {
            m_drag = NewSelection;
            m_selection = QRectF(img, img);
            emit selectionRectChanged();
        }
        update();
        e->accept();
        return;
    }

    if (m_tool != None) {
        m_drag = DrawDrag;
        m_drawing = true;
        m_current = {};
        m_current.type = Tool(m_tool);
        m_current.color = m_color;
        m_current.fillColor = m_fillColor;
        m_current.filled = m_fillEnabled && (m_tool == Rect || m_tool == Ellipse);
        m_current.width = m_strokeWidth;
        m_current.fontSize = m_fontSize;
        m_current.rect = QRectF(img, img);
        if (m_tool == Pen)
            m_current.points = {img};
        if (m_tool == Highlight && m_current.color == QColor(QStringLiteral("#FF4757")))
            m_current.color = QColor(255, 234, 112); // sensible default for highlighter
        update();
    }
    e->accept();
}

// Samples the perimeter of `r` in the current base image and returns the
// average color — the "background" a smart eraser fills the region with.
QColor AnnotationCanvas::sampleEdgeColor(const QRectF &rf) const
{
    const QRect r = rf.normalized().toAlignedRect().intersected(m_base.rect());
    if (r.width() < 2 || r.height() < 2 || m_base.isNull())
        return QColor(Qt::white);
    quint64 sr = 0, sg = 0, sb = 0, n = 0;
    auto acc = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= m_base.width() || y >= m_base.height()) return;
        const QColor c = m_base.pixelColor(x, y);
        sr += c.red(); sg += c.green(); sb += c.blue(); ++n;
    };
    const int step = qMax(1, r.width() / 64);
    for (int x = r.left(); x <= r.right(); x += step) {
        acc(x, r.top()); acc(x, r.bottom());
        acc(x, r.top() - 1); acc(x, r.bottom() + 1); // just outside, if available
    }
    const int stepY = qMax(1, r.height() / 64);
    for (int y = r.top(); y <= r.bottom(); y += stepY) {
        acc(r.left(), y); acc(r.right(), y);
        acc(r.left() - 1, y); acc(r.right() + 1, y);
    }
    if (!n)
        return QColor(Qt::white);
    return QColor(int(sr / n), int(sg / n), int(sb / n));
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF img = toImage(e->position().x(), e->position().y());

    switch (m_drag) {
    case DrawDrag:
        if (m_current.type == Pen)
            m_current.points.append(img);
        else
            m_current.rect.setBottomRight(img);
        update();
        break;
    case NewSelection:
        m_selection = QRectF(m_dragStart, img).normalized();
        normalizeSelection();
        emit selectionRectChanged();
        update();
        break;
    case MoveSelection: {
        QRectF r = m_selStart.translated(img - m_dragStart);
        r.moveLeft(qBound(0.0, r.left(), qreal(m_base.width()) - r.width()));
        r.moveTop(qBound(0.0, r.top(), qreal(m_base.height()) - r.height()));
        m_selection = r;
        emit selectionRectChanged();
        update();
        break;
    }
    case ResizeSelection: {
        QRectF r = m_selStart;
        const QPointF d = img - m_dragStart;
        switch (m_resizeHandle) {
        case 0: r.setTopLeft(r.topLeft() + d); break;
        case 1: r.setTop(r.top() + d.y()); break;
        case 2: r.setTopRight(r.topRight() + d); break;
        case 3: r.setLeft(r.left() + d.x()); break;
        case 4: r.setRight(r.right() + d.x()); break;
        case 5: r.setBottomLeft(r.bottomLeft() + d); break;
        case 6: r.setBottom(r.bottom() + d.y()); break;
        case 7: r.setBottomRight(r.bottomRight() + d); break;
        }
        m_selection = r.normalized();
        normalizeSelection();
        emit selectionRectChanged();
        update();
        break;
    }
    default:
        break;
    }
    e->accept();
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_tool == ObjectPick && m_drag == NewSelection && hasSelection())
        startSegmentation();
    if (m_drag == DrawDrag && m_drawing) {
        m_drawing = false;
        const bool tiny = m_current.type != Pen &&
                          QLineF(m_current.rect.topLeft(), m_current.rect.bottomRight()).length() < 3;
        if (!tiny) {
            // Smart eraser: fill the region with the sampled surrounding color.
            if (m_current.type == SmartErase)
                m_current.color = sampleEdgeColor(m_current.rect);
            pushUndo();
            m_items.append(m_current);
        }
        update();
    }
    m_drag = NoDrag;
    m_resizeHandle = -1;
    e->accept();
}

void AnnotationCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_selectionMode && hasSelection())
        emit selectionConfirmed();
    e->accept();
}

void AnnotationCanvas::hoverMoveEvent(QHoverEvent *e)
{
    const QPointF img = toImage(e->position().x(), e->position().y());
    if (m_tool == ObjectPick) {
        // Candidates are sorted smallest-first, so the first hit is the
        // innermost element under the cursor.
        QRect found;
        const QPoint p = img.toPoint();
        for (const QRect &r : std::as_const(m_objectCandidates)) {
            if (r.contains(p)) { found = r; break; }
        }
        if (found != m_hoverObject) {
            m_hoverObject = found;
            update();
        }
        setCursor(found.isNull() ? Qt::CrossCursor : Qt::PointingHandCursor);
        e->accept();
        return;
    }
    if ((m_selectionMode && m_tool == None) || m_tool == Crop) {
        const int h = hitHandle(img);
        if (h == 0 || h == 7) setCursor(Qt::SizeFDiagCursor);
        else if (h == 2 || h == 5) setCursor(Qt::SizeBDiagCursor);
        else if (h == 1 || h == 6) setCursor(Qt::SizeVerCursor);
        else if (h == 3 || h == 4) setCursor(Qt::SizeHorCursor);
        else if (hasSelection() && m_selection.contains(img)) setCursor(Qt::SizeAllCursor);
        else setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
    e->accept();
}

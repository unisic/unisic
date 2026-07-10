#include "AnnotationCanvas.h"
#include "overlay/ObjectDetector.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QCursor>
#include <QtMath>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <algorithm>
#include <cmath>

AnnotationCanvas::AnnotationCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setCursor(Qt::CrossCursor);
    // Debounce for nudge-driven re-segmentation: arrow-key autorepeat would
    // otherwise queue one full segmentation job per repeat.
    m_nudgeTimer = new QTimer(this);
    m_nudgeTimer->setSingleShot(true);
    m_nudgeTimer->setInterval(150);
    connect(m_nudgeTimer, &QTimer::timeout, this, &AnnotationCanvas::startSegmentation);
}

bool AnnotationCanvas::segmenting() const
{
    return m_segmentActive > 0 || (m_nudgeTimer && m_nudgeTimer->isActive());
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
    m_hoverChain.clear();
    m_pickOffset = 0;
    m_hoverObject = QRect();
    m_hoverObjectKind.clear();
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
    update();    if (m_smartPick && m_selectionMode)
        ensureObjectCandidates();
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
    const bool leftHighlight = (m_tool == Highlight);
    m_tool = t;
    emit toolChanged();
    // Until the user explicitly picks a color, the highlighter defaults to
    // yellow instead of the stock red. Flip the live color both ways so the
    // toolbar dot always shows exactly what a stroke will draw — and an
    // explicit pick (including re-clicking the red swatch) is honored as-is.
    if (!m_strokeColorTouched) {
        if (t == Highlight && m_color == QColor(QStringLiteral("#FF4757"))) {
            m_color = QColor(255, 234, 112); // sensible default for highlighter
            m_strokeAuto = true;             // not a user pick — never persisted
            emit strokeColorChanged();
        } else if (leftHighlight && m_color == QColor(255, 234, 112)) {
            m_color = QColor(QStringLiteral("#FF4757"));
            m_strokeAuto = true;
            emit strokeColorChanged();
        }
    }
    if (leftObjectPick)
        clearObjectMask();
    if (t == ObjectPick) {
        m_hoverObject = QRect();
        m_hoverObjectKind.clear();
        ensureObjectCandidates();
    }
    update();
}

void AnnotationCanvas::ensureObjectCandidates()
{
    // Detect candidate objects once, off the GUI thread (a few tens of ms).
    if (m_objectCandidates.isEmpty() && !m_base.isNull() && !m_detectWatcher) {
        m_detectWatcher = new QFutureWatcher<QVector<ObjectDetector::Candidate>>(this);
        connect(m_detectWatcher, &QFutureWatcher<QVector<ObjectDetector::Candidate>>::finished, this, [this] {
            m_objectCandidates = m_detectWatcher->result();
            m_detectWatcher->deleteLater();
            m_detectWatcher = nullptr;
            if (!m_lastHoverImg.isNull())
                updateHoverObject(m_lastHoverImg);
            update();
        });
        m_detectWatcher->setFuture(QtConcurrent::run(ObjectDetector::detectCandidates, m_base));
    }
}

void AnnotationCanvas::setSmartPick(bool on)
{
    if (m_smartPick == on)
        return;
    m_smartPick = on;
    // Start detecting right away: the candidates must be ready by the time
    // the user's first click lands (the pass runs off-thread).
    if (on && m_selectionMode)
        ensureObjectCandidates();
    emit smartPickChanged();
    update();
}

void AnnotationCanvas::setColorPicking(bool on)
{
    if (m_colorPicking == on)
        return;
    m_colorPicking = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    emit colorPickingChanged();
    update();
}

void AnnotationCanvas::setStrokeColor(const QColor &c)
{
    if (m_color == c) return;
    // Only a real change counts as a user pick — restoring the identical
    // default from settings at startup early-returns on the guard above.
    m_strokeColorTouched = true;
    m_strokeAuto = false;
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

// Grayscale8 -> Alpha8 the only reliable way: convertToFormat routes through
// RGB (gray becomes an opaque color) and yields an all-255 alpha image, which
// silently turned both the cutout and its preview into no-ops. Both formats
// are 1 byte/px, so a per-scanline copy of the gray values IS the alpha plane.
static QImage grayToAlpha(const QImage &gray)
{
    QImage a(gray.size(), QImage::Format_Alpha8);
    for (int y = 0; y < gray.height(); ++y)
        memcpy(a.scanLine(y), gray.constScanLine(y), size_t(gray.width()));
    return a;
}

void AnnotationCanvas::startSegmentation()
{
    if (m_base.isNull() || !hasSelection()) {
        // A pending nudge-debounce timer counted as segmenting() until this
        // fire — notify, or the pill and a deferred confirm wait forever.
        emit segmentingChanged();
        return;
    }
    const QRect r = m_selection.toAlignedRect().intersected(m_base.rect());
    m_objectMask = {};
    m_objectMaskRect = {};
    m_maskOverlay = {};
    m_segmentRect = r;
    const quint64 seq = ++m_segmentSeq; // invalidates any in-flight run
    if (r.width() < 12 || r.height() < 12) {
        m_segmentRect = {};
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
        // A degenerate result must not poison m_segmentRect: re-clicking the
        // same candidate should retry, not become a permanent no-op.
        if (seq == m_segmentSeq && mask.isNull())
            m_segmentRect = {};
        if (seq == m_segmentSeq && !mask.isNull()) {
            m_objectMask = mask;
            m_objectMaskRect = r;
            // Preview overlay: dark tint exactly where the mask removes pixels.
            QImage inv = mask;
            inv.invertPixels();
            m_maskOverlay = QImage(mask.size(), QImage::Format_ARGB32_Premultiplied);
            { QColor sc = m_uiScrim; sc.setAlpha(200); m_maskOverlay.fill(sc); }
            QPainter p(&m_maskOverlay);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.drawImage(0, 0, grayToAlpha(inv));
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
    const bool wasSegmenting = segmenting();
    const bool had = !m_objectMask.isNull();
    if (m_nudgeTimer)
        m_nudgeTimer->stop();
    m_objectMask = {};
    m_objectMaskRect = {};
    m_maskOverlay = {};
    m_segmentRect = {};
    if (had || wasSegmenting != segmenting())
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
    // The mask was computed for the old rectangle — re-segment, debounced so
    // key autorepeat runs ONE job after the burst instead of one per repeat.
    // The pending timer counts as segmenting(), so a confirm in the window
    // between keypress and timer fire waits instead of exporting stale state.
    if (m_tool == ObjectPick) {
        const bool wasIdle = !segmenting();
        m_nudgeTimer->start();
        if (wasIdle)
            emit segmentingChanged();
    }
    update();
}

void AnnotationCanvas::selectAll()
{
    m_selection = QRectF(QPointF(0, 0), QSizeF(m_base.size()));
    // Any object mask was computed for the old rect — drop it, or the status
    // pill keeps promising a cutout that renderedSelection() would skip. No
    // auto re-segment: select-all is a change of intent and a full-frame
    // segmentation is costly; clicking a candidate re-segments as before.
    if (m_tool == ObjectPick)
        clearObjectMask();
    emit selectionRectChanged();
    update();
}

// Escape-cancel for an in-progress selection: drops the rect (and any object
// mask computed for it) without touching annotations or the base image.
void AnnotationCanvas::clearSelection()
{
    m_selection = {};
    if (m_tool == ObjectPick)
        clearObjectMask();
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
    m_hoverChain.clear();
    m_pickOffset = 0;
    m_hoverObject = QRect();
    m_hoverObjectKind.clear();
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
            // Cached patch: identical inputs (base data, rect, width) produce a
            // byte-identical patch, so only recompute when one of them changed.
            if (a.fxBaseKey != m_base.cacheKey() || a.fxRect != a.rect || a.fxWidth != a.width) {
                QImage region = m_base.copy(r);
                const int f = 10;
                QImage small = region.scaled(qMax(1, r.width() / f), qMax(1, r.height() / f),
                                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                small = small.scaled(qMax(1, r.width() / (f / 2)), qMax(1, r.height() / (f / 2)),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                a.fxPatch = small.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                a.fxBaseKey = m_base.cacheKey();
                a.fxRect = a.rect;
                a.fxWidth = a.width;
            }
            p.drawImage(r, a.fxPatch);
        }
        break;
    }
    case Pixelate: {
        const QRect r = a.rect.normalized().toAlignedRect().intersected(m_base.rect());
        if (r.width() > 4 && r.height() > 4) {
            if (a.fxBaseKey != m_base.cacheKey() || a.fxRect != a.rect || a.fxWidth != a.width) {
                const int px = qMax(8, int(a.width) * 3);
                QImage region = m_base.copy(r);
                QImage small = region.scaled(qMax(1, r.width() / px), qMax(1, r.height() / px),
                                             Qt::IgnoreAspectRatio, Qt::FastTransformation);
                a.fxPatch = small.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                a.fxBaseKey = m_base.cacheKey();
                a.fxRect = a.rect;
                a.fxWidth = a.width;
            }
            p.drawImage(r, a.fxPatch);
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

QRectF AnnotationCanvas::annotBoundsImg(const Annot &a) const
{
    QRectF r;
    if (!a.points.isEmpty()) {
        r = QRectF(a.points.first(), QSizeF(0, 0));
        for (const QPointF &pt : a.points)
            r |= QRectF(pt, QSizeF(0, 0));
    } else {
        r = a.rect.normalized();
    }
    // Stroke width + the arrow head (largest overdraw any tool does) + slack
    // for antialiasing; over-estimating only repaints a few extra pixels.
    const qreal pad = a.width / 2.0 + arrowHeadLen(a.width) + 4.0;
    return r.adjusted(-pad, -pad, pad, pad);
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

    if ((m_tool == ObjectPick
         || (m_selectionMode && m_tool == None && m_smartPick)) && !hasSelection()) {
        // Highlight the object under the cursor: dim the rest, outline it in accent.
        if (!m_hoverObject.isNull()) {
            QPainterPath full;
            full.addRect(QRectF(QPointF(0, 0), QSizeF(m_base.size())));
            QPainterPath hole;
            hole.addRect(QRectF(m_hoverObject));
            full = full.subtracted(hole);
            QColor scrim = m_uiScrim; scrim.setAlpha(150);
            painter->setPen(Qt::NoPen);
            painter->setBrush(scrim);
            painter->drawPath(full);
            painter->setBrush(Qt::NoBrush);
            // White halo under the accent line: keeps the highlight readable
            // over dark and light content alike.
            painter->setPen(QPen(QColor(255, 255, 255, 160), 4.0 / s));
            painter->drawRect(QRectF(m_hoverObject));
            painter->setPen(QPen(m_uiAccent, 2.0 / s)); // themed accent
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
        QColor scrim = m_uiScrim; scrim.setAlpha(150);
        painter->setPen(Qt::NoPen);
        painter->setBrush(scrim); // themed dim tint
        painter->drawPath(full);

        if (hasSelection()) {
            // Object-pick preview: dim the pixels the mask will remove so the
            // user sees the exact cutout before confirming.
            if (m_tool == ObjectPick && !m_maskOverlay.isNull()
                && m_objectMaskRect == m_selection.toAlignedRect().intersected(m_base.rect()))
                painter->drawImage(m_objectMaskRect.topLeft(), m_maskOverlay);
            QPen border(m_uiAccent, 1.5 / s); // themed accent
            border.setStyle(Qt::SolidLine);
            painter->setPen(border);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(m_selection);
            // Handles
            painter->setBrush(m_uiAccent);
            painter->setPen(QPen(m_uiScrim, 1.0 / s));
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
    if (!hasSelection()) return rendered();
    // Render straight into the crop instead of compositing the whole frame and
    // throwing it away: rendered() detached a full ~33 MB copy of a 4K base and
    // rasterized every annotation across it just to keep a small region.
    const QRect r = m_selection.toAlignedRect().intersected(m_base.rect());
    if (r.isEmpty()) return QImage();
    QImage crop = m_base.copy(r);
    QPainter p(&crop);
    p.setRenderHint(QPainter::Antialiasing);
    // Same image-space coordinates as a full-frame render, clipped to the crop.
    p.translate(-r.topLeft());
    drawAll(p);
    p.end();
    // Object pick: cut the segmented foreground out — everything the mask
    // rejects becomes fully transparent (survives png/webp/clipboard).
    if (m_tool == ObjectPick && !m_objectMask.isNull() && m_objectMaskRect == r) {
        QPainter pm(&crop);
        pm.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        pm.drawImage(0, 0, grayToAlpha(m_objectMask));
        pm.end();
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
    m_hoverPoint = e->position();
    emit hoverPointChanged();
    const QPointF img = toImage(e->position().x(), e->position().y());
    m_dragStart = img;

    if (m_colorPicking) {
        // Sample the pixel under the cursor from the frozen screenshot.
        const QPoint p = img.toPoint();
        if (m_base.rect().contains(p))
            emit colorPicked(m_base.pixelColor(p));
        setColorPicking(false);
        e->accept();
        return;
    }

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
            const QRect padded = m_hoverObject.adjusted(-pad, -pad, pad, pad)
                                     .intersected(m_base.rect());
            // Same region as the current (or in-flight) run: leave it alone.
            // This press may be the first half of double-click-confirm, and
            // restarting would wipe the mask right before the confirm fires.
            if (padded != m_segmentRect) {
                m_selection = QRectF(padded);
                normalizeSelection();
                emit selectionRectChanged();
                startSegmentation();
                update();
            }
        } else {
            // Defer everything: a bare press must not destroy the selection or
            // the mask (double-click-confirm arrives as press,press,dblclick).
            // mouseMoveEvent promotes this to a real NewSelection once the
            // pointer actually moves.
            m_drag = PendingNewSelection;
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
        // Ctrl + press anywhere inside the region = grab-and-move, overriding
        // handle-resize and new-selection. A reliable "reposition the whole
        // capture area" that never accidentally resizes an edge or starts a
        // fresh rectangle.
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        if (ctrl && hasSelection() && m_selection.contains(img)) {
            m_drag = MoveSelection;
            m_selStart = m_selection;
            setCursor(Qt::ClosedHandCursor);
            update();
            e->accept();
            return;
        }
        const int h = hitHandle(img);
        if (h >= 0) {
            m_drag = ResizeSelection;
            m_resizeHandle = h;
            m_selStart = m_selection;
        } else if (hasSelection() && m_selection.contains(img)) {
            m_drag = MoveSelection;
            m_selStart = m_selection;
        } else if (m_smartPick && m_tool == None) {
            // Smart pick: defer — a genuine drag promotes to NewSelection
            // (mouseMoveEvent), a plain click selects the detected object
            // under the cursor on release.
            m_drag = PendingNewSelection;
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
        m_lastDragBoundsImg = QRectF();
        m_current.type = Tool(m_tool);
        m_current.color = m_color;
        m_current.fillColor = m_fillColor;
        m_current.filled = m_fillEnabled && (m_tool == Rect || m_tool == Ellipse);
        m_current.width = m_strokeWidth;
        m_current.fontSize = m_fontSize;
        m_current.rect = QRectF(img, img);
        if (m_tool == Pen)
            m_current.points = {img};
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
    m_hoverPoint = e->position();
    emit hoverPointChanged();
    const QPointF img = toImage(e->position().x(), e->position().y());

    switch (m_drag) {
    case DrawDrag: {
        if (m_current.type == Pen) {
            // Retain enough detail for a smooth path while coalescing
            // sub-pixel duplicate samples from high-frequency mice/tablets.
            const qreal minDistance = qMax(0.75, m_current.width / 3.0);
            if (m_current.points.isEmpty()
                || QLineF(m_current.points.constLast(), img).length() >= minDistance)
                m_current.points.append(img);
        } else
            m_current.rect.setBottomRight(img);
        // Repaint only the union of the annotation's old and new bounds — a
        // bare update() re-rasterized the whole 4K base every mouse-move.
        const QRectF nowB = annotBoundsImg(m_current);
        const QRectF dirtyImg = nowB.united(m_lastDragBoundsImg);
        m_lastDragBoundsImg = nowB;
        const qreal s = renderScale();
        update(QRectF(dirtyImg.x() * s, dirtyImg.y() * s,
                      dirtyImg.width() * s, dirtyImg.height() * s)
                   .toAlignedRect().adjusted(-2, -2, 2, 2));
        break;
    }
    case PendingNewSelection: {
        // Promote to a real selection only after a genuine drag (threshold in
        // item pixels, converted to image space so it feels the same at any
        // zoom); until then the previous selection + mask stay intact.
        const qreal threshold = 5.0 / qMax(0.05, renderScale());
        if (QLineF(m_dragStart, img).length() >= threshold) {
            m_drag = NewSelection;
            clearObjectMask();
            m_selection = QRectF(m_dragStart, img).normalized();
            normalizeSelection();
            emit selectionRectChanged();
            update();
        }
        break;
    }
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
    // Smart pick: the press never moved (still pending), so this is a CLICK —
    // select the evidence-ranked detected object under it. No candidate = no-op,
    // exactly like today's empty click. (An ObjectPick pending click keeps
    // its existing no-op semantics.)
    if (m_drag == PendingNewSelection && m_selectionMode && m_tool == None && m_smartPick) {
        // The highlighted rect IS the promise — select exactly it (including
        // the scroll-chosen nesting level), not a recomputed first hit.
        updateHoverObject(m_dragStart.toPoint());
        if (!m_hoverObject.isNull()) {
            m_selection = QRectF(m_hoverObject);
            normalizeSelection();
            m_hoverObject = QRect();
            m_hoverObjectKind.clear();
            m_hoverChain.clear();
            emit hoverObjectChanged();
            emit selectionRectChanged();
            update();
        }
    }
    if (m_drag == DrawDrag && m_drawing) {
        // Preserve the stroke endpoint even when it falls within the sampling
        // threshold used while the pointer is moving.
        if (m_current.type == Pen && !m_current.points.isEmpty()) {
            const QPointF releasePoint = toImage(e->position().x(), e->position().y());
            if (QLineF(m_current.points.constLast(), releasePoint).length() > 0.0)
                m_current.points.append(releasePoint);
        }
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

void AnnotationCanvas::updateHoverObject(const QPoint &imgPos)
{
    m_lastHoverImg = imgPos;
    QVector<ObjectDetector::Candidate> chain;
    for (const ObjectDetector::Candidate &candidate : std::as_const(m_objectCandidates))
        if (candidate.rect.contains(imgPos))
            chain.append(candidate); // candidates are area-sorted -> chain is inner→outer

    const auto sameChain = [](const QVector<ObjectDetector::Candidate> &a,
                              const QVector<ObjectDetector::Candidate> &b) {
        if (a.size() != b.size())
            return false;
        for (qsizetype i = 0; i < a.size(); ++i)
            if (a[i].rect != b[i].rect || a[i].kind != b[i].kind)
                return false;
        return true;
    };
    const bool chainChanged = !sameChain(chain, m_hoverChain);
    if (chain.isEmpty()) {
        m_pickOffset = 0;
        m_hoverDefaultIndex = 0;
    } else if (chainChanged) {
        // An element's visual evidence matters as much as its size. The old
        // "smallest bounding box always wins" rule regularly picked a bright
        // speck in a photo/video over the actual panel beneath the cursor.
        // Prefer a high-confidence, specific candidate, while preserving all
        // other candidates as scrollable levels.
        const double imageArea = qMax<qint64>(1, qint64(m_base.width()) * m_base.height());
        double best = -1.0;
        m_hoverDefaultIndex = 0;
        for (qsizetype i = 0; i < chain.size(); ++i) {
            const ObjectDetector::Candidate &candidate = chain[i];
            const double area = qint64(candidate.rect.width()) * candidate.rect.height();
            const double specificity = 1.0 - std::sqrt(std::min(1.0, area / imageArea));
            double kindBonus = 0.0;
            switch (candidate.kind) {
            case ObjectDetector::CandidateKind::Element: kindBonus = 0.025; break;
            case ObjectDetector::CandidateKind::Group: kindBonus = 0.01; break;
            case ObjectDetector::CandidateKind::Container: break;
            case ObjectDetector::CandidateKind::Window: kindBonus = 0.04; break;
            case ObjectDetector::CandidateKind::Screen: kindBonus = -0.20; break;
            }
            // Confidence intentionally dominates specificity. A small region
            // in a video is often more specific, but its colour/edge evidence
            // is weaker than the clear frame of the app or content panel.
            const double score = candidate.confidence * 0.90 + specificity * 0.10 + kindBonus;
            if (score > best) {
                best = score;
                m_hoverDefaultIndex = int(i);
            }
        }
        m_pickOffset = 0;
    }
    const int idx = chain.isEmpty()
        ? 0
        : qBound(0, m_hoverDefaultIndex + m_pickOffset, int(chain.size()) - 1);
    const QRect found = chain.isEmpty() ? QRect() : chain.at(idx).rect;
    const QString foundKind = [&] {
        if (chain.isEmpty())
            return QString();
        switch (chain.at(idx).kind) {
        case ObjectDetector::CandidateKind::Element: return tr("Element");
        case ObjectDetector::CandidateKind::Group: return tr("Group");
        case ObjectDetector::CandidateKind::Container: return tr("Panel");
        case ObjectDetector::CandidateKind::Window: return tr("Window");
        case ObjectDetector::CandidateKind::Screen: return tr("Screen");
        }
        return QString();
    }();
    const bool changed = (found != m_hoverObject) || (chain.size() != m_hoverChain.size())
                         || (idx != m_hoverIndex) || (foundKind != m_hoverObjectKind);
    m_hoverChain = chain;
    m_hoverIndex = idx;
    if (changed) {
        m_hoverObject = found;
        m_hoverObjectKind = foundKind;
        emit hoverObjectChanged();
        update();
    }
}

void AnnotationCanvas::wheelEvent(QWheelEvent *e)
{
    // Scroll cycles the nesting level while hovering in a pick mode: down =
    // inner detected elements, up = enclosing windows/containers, topmost =
    // whole screen.
    const bool pickHover = !hasSelection()
        && (m_tool == ObjectPick || (m_selectionMode && m_tool == None && m_smartPick));
    if (!pickHover || m_hoverChain.size() < 2) {
        e->ignore();
        return;
    }
    const int dir = e->angleDelta().y() > 0 ? 1 : -1;
    const int before = m_hoverIndex;
    m_pickOffset += dir;
    updateHoverObject(m_lastHoverImg);
    if (m_hoverIndex == before)
        m_pickOffset -= dir; // chain end — don't bank invisible steps
    e->accept();
}

void AnnotationCanvas::hoverMoveEvent(QHoverEvent *e)
{
    m_hoverPoint = e->position();
    emit hoverPointChanged();
    const QPointF img = toImage(e->position().x(), e->position().y());
    if (m_colorPicking) {
        setCursor(Qt::CrossCursor);
        e->accept();
        return;
    }
    if (m_tool == ObjectPick) {
        updateHoverObject(img.toPoint());
        setCursor(m_hoverObject.isNull() ? Qt::CrossCursor : Qt::PointingHandCursor);
        e->accept();
        return;
    }
    if (m_selectionMode && m_tool == None && m_smartPick && !hasSelection()) {
        // Smart pick: highlight the detected object under the cursor at the
        // current nesting depth (same candidate list as the ObjectPick tool).
        updateHoverObject(img.toPoint());
        setCursor(m_hoverObject.isNull() ? Qt::CrossCursor : Qt::PointingHandCursor);
        e->accept();
        return;
    }
    if ((m_selectionMode && m_tool == None) || m_tool == Crop) {
        // Ctrl inside the region = grab-to-move (see mousePressEvent): show the
        // open-hand cursor there so the affordance is discoverable.
        if ((e->modifiers() & Qt::ControlModifier)
            && hasSelection() && m_selection.contains(img)) {
            setCursor(Qt::OpenHandCursor);
            e->accept();
            return;
        }
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

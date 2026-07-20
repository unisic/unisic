#include "AnnotationCanvas.h"
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QLinearGradient>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
#include <QCursor>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <algorithm>
#include <cmath>

// A callout is intentionally just a shape: the established Text tool remains
// responsible for multilingual/font-aware content and can be placed inside the
// bubble. This keeps a placed annotation compact (one rect, no text editor
// state) while still making directions and notes stand out in a capture.
static qreal calloutTailHeight(const QRectF &rect)
{
    return qBound(10.0, rect.normalized().height() * 0.25, 42.0);
}

static QPainterPath calloutPath(const QRectF &rect)
{
    const QRectF r = rect.normalized();
    QPainterPath path;
    if (r.width() < 2 || r.height() < 2)
        return path;

    const qreal radius = qMin(12.0, qMin(r.width(), r.height()) / 4.0);
    path.addRoundedRect(r, radius, radius);
    if (r.width() < 12 || r.height() < 12)
        return path;

    const qreal tailWidth = qBound(12.0, r.width() * 0.24, 42.0);
    const qreal baseLeft = r.left() + qMax(radius, tailWidth * 0.35);
    const qreal baseRight = qMin(r.right() - radius, baseLeft + tailWidth);
    QPainterPath tail;
    tail.moveTo(baseLeft, r.bottom() - 1.0);
    tail.lineTo(baseLeft - tailWidth * 0.42, r.bottom() + calloutTailHeight(r));
    tail.lineTo(baseRight, r.bottom() - 1.0);
    tail.closeSubpath();
    return path.united(tail);
}

AnnotationCanvas::AnnotationCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
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
    if (m_selectedAnnot >= 0) { m_selectedAnnot = -1; emit selectedAnnotChanged(); }
    // A new base invalidates any in-flight OCR: its word boxes are in the old
    // image's pixel space and would misalign.
    ++m_ocrSeq;
    if (m_ocrMode || !m_ocrWords.isEmpty()) {
        m_ocrMode = false; m_ocrBusy = false; m_ocrDragging = false;
        m_ocrWords.clear(); m_ocrLines.clear();
        m_ocrCaretA = m_ocrCaretB = m_ocrPressCaret = -1;
        m_ocrHoverLine = -1; m_ocrDidDrag = false;
        emit ocrChanged();
    }
    // Text-aware Highlight glyph boxes are in the old base's pixel space; drop
    // them and allow one fresh request when the Highlight tool is next used.
    m_glyphBoxes.clear();
    m_glyphBoxesReady = false;
    m_glyphBoxesRequested = false;
    m_glyphBoxesBaseKey = -1;
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
    const bool leftHighlight = (m_tool == Highlight);
    m_tool = t;
    emit toolChanged();
    // Text-aware Highlight: request glyph boxes once so the next drag over text
    // can snap to the line(s). The editor answers via setGlyphBoxes(); the
    // overlay has no OCR wiring and simply leaves the highlighter plain.
    if (t == Highlight && !m_glyphBoxesReady && !m_glyphBoxesRequested) {
        m_glyphBoxesRequested = true;
        emit glyphBoxesRequested();
    }
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
    // Leaving EditShapes drops the placed-shape selection (its handles/outline
    // must not linger under a drawing tool).
    if (t != EditShapes && m_selectedAnnot >= 0)
        clearAnnotSelection();
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

// Property ids for undo coalescing (see pushUndoCoalesced): a run of edits to
// the SAME id on the SAME shape within the window collapses to one undo entry.
namespace {
enum PropId { PStroke, PFillColor, PFillEnabled, PWidth, PFontSize, PFontFamily,
              PBold, PItalic, PUnderline, POutline, POutlineColor, PTextBg,
              PTextBgColor, PGeometry, PStepSize, PArrowHead };
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
    routeToSelected(PStroke);
}

void AnnotationCanvas::setShapeFillColor(const QColor &c)
{
    if (m_fillColor == c) return;
    m_fillColor = c;
    emit shapeFillColorChanged();
    routeToSelected(PFillColor);
}

void AnnotationCanvas::setShapeFillEnabled(bool on)
{
    if (m_fillEnabled == on) return;
    m_fillEnabled = on;
    emit shapeFillEnabledChanged();
    routeToSelected(PFillEnabled);
}

void AnnotationCanvas::setStrokeWidth(int w)
{
    if (m_strokeWidth == w) return;
    m_strokeWidth = w;
    emit strokeWidthChanged();
    routeToSelected(PWidth);
}

void AnnotationCanvas::setArrowHeadStyle(int style)
{
    style = qBound(0, style, 2);
    if (m_arrowHeadStyle == style) return;
    m_arrowHeadStyle = style;
    emit arrowHeadStyleChanged();
    routeToSelected(PArrowHead);
}

void AnnotationCanvas::setHighlightMode(int mode)
{
    mode = qBound(int(HlFreehand), mode, int(HlText));
    if (m_highlightMode == mode) return;
    m_highlightMode = mode;
    emit highlightModeChanged();
    // Text-snap needs the glyph boxes; request them once if the switch happened
    // while already on the Highlight tool (setTool only asks on tool entry).
    if (m_tool == Highlight && mode == HlText
        && !m_glyphBoxesReady && !m_glyphBoxesRequested) {
        m_glyphBoxesRequested = true;
        emit glyphBoxesRequested();
    }
    update();
}

void AnnotationCanvas::setMeasureMode(int mode)
{
    mode = qBound(0, mode, 1);
    if (m_measureMode == mode) return;
    m_measureMode = mode;
    emit measureModeChanged();
    update();
}

QString AnnotationCanvas::measuresText(const QString &format) const
{
    QStringList lines;
    for (const Annot &a : m_items) {
        if (a.type != Measure)
            continue;
        // `filled` marks a size-box measure; distance otherwise (see commit in
        // mouseReleaseEvent). rect stores the two endpoints as topLeft/bottomRight.
        if (a.filled) {
            const QRectF r = a.rect.normalized();
            const int w = qRound(r.width());
            const int h = qRound(r.height());
            if (format == QLatin1String("plain"))
                lines << QStringLiteral("%1x%2").arg(w).arg(h);
            else if (format == QLatin1String("css"))
                lines << QStringLiteral("width: %1px; height: %2px").arg(w).arg(h);
            else
                lines << QStringLiteral("%1 × %2").arg(w).arg(h);
        } else {
            const int len = qRound(QLineF(a.rect.topLeft(), a.rect.bottomRight()).length());
            if (format == QLatin1String("plain"))
                lines << QString::number(len);
            else if (format == QLatin1String("css"))
                lines << QStringLiteral("%1px").arg(len);
            else
                lines << QStringLiteral("%1 px").arg(len);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void AnnotationCanvas::setFontSize(int s)
{
    if (m_fontSize == s) return;
    m_fontSize = s;
    emit fontSizeChanged();
    routeToSelected(PFontSize);
}

void AnnotationCanvas::setStepSize(int s)
{
    if (m_stepSize == s) return;
    m_stepSize = s;
    emit stepSizeChanged();
    routeToSelected(PStepSize);
}

void AnnotationCanvas::setFontFamily(const QString &f)
{
    if (m_fontFamily == f) return;
    m_fontFamily = f;
    emit fontFamilyChanged();
    routeToSelected(PFontFamily);
}

void AnnotationCanvas::setFontBold(bool on)
{
    if (m_fontBold == on) return;
    m_fontBold = on;
    emit fontBoldChanged();
    routeToSelected(PBold);
}

void AnnotationCanvas::setFontItalic(bool on)
{
    if (m_fontItalic == on) return;
    m_fontItalic = on;
    emit fontItalicChanged();
    routeToSelected(PItalic);
}

void AnnotationCanvas::setFontUnderline(bool on)
{
    if (m_fontUnderline == on) return;
    m_fontUnderline = on;
    emit fontUnderlineChanged();
    routeToSelected(PUnderline);
}

void AnnotationCanvas::setTextOutline(bool on)
{
    if (m_textOutline == on) return;
    m_textOutline = on;
    emit textOutlineChanged();
    routeToSelected(POutline);
}

void AnnotationCanvas::setTextOutlineColor(const QColor &c)
{
    if (m_textOutlineColor == c) return;
    m_textOutlineColor = c;
    emit textOutlineColorChanged();
    routeToSelected(POutlineColor);
}

void AnnotationCanvas::setTextBackground(bool on)
{
    if (m_textBackground == on) return;
    m_textBackground = on;
    emit textBackgroundChanged();
    routeToSelected(PTextBg);
}

void AnnotationCanvas::setTextBackgroundColor(const QColor &c)
{
    if (m_textBgColor == c) return;
    m_textBgColor = c;
    emit textBackgroundColorChanged();
    routeToSelected(PTextBgColor);
}

void AnnotationCanvas::setSelectionMode(bool on)
{
    if (m_selectionMode == on) return;
    m_selectionMode = on;
    emit selectionModeChanged();
    update();
}

QPointF AnnotationCanvas::toImage(qreal itemX, qreal itemY) const
{
    const qreal s = renderScale();
    return s > 0 ? QPointF(itemX / s, itemY / s) : QPointF(itemX, itemY);
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
    // Also bound the stack by memory. Annotation-only snapshots share the base
    // (implicit sharing), but each crop / background-removal leaves a DISTINCT
    // full ARGB32 base (~33 MB at 4K) pinned by its snapshot — a handful can
    // reach hundreds of MB under the 100-entry cap alone. Trim the oldest
    // entries once the cumulative bytes of distinct base images exceed a budget
    // (keep a floor so undo stays useful even with very large bases). Snapshots
    // that share a base are adjacent (a new base = a new cacheKey), so counting
    // one per contiguous cacheKey run is exact for the real cases and only ever
    // over-counts (trims slightly sooner) in the impossible ones.
    constexpr qint64 kUndoBaseBudget = 256LL * 1024 * 1024;
    auto distinctBaseBytes = [this] {
        qint64 total = 0;
        qint64 prevKey = 0;
        for (int i = 0; i < m_undo.size(); ++i) {
            const qint64 key = m_undo[i].base.cacheKey();
            if (i == 0 || key != prevKey)
                total += qint64(m_undo[i].base.sizeInBytes());
            prevKey = key;
        }
        return total;
    };
    while (m_undo.size() > 8 && distinctBaseBytes() > kUndoBaseBudget)
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
    // The item list changed wholesale — a stale selection index would point at
    // the wrong shape (or past the end). Drop it (restoring the user's own
    // pre-selection style, which the seeded selection had overwritten).
    if (m_selectedAnnot >= 0) { m_selectedAnnot = -1; restoreStyleBackup(); emit selectedAnnotChanged(); }
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
    if (m_selectedAnnot >= 0) { m_selectedAnnot = -1; restoreStyleBackup(); emit selectedAnnotChanged(); }
    if (baseChanged) { emit imageChanged(); emit renderScaleChanged(); }
    emit historyChanged();
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
    a.fontFamily = m_fontFamily;
    a.bold = m_fontBold;
    a.italic = m_fontItalic;
    a.underline = m_fontUnderline;
    a.outlined = m_textOutline;
    a.outlineColor = m_textOutlineColor;
    a.textBg = m_textBackground;
    a.textBgColor = m_textBgColor;
    m_items.append(a);
    update();
}

bool AnnotationCanvas::pasteClipboard(qreal imgX, qreal imgY)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime)
        return false;
    const QPointF origin(imgX, imgY);
    if (mime->hasImage()) {
        QImage image = qvariant_cast<QImage>(mime->imageData());
        if (image.isNull())
            return false;
        image.setDevicePixelRatio(1.0);
        QSizeF size = image.size();
        // A pasted full-screen image should stay movable/editable instead of
        // covering the whole canvas by default. Downscale ONCE here rather
        // than asking QPainter to resample a 4K source on every canvas repaint;
        // the visible/exported rect cannot use those discarded pixels anyway.
        const QSizeF maxSize(m_base.width() * 0.6, m_base.height() * 0.6);
        if (size.width() > maxSize.width() || size.height() > maxSize.height()) {
            size.scale(maxSize, Qt::KeepAspectRatio);
            image = image.scaled(size.toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            size = image.size();
        }
        pushUndo();
        Annot a;
        a.type = PastedImage;
        a.rect = QRectF(origin - QPointF(size.width() / 2.0, size.height() / 2.0), size);
        a.image = image;
        m_items.append(a);
        updateImgRect(annotBoundsImg(a));
        return true;
    }
    if (!mime->hasText() || mime->text().trimmed().isEmpty())
        return false;
    commitText(imgX, imgY, mime->text());
    return true;
}

// ------------------------------------------------------ EditShapes: selection

void AnnotationCanvas::pushUndoCoalesced(int propId)
{
    // Collapse a burst of same-property edits on the same shape (a slider drag,
    // a held color pick) into one undo entry: only the FIRST edit of the run
    // snapshots. A 900 ms idle gap or a switch of shape/property starts a new
    // run. Move/resize gestures pass a fixed id and self-manage via mouseMove.
    const bool sameRun = (propId == m_lastCoalesceProp
                          && m_selectedAnnot == m_lastCoalesceIndex
                          && m_coalesceTimer.isValid()
                          && m_coalesceTimer.elapsed() < 900);
    if (!sameRun)
        pushUndo();
    m_lastCoalesceProp = propId;
    m_lastCoalesceIndex = m_selectedAnnot;
    m_coalesceTimer.restart();
}

void AnnotationCanvas::applyStyleToSelected()
{
    if (m_selectedAnnot < 0 || m_selectedAnnot >= m_items.size())
        return;
    Annot &a = m_items[m_selectedAnnot];
    a.color = m_color;
    a.fillColor = m_fillColor;
    a.filled = m_fillEnabled && (a.type == Rect || a.type == Ellipse || a.type == Callout);
    a.width = m_strokeWidth;
    a.arrowHeadStyle = m_arrowHeadStyle;
    a.fontSize = m_fontSize;
    a.stepSize = m_stepSize;
    a.fontFamily = m_fontFamily;
    a.bold = m_fontBold;
    a.italic = m_fontItalic;
    a.underline = m_fontUnderline;
    a.outlined = m_textOutline;
    a.outlineColor = m_textOutlineColor;
    a.textBg = m_textBackground;
    a.textBgColor = m_textBgColor;
}

void AnnotationCanvas::routeToSelected(int propId)
{
    if (m_selectedAnnot < 0 || m_suppressStyleToSelected)
        return;
    pushUndoCoalesced(propId);
    applyStyleToSelected();
    update();
}

void AnnotationCanvas::selectAnnot(int index)
{
    if (index == m_selectedAnnot) {
        if (index >= 0) update();
        return;
    }
    const bool hadSelection = m_selectedAnnot >= 0;
    m_selectedAnnot = index;
    // Reset the coalesce run so the next style edit starts fresh.
    m_lastCoalesceProp = -1;
    m_lastCoalesceIndex = -1;
    if (index >= 0 && index < m_items.size()) {
        // Entering a selection from none: remember the user's own style so
        // deselecting restores it (the seeding below overwrites it with the
        // clicked shape's style, which must not leak into the next drawn
        // shape).
        if (!hadSelection) {
            Annot &b = m_styleBackup;
            b.color = m_color; b.fillColor = m_fillColor; b.filled = m_fillEnabled;
            b.width = m_strokeWidth; b.fontSize = m_fontSize; b.stepSize = m_stepSize;
            b.arrowHeadStyle = m_arrowHeadStyle;
            b.fontFamily = m_fontFamily;
            b.bold = m_fontBold; b.italic = m_fontItalic; b.underline = m_fontUnderline;
            b.outlined = m_textOutline; b.outlineColor = m_textOutlineColor;
            b.textBg = m_textBackground; b.textBgColor = m_textBgColor;
            m_styleBackupValid = true;
        }
        // Seed the "current" props from the shape so the props bar shows its
        // values — suppress the routing so this seeding is not itself an edit.
        const Annot &a = m_items[index];
        m_suppressStyleToSelected = true;
        // Seed the stroke colour by setting m_color directly (not setStrokeColor):
        // the public setter would flip m_strokeColorTouched/m_strokeAuto, which
        // then persists the highlighter's auto-yellow as the user default and
        // permanently disables the red↔yellow auto-swap. Seeding must not touch
        // that state.
        if (m_color != a.color) { m_color = a.color; emit strokeColorChanged(); }
        setShapeFillColor(a.fillColor);
        setShapeFillEnabled(a.filled);
        setStrokeWidth(int(a.width));
        setArrowHeadStyle(a.arrowHeadStyle);
        setFontSize(a.fontSize);
        setStepSize(a.stepSize);
        setFontFamily(a.fontFamily);
        setFontBold(a.bold);
        setFontItalic(a.italic);
        setFontUnderline(a.underline);
        setTextOutline(a.outlined);
        setTextOutlineColor(a.outlineColor);
        setTextBackground(a.textBg);
        setTextBackgroundColor(a.textBgColor);
        m_suppressStyleToSelected = false;
    } else if (index < 0) {
        restoreStyleBackup();
    }
    emit selectedAnnotChanged();
    update();
}

// Hand the user their own pre-selection style back after a shape selection
// ends. Called from EVERY deselect path (click-empty, clear, delete, tool
// switch, undo/redo) so a clicked shape's style never leaks into the drawing
// defaults. Stroke colour is restored by writing m_color directly, for the
// same reason seeding does — never route the auto/touched flags through the
// public setter. Callers must have already set m_selectedAnnot to -1, so the
// fill/font setters below cannot route back into a shape.
void AnnotationCanvas::restoreStyleBackup()
{
    if (!m_styleBackupValid)
        return;
    m_styleBackupValid = false;
    const Annot b = m_styleBackup;
    if (m_color != b.color) { m_color = b.color; emit strokeColorChanged(); }
    setShapeFillColor(b.fillColor);
    setShapeFillEnabled(b.filled);
    setStrokeWidth(int(b.width));
    setArrowHeadStyle(b.arrowHeadStyle);
    setFontSize(b.fontSize);
    setStepSize(b.stepSize);
    setFontFamily(b.fontFamily);
    setFontBold(b.bold);
    setFontItalic(b.italic);
    setFontUnderline(b.underline);
    setTextOutline(b.outlined);
    setTextOutlineColor(b.outlineColor);
    setTextBackground(b.textBg);
    setTextBackgroundColor(b.textBgColor);
}

void AnnotationCanvas::selectAnnotAt(qreal imgX, qreal imgY)
{
    selectAnnot(annotAt(QPointF(imgX, imgY)));
}

// ------------------------------------------------------ OCR text-pick mode

void AnnotationCanvas::setOcrMode(bool on)
{
    if (m_ocrMode == on) return;
    // Invalidate any in-flight recognition: a result from a previous OCR pick
    // (or from before the mode was dismissed) must not repopulate this session.
    ++m_ocrSeq;
    m_ocrMode = on;
    if (!on) {
        m_ocrWords.clear();
        m_ocrLines.clear();
        m_ocrCaretA = m_ocrCaretB = m_ocrPressCaret = -1;
        m_ocrBusy = false;
        m_ocrDragging = false;
        m_ocrHoverLine = -1;
        m_ocrDidDrag = false;
    } else {
        // Entering: any placed-shape selection chrome would clutter the overlay.
        if (m_selectedAnnot >= 0) { m_selectedAnnot = -1; emit selectedAnnotChanged(); }
    }
    emit ocrChanged();
    update();
}

void AnnotationCanvas::clearOcrMode() { setOcrMode(false); }

void AnnotationCanvas::setOcrBusy(bool on)
{
    if (m_ocrBusy == on) return;
    m_ocrBusy = on;
    emit ocrChanged();
    update();
}

void AnnotationCanvas::setOcrWords(const QVector<OcrWord> &words)
{
    m_ocrWords = words;
    rebuildOcrLines();
    m_ocrCaretA = m_ocrCaretB = m_ocrPressCaret = -1;
    m_ocrHoverLine = -1;
    // Abandon any gesture in flight: a press-hold begun while OCR was still busy
    // recorded m_ocrPressCaret=-1 (no words yet). Without clearing m_ocrDragging
    // the continued drag would pin m_ocrCaretA at -1 and never form a selection.
    m_ocrDragging = false;
    m_ocrDidDrag = false;
    m_ocrBusy = false;
    emit ocrChanged();
    update();
}

// Group the reading-order glyphs into text lines. A line is a contiguous run of
// equal OcrWord::line (tesseract increments it monotonically per text line), and
// its band is the union of the run's glyph rects — one clean rectangle per line.
void AnnotationCanvas::rebuildOcrLines()
{
    m_ocrLines.clear();
    const int n = m_ocrWords.size();
    int i = 0;
    while (i < n) {
        const int ln = m_ocrWords[i].line;
        OcrLine L;
        L.first = i;
        QRectF band(m_ocrWords[i].rect);
        int j = i;
        while (j < n && m_ocrWords[j].line == ln) { band |= QRectF(m_ocrWords[j].rect); ++j; }
        L.last = j - 1;
        L.band = band;
        m_ocrLines.append(L);
        i = j;
    }
}

bool AnnotationCanvas::hasOcrSelection() const
{
    return m_ocrCaretA >= 0 && m_ocrCaretB >= 0 && m_ocrCaretA != m_ocrCaretB;
}

int AnnotationCanvas::ocrLineOfGlyph(int g) const
{
    for (int k = 0; k < m_ocrLines.size(); ++k)
        if (g >= m_ocrLines[k].first && g <= m_ocrLines[k].last)
            return k;
    return -1;
}

int AnnotationCanvas::ocrLineAt(const QPointF &p) const
{
    // Nearest text line, vertical distance dominating: a point in the gutter
    // between two lines snaps to the vertically closer one, and horizontal
    // position only breaks ties (e.g. two lines starting at the same y). -1 when
    // there are no lines.
    int best = -1;
    qreal bestD = 1e18;
    for (int k = 0; k < m_ocrLines.size(); ++k) {
        const QRectF b = m_ocrLines[k].band;
        qreal dy = 0;
        if (p.y() < b.top()) dy = b.top() - p.y();
        else if (p.y() > b.bottom()) dy = p.y() - b.bottom();
        qreal dx = 0;
        if (p.x() < b.left()) dx = b.left() - p.x();
        else if (p.x() > b.right()) dx = p.x() - b.right();
        const qreal d = dy * 4096.0 + dx; // y dominates; x is only a tiebreak
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

bool AnnotationCanvas::ocrOverLine(const QPointF &p, int line) const
{
    if (line < 0 || line >= m_ocrLines.size())
        return false;
    // A small margin around the band so an I-beam / line-click still lands when
    // the cursor grazes the line's top/bottom or sits just past its ends.
    const QRectF b = m_ocrLines[line].band;
    const qreal padY = b.height() * 0.35;
    const qreal padX = b.height() * 0.6;
    return b.adjusted(-padX, -padY, padX, padY).contains(p);
}

int AnnotationCanvas::ocrCaretAt(const QPointF &p) const
{
    if (m_ocrWords.isEmpty())
        return -1;
    const int k = ocrLineAt(p);
    if (k < 0)
        return -1;
    const OcrLine &L = m_ocrLines[k];
    if (p.x() <= QRectF(m_ocrWords[L.first].rect).left())
        return L.first;                       // caret before the line's first glyph
    if (p.x() >= QRectF(m_ocrWords[L.last].rect).right())
        return L.last + 1;                    // caret after the line's last glyph
    // Otherwise the nearest inter-glyph gap on this line: for each glyph decide
    // left- or right-edge by which side of its centre the cursor falls on.
    for (int i = L.first; i <= L.last; ++i) {
        const QRectF r(m_ocrWords[i].rect);
        if (p.x() < r.center().x())
            return i;                         // caret before glyph i
        if (p.x() <= r.right())
            return i + 1;                     // caret after glyph i
    }
    return L.last + 1;
}

int AnnotationCanvas::ocrGlyphAtOnLine(int line, qreal x) const
{
    if (line < 0 || line >= m_ocrLines.size())
        return -1;
    const OcrLine &L = m_ocrLines[line];
    int best = L.first;
    qreal bestD = 1e18;
    for (int i = L.first; i <= L.last; ++i) {
        const QRectF r(m_ocrWords[i].rect);
        if (x >= r.left() && x <= r.right())
            return i;
        const qreal d = qMin(qAbs(x - r.left()), qAbs(x - r.right()));
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void AnnotationCanvas::ocrExpandWord(int i, int &lo, int &hi) const
{
    if (m_ocrWords.isEmpty() || i < 0) { lo = hi = -1; return; }
    // A word's FIRST glyph carries spaceBefore; expand until the boundary or a
    // line change.
    const int ln = m_ocrWords[i].line;
    lo = i;
    while (lo > 0 && !m_ocrWords[lo].spaceBefore && m_ocrWords[lo - 1].line == ln) --lo;
    hi = i;
    while (hi + 1 < m_ocrWords.size() && !m_ocrWords[hi + 1].spaceBefore
           && m_ocrWords[hi + 1].line == ln) ++hi;
}

QRectF AnnotationCanvas::ocrCaretSpanDirty(int cA, int cB) const
{
    const int n = m_ocrWords.size();
    if (n == 0 || m_ocrLines.isEmpty())
        return {};
    // Carets are glyph boundaries; the affected glyphs are [min .. max-1]. The
    // selection bar for each touched line spans that line's FULL band height, so
    // dirty whole line bands (not just the glyph rects) or a partial selection
    // on a tall line would leave its ascenders/descenders un-repainted.
    const int g0 = qBound(0, qMin(cA, cB), n - 1);
    const int g1 = qBound(0, qMax(cA, cB) - 1, n - 1);
    int k0 = ocrLineOfGlyph(g0);
    int k1 = ocrLineOfGlyph(qMax(g0, g1));
    if (k0 < 0) k0 = 0;
    if (k1 < 0) k1 = m_ocrLines.size() - 1;
    if (k1 < k0) { const int t = k0; k0 = k1; k1 = t; }
    QRectF box;
    for (int k = k0; k <= k1 && k < m_ocrLines.size(); ++k)
        box |= m_ocrLines[k].band;
    return box.isNull() ? box : box.adjusted(-3, -3, 3, 3);
}

void AnnotationCanvas::ocrSelectAll()
{
    if (m_ocrWords.isEmpty()) return;
    m_ocrCaretA = 0;
    m_ocrCaretB = m_ocrWords.size();
    emit ocrChanged();
    update();
}

QString AnnotationCanvas::ocrSelectedText() const
{
    if (!hasOcrSelection())
        return {};
    const int lo = qMin(m_ocrCaretA, m_ocrCaretB);
    const int hiEx = qMax(m_ocrCaretA, m_ocrCaretB);
    // Glyphs joined in reading order: a space before a word boundary, a newline
    // where the line index advances. m_ocrWords is already in reading order.
    QString out;
    int lastLine = -1;
    for (int i = lo; i < hiEx && i < m_ocrWords.size(); ++i) {
        const OcrWord &g = m_ocrWords[i];
        if (lastLine < 0) {
            // first glyph
        } else if (g.line != lastLine) {
            out += QLatin1Char('\n');
        } else if (g.spaceBefore) {
            out += QLatin1Char(' ');
        }
        out += g.text;
        lastLine = g.line;
    }
    return out;
}

bool AnnotationCanvas::addOcrSelectionAnnotations(Tool type, const QColor &color,
                                                  bool filled, const QColor &fillColor)
{
    if (!hasOcrSelection() || m_ocrWords.isEmpty())
        return false;

    // OCR glyphs are in reading order. Therefore the contiguous [lo..hi]
    // range naturally clips the first line at the anchor and the last one at
    // the caret; middle lines contain their complete spans.
    const int n = m_ocrWords.size();
    const int lo = qBound(0, qMin(m_ocrCaretA, m_ocrCaretB), n - 1);
    const int hiEx = qBound(0, qMax(m_ocrCaretA, m_ocrCaretB), n);
    QVector<int> glyphs;
    glyphs.reserve(hiEx - lo);
    for (int i = lo; i < hiEx; ++i)
        glyphs.append(i);
    return addOcrGlyphAnnotations(glyphs, type, color, filled, fillColor);
}

bool AnnotationCanvas::addOcrGlyphAnnotations(const QVector<int> &glyphs, Tool type,
                                              const QColor &color, bool filled,
                                              const QColor &fillColor)
{
    if (glyphs.isEmpty() || m_ocrWords.isEmpty())
        return false;

    QVector<Annot> batch;
    QRectF dirty;
    int k = -1;          // current run's line
    int prevIdx = -2;    // last glyph index consumed (-2 never adjacent to 0)
    qreal xL = 0, xR = 0;
    QRectF band;
    auto flush = [&] {
        if (k < 0)
            return;
        Annot a;
        a.type = type;
        // Full line-band height (a redaction bar must fully hide the text's
        // ascenders/descenders), hugging the covered glyphs horizontally.
        a.rect = QRectF(xL, band.top(), xR - xL, band.height());
        a.color = color;
        a.filled = filled;
        a.fillColor = fillColor;
        a.width = m_strokeWidth;
        batch.append(a);
        dirty |= annotBoundsImg(a);
    };
    for (const int i : glyphs) {
        if (i < 0 || i >= m_ocrWords.size())
            continue;
        const int lk = ocrLineOfGlyph(i);
        const QRectF gr(m_ocrWords[i].rect);
        if (lk != k || i != prevIdx + 1) {
            flush();
            k = lk;
            band = (k >= 0 && k < m_ocrLines.size()) ? m_ocrLines[k].band : gr;
            xL = gr.left();
            xR = gr.right();
        } else {
            xL = qMin(xL, gr.left());
            xR = qMax(xR, gr.right());
        }
        prevIdx = i;
    }
    flush();
    if (batch.isEmpty())
        return false;

    // The batch is one user action even when it spans several text lines.
    pushUndo();
    m_items += batch;
    updateImgRect(dirty);
    // Stay in OCR mode so highlight/redact can CHAIN (mark one phrase, then
    // another) — the fresh marks are re-drawn on top of the scrim in paint().
    // Just drop the transient range so the selection bar clears and the action
    // buttons disable until the next pick; the explicit Done button exits.
    m_ocrCaretA = m_ocrCaretB = -1;
    m_ocrHoverLine = -1;
    emit ocrChanged();
    update();
    return true;
}

bool AnnotationCanvas::highlightOcrSelection()
{
    // Match the Highlight tool's automatic yellow without changing the active
    // tool or persisting an OCR-specific colour. A deliberate stroke pick is
    // still honoured for users who want a different highlighter colour.
    const QColor color = (m_strokeAuto || !m_strokeColorTouched)
                             ? QColor(255, 234, 112) : m_color;
    return addOcrSelectionAnnotations(Highlight, color);
}

bool AnnotationCanvas::redactOcrSelection(bool pixelate)
{
    Q_UNUSED(pixelate);
    // Redaction must DESTROY the underlying signal: a blur/mosaic of text is
    // partially recoverable (de-pixelation, known-font attacks), so a saved or
    // uploaded "redacted" password/token could still leak. Paint an opaque
    // filled bar over each text line instead.
    const QColor bar(0, 0, 0);
    return addOcrSelectionAnnotations(Rect, bar, /*filled=*/true, /*fillColor=*/bar);
}

QString AnnotationCanvas::ocrAllText(QVector<int> *charToGlyph) const
{
    QString out;
    if (charToGlyph)
        charToGlyph->clear();
    auto push = [&](const QString &s, int glyph) {
        out += s;
        if (charToGlyph)
            charToGlyph->insert(charToGlyph->size(), s.size(), glyph);
    };
    int lastLine = -1;
    for (int i = 0; i < m_ocrWords.size(); ++i) {
        const OcrWord &g = m_ocrWords[i];
        // Separators belong to no glyph (-1): a match may legitimately span
        // them (an e-mail never does, a multi-word phrase does), and mapping
        // them to a neighbour would extend a bar past the matched text.
        if (lastLine < 0) {
            // first glyph
        } else if (g.line != lastLine) {
            push(QStringLiteral("\n"), -1);
        } else if (g.spaceBefore) {
            push(QStringLiteral(" "), -1);
        }
        push(g.text, i);
        lastLine = g.line;
    }
    return out;
}

int AnnotationCanvas::redactTextMatching(const QString &pattern)
{
    if (m_ocrWords.isEmpty() || pattern.isEmpty())
        return 0;
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid())
        return -1;

    QVector<int> map;
    const QString text = ocrAllText(&map);
    if (text.isEmpty())
        return 0;

    QSet<int> hitSet;
    int matches = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        // A zero-width match (e.g. a pattern that is all-optional) covers no
        // glyph — counting it would report redactions that left no bar.
        if (m.capturedLength() <= 0)
            continue;
        bool covered = false;
        for (int c = m.capturedStart(); c < m.capturedEnd(); ++c) {
            const int g = map.value(c, -1);
            if (g >= 0) {
                hitSet.insert(g);
                covered = true;
            }
        }
        if (covered)
            ++matches;
    }
    if (hitSet.isEmpty())
        return 0;

    QVector<int> glyphs(hitSet.cbegin(), hitSet.cend());
    // addOcrGlyphAnnotations breaks runs on an index gap, so the indices must
    // arrive ascending or every glyph would become its own bar.
    std::sort(glyphs.begin(), glyphs.end());
    const QColor bar(0, 0, 0);
    if (!addOcrGlyphAnnotations(glyphs, Rect, bar, /*filled=*/true, /*fillColor=*/bar))
        return 0;
    return matches;
}

void AnnotationCanvas::setGlyphBoxes(const QVector<OcrWord> &words)
{
    m_glyphBoxes = words;
    m_glyphBoxesReady = true;
    m_glyphBoxesBaseKey = m_base.cacheKey();
}

// Does the segment a→b touch rect r (an endpoint inside, or crossing any edge)?
static bool segIntersectsRect(const QPointF &a, const QPointF &b, const QRectF &r)
{
    if (r.contains(a) || r.contains(b))
        return true;
    const QLineF seg(a, b);
    const QLineF edges[4] = {
        QLineF(r.topLeft(), r.topRight()),
        QLineF(r.topRight(), r.bottomRight()),
        QLineF(r.bottomRight(), r.bottomLeft()),
        QLineF(r.bottomLeft(), r.topLeft())
    };
    QPointF ip;
    for (const QLineF &e : edges)
        if (seg.intersects(e, &ip) == QLineF::BoundedIntersection)
            return true;
    return false;
}

bool AnnotationCanvas::applyTextAwareHighlight(const Annot &stroke)
{
    if (!m_glyphBoxesReady || m_glyphBoxes.isEmpty()
        || m_glyphBoxesBaseKey != m_base.cacheKey())
        return false;
    // Text mode is a PEN: highlight every glyph the freehand stroke passes
    // through. Needs a real path, not a single tap.
    const QVector<QPointF> &pts = stroke.points;
    if (pts.size() < 2)
        return false;

    // Group the crossed glyphs by text line and union their rects. The band
    // therefore takes each LINE's own glyph height — sized to the text like a
    // PDF highlighter — and spans exactly the glyphs the pen crossed (not a
    // fixed box or the stroke's bounding rect).
    const qreal hw = qMax<qreal>(stroke.width / 2.0, 2.0); // pen radius
    QMap<int, QRectF> lineRects;
    int hitCount = 0;
    for (const OcrWord &g : std::as_const(m_glyphBoxes)) {
        const QRectF gr(g.rect);
        if (gr.isEmpty())
            continue;
        // Inflate by the pen radius so a stroke grazing a glyph still counts.
        const QRectF hitRect = gr.adjusted(-hw, -hw, hw, hw);
        bool crossed = false;
        for (int i = 1; i < pts.size() && !crossed; ++i)
            crossed = segIntersectsRect(pts[i - 1], pts[i], hitRect);
        if (!crossed)
            continue;
        ++hitCount;
        if (lineRects.contains(g.line)) lineRects[g.line] |= gr;
        else lineRects.insert(g.line, gr);
    }
    // A couple of glyphs minimum: a stray graze over a picture's OCR noise must
    // not turn into a highlight (the caller then keeps the freehand marker).
    if (hitCount < 2 || lineRects.isEmpty())
        return false;

    pushUndo();
    QRectF dirty;
    for (const QRectF &b : std::as_const(lineRects)) {
        Annot a;
        a.type = Highlight;
        a.rect = b;                 // text-height band over the crossed glyphs
        a.color = stroke.color;
        a.width = stroke.width;
        m_items.append(a);
        dirty |= annotBoundsImg(a);
    }
    updateImgRect(dirty);
    return true;
}

void AnnotationCanvas::clearAnnotSelection()
{
    if (m_selectedAnnot < 0) return;
    m_selectedAnnot = -1;
    m_lastCoalesceProp = -1;
    m_lastCoalesceIndex = -1;
    restoreStyleBackup();
    emit selectedAnnotChanged();
    update();
}

void AnnotationCanvas::removeSelectedAnnot()
{
    if (m_selectedAnnot < 0 || m_selectedAnnot >= m_items.size()) return;
    pushUndo();
    m_items.remove(m_selectedAnnot);
    m_selectedAnnot = -1;
    restoreStyleBackup();
    emit selectedAnnotChanged();
    update();
}

void AnnotationCanvas::nudgeSelectedAnnot(qreal dx, qreal dy)
{
    if (m_selectedAnnot < 0 || m_selectedAnnot >= m_items.size()) return;
    pushUndoCoalesced(PGeometry);
    Annot &a = m_items[m_selectedAnnot];
    const QRectF oldB = annotBoundsImg(a);
    a.rect.translate(dx, dy);
    for (QPointF &p : a.points)
        p += QPointF(dx, dy);
    // Dirty-rect repaint (old ∪ new) like the MoveAnnot drag path — a full
    // update() re-rasterized the whole scaled base per arrow-key autorepeat.
    updateImgRect(oldB.united(annotBoundsImg(a)));
}

void AnnotationCanvas::commitTextEdit(const QString &text)
{
    if (m_selectedAnnot < 0 || m_selectedAnnot >= m_items.size()) return;
    if (m_items[m_selectedAnnot].type != Text) return;
    pushUndo();
    if (text.trimmed().isEmpty()) {
        m_items.remove(m_selectedAnnot);
        m_selectedAnnot = -1;
        restoreStyleBackup();
        emit selectedAnnotChanged();
    } else {
        m_items[m_selectedAnnot].text = text;
    }
    update();
}

// Reverse z-order (topmost first) hit-test over placed annotations.
int AnnotationCanvas::annotAt(const QPointF &pos) const
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        const Annot &a = m_items[i];
        const qreal tol = qMax(a.width / 2.0 + 3.0, 6.0 / qMax(0.05, renderScale()));
        switch (a.type) {
        case Rect: case Blur: case Pixelate: case Highlight: case SmartErase: case PastedImage:
        case Magnify: {
            const QRectF r = a.rect.normalized();
            const bool solid = a.filled || a.type == Blur || a.type == Pixelate
                               || a.type == Highlight || a.type == SmartErase
                               || a.type == PastedImage || a.type == Magnify;
            if (solid) {
                if (r.contains(pos)) return i;
            } else {
                // Outline only: within tol of the border band.
                const QRectF outer = r.adjusted(-tol, -tol, tol, tol);
                const QRectF inner = r.adjusted(tol, tol, -tol, -tol);
                if (outer.contains(pos) && !inner.contains(pos)) return i;
            }
            break;
        }
        case Callout: {
            const QPainterPath path = calloutPath(a.rect);
            if (a.filled) {
                if (path.contains(pos)) return i;
            } else {
                QPainterPathStroker stroker;
                stroker.setWidth(a.width + 2.0 * tol);
                if (stroker.createStroke(path).contains(pos)) return i;
            }
            break;
        }
        case Ellipse: {
            const QRectF r = a.rect.normalized();
            if (r.width() < 1 || r.height() < 1) break;
            const qreal nx = (pos.x() - r.center().x()) / (r.width() / 2.0);
            const qreal ny = (pos.y() - r.center().y()) / (r.height() / 2.0);
            const qreal d = nx * nx + ny * ny;
            if (a.filled) {
                if (d <= 1.0) return i;
            } else {
                // Ring around the unit circle, tolerance scaled by radius.
                const qreal band = tol / qMax(r.width(), r.height());
                if (d >= (1.0 - band) * (1.0 - band) && d <= (1.0 + band) * (1.0 + band))
                    return i;
            }
            break;
        }
        case Line: case Arrow: case Measure: {
            if (QLineF(a.rect.topLeft(), pos).length() <= tol
                || QLineF(a.rect.bottomRight(), pos).length() <= tol)
                return i;
            const QLineF seg(a.rect.topLeft(), a.rect.bottomRight());
            const qreal len = seg.length();
            if (len < 1) break;
            const QPointF d = seg.p2() - seg.p1();
            const qreal t = qBound(0.0, QPointF::dotProduct(pos - seg.p1(), d) / (len * len), 1.0);
            const QPointF proj = seg.p1() + t * d;
            if (QLineF(pos, proj).length() <= tol) return i;
            break;
        }
        case Pen: {
            for (int k = 1; k < a.points.size(); ++k) {
                const QLineF seg(a.points[k - 1], a.points[k]);
                const qreal len = seg.length();
                if (len < 1) continue;
                const QPointF d = seg.p2() - seg.p1();
                const qreal t = qBound(0.0, QPointF::dotProduct(pos - seg.p1(), d) / (len * len), 1.0);
                if (QLineF(pos, seg.p1() + t * d).length() <= tol) return i;
            }
            break;
        }
        case Text:
            if (textBoundsImg(a).adjusted(-tol, -tol, tol, tol).contains(pos)) return i;
            break;
        case Step: {
            const qreal r = qMax(14.0, a.stepSize * 0.9);
            if (QLineF(a.rect.topLeft(), pos).length() <= r) return i;
            break;
        }
        default:
            break;
        }
    }
    // Second pass: the INTERIOR of unfilled rects/ellipses. Only their border
    // paints, but the inside is the natural click target — without this,
    // selecting an empty frame required pixel-hunting its outline (reported
    // live: "clicking the shape does nothing"). A second pass so any exact
    // hit (a smaller shape inside the frame, the frame's own border) wins
    // first.
    for (int i = m_items.size() - 1; i >= 0; --i) {
        const Annot &a = m_items[i];
        if (a.filled)
            continue;
        if (a.type == Rect) {
            if (a.rect.normalized().contains(pos))
                return i;
        } else if (a.type == Callout) {
            if (calloutPath(a.rect).contains(pos))
                return i;
        } else if (a.type == Ellipse) {
            const QRectF r = a.rect.normalized();
            if (r.width() < 1 || r.height() < 1)
                continue;
            const qreal nx = (pos.x() - r.center().x()) / (r.width() / 2.0);
            const qreal ny = (pos.y() - r.center().y()) / (r.height() / 2.0);
            if (nx * nx + ny * ny <= 1.0)
                return i;
        }
    }
    return -1;
}

// Handle points for the selected annotation. Rect-like shapes get 8 handles
// (same order as the crop-rect handles), line tools get 2 endpoints.
// Pen/Text/Step are move-only → 0 handles.
int AnnotationCanvas::handlesForSelected(QPointF out[8]) const
{
    if (m_selectedAnnot < 0 || m_selectedAnnot >= m_items.size()) return 0;
    const Annot &a = m_items[m_selectedAnnot];
    // A freehand highlighter stroke is points-based (like Pen) → move-only, no
    // rect resize handles.
    if (a.type == Highlight && !a.points.isEmpty())
        return 0;
    switch (a.type) {
    case Rect: case Ellipse: case Callout: case Blur: case Pixelate: case Highlight: case SmartErase: case PastedImage: case Magnify: {
        const QRectF r = a.rect.normalized();
        out[0] = r.topLeft();  out[1] = {r.center().x(), r.top()};    out[2] = r.topRight();
        out[3] = {r.left(), r.center().y()}; out[4] = {r.right(), r.center().y()};
        out[5] = r.bottomLeft(); out[6] = {r.center().x(), r.bottom()}; out[7] = r.bottomRight();
        return 8;
    }
    case Line: case Arrow: case Measure:
        out[0] = a.rect.topLeft();
        out[1] = a.rect.bottomRight();
        return 2;
    default:
        return 0;
    }
}

int AnnotationCanvas::hitAnnotHandle(const QPointF &pos) const
{
    QPointF pts[8];
    const int n = handlesForSelected(pts);
    const qreal tol = 12.0 / qMax(0.05, renderScale());
    for (int i = 0; i < n; ++i)
        if (QLineF(pos, pts[i]).length() <= tol) return i;
    return -1;
}

// Cursor for direct shape editing: the selected shape's resize handles map to
// the 8 directional cursors (a Line/Arrow's 2 endpoint handles are diagonal-
// agnostic → SizeAll), its body → pointing hand, empty space → arrow. Shared by
// the Edit tool and the drawing-tool direct-manipulation path.
Qt::CursorShape AnnotationCanvas::annotEditCursor(const QPointF &pos) const
{
    const int h = (m_selectedAnnot >= 0) ? hitAnnotHandle(pos) : -1;
    if (h >= 0) {
        QPointF pts[8];
        const int n = handlesForSelected(pts);
        if (n == 2 || n == 3) return Qt::SizeAllCursor;
        if (h == 0 || h == 7) return Qt::SizeFDiagCursor;
        if (h == 2 || h == 5) return Qt::SizeBDiagCursor;
        if (h == 1 || h == 6) return Qt::SizeVerCursor;
        return Qt::SizeHorCursor;
    }
    return annotAt(pos) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor;
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
    update();
}

void AnnotationCanvas::selectAll()
{
    m_selection = QRectF(QPointF(0, 0), QSizeF(m_base.size()));
    emit selectionRectChanged();
    update();
}

void AnnotationCanvas::setSelectionRect(const QRectF &r)
{
    m_selection = r.normalized().intersected(QRectF(QPointF(0, 0), QSizeF(m_base.size())));
    emit selectionRectChanged();
    update();
}

// Escape-cancel for an in-progress selection: drops the rect without touching
// annotations or the base image.
void AnnotationCanvas::clearSelection()
{
    m_selection = {};
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
        a.srcRect.translate(-r.x(), -r.y());   // Magnify source region
        for (QPointF &p : a.points)
            p -= QPointF(r.x(), r.y());
    }
    m_selection = {};
    if (m_selectedAnnot >= 0) { m_selectedAnnot = -1; emit selectedAnnotChanged(); }
    emit imageChanged();
    emit renderScaleChanged();
    emit selectionRectChanged();
    update();
}

// ---------------------------------------------------------------- painting

static qreal arrowHeadLen(qreal w) { return qMax(12.0, w * 4.5); }

// Font for a Text annotation. Kept in sync with the QML input overlays'
// WYSIWYG preview (EditorWindow/OverlayWindow text editors).
QFont AnnotationCanvas::annotFontFor(const Annot &a) const
{
    QFont f;
    if (!a.fontFamily.isEmpty())
        f.setFamily(a.fontFamily);
    f.setPixelSize(a.fontSize);
    f.setBold(a.bold);
    f.setItalic(a.italic);
    f.setUnderline(a.underline);
    return f;
}

QRectF AnnotationCanvas::textBoundsImg(const Annot &a) const
{
    const QFontMetricsF fm(annotFontFor(a));
    const QStringList lines = a.text.split(QLatin1Char('\n'));
    qreal w = 0;
    for (const QString &l : lines)
        w = qMax(w, fm.horizontalAdvance(l));
    // First baseline sits at rect.top() + fontSize (legacy single-line
    // placement); the box spans from that line's ascent to the last descent.
    const qreal top = a.rect.top() + a.fontSize - fm.ascent();
    const qreal h = fm.lineSpacing() * (lines.size() - 1) + fm.height();
    return QRectF(a.rect.left(), top, w, h);
}

// Draws the filled arrowhead with its apex exactly at `to`. Assumes the brush
// is already set to the fill color.
static void drawArrowHead(QPainter &p, const QPointF &from, const QPointF &to,
                          qreal w, int style)
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
    const QPointF left = rot(angle);
    const QPointF right = rot(-angle);
    if (style == 1) {
        const QColor color = p.brush().color();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, qMax<qreal>(1.0, w), Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(left, to);
        p.drawLine(to, right);
    } else {
        QPolygonF head{to, left, right};
        p.setPen(Qt::NoPen);
        p.drawPolygon(head);
    }
}

void AnnotationCanvas::drawAnnot(QPainter &p, const Annot &a) const
{
    p.save();
    QPen pen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (a.type) {
    case Pen: {
        if (a.points.size() == 1) {
            // A pen tap is a deliberate dot, but a 1-point path has no segments
            // and drawPath would paint nothing (leaving an invisible annotation
            // that only a puzzling Ctrl+Z removes). Render a filled dot the
            // width of the stroke instead.
            p.setBrush(a.color);
            p.setPen(Qt::NoPen);
            const qreal rad = qMax(a.width, 1.0) / 2.0;
            p.drawEllipse(a.points.first(), rad, rad);
            break;
        }
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
        const bool bothEnds = a.arrowHeadStyle == 2;
        if (shaft.length() > headLen * (bothEnds ? 1.8 : 0.9)) {
            const QPointF unit = (tip - tail) / shaft.length();
            const QPointF start = bothEnds ? tail + unit * (headLen * 0.82) : tail;
            const QPointF end = tip - unit * (headLen * 0.82);
            QPen shaftPen(a.color, a.width, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin);
            p.setPen(shaftPen);
            p.drawLine(start, end);
        }
        p.setBrush(a.color);
        drawArrowHead(p, tail, tip, a.width, a.arrowHeadStyle);
        if (bothEnds)
            drawArrowHead(p, tip, tail, a.width, a.arrowHeadStyle);
        break;
    }
    case Measure: {
        QFont f = p.font();
        f.setPixelSize(qMax(12, int(a.width * 4)));
        f.setBold(true);
        const auto drawLabel = [&](const QString &label, const QPointF &center) {
            p.setFont(f);
            const QFontMetricsF fm(f);
            QRectF box = fm.boundingRect(label).adjusted(-5, -3, 5, 3);
            box.moveCenter(center);
            const QColor stroke = p.pen().color();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(23, 21, 59, 220));
            p.drawRoundedRect(box, 4, 4);
            p.setPen(Qt::white);
            p.drawText(box, Qt::AlignCenter, label);
            p.setPen(stroke); // restore for any following strokes
        };
        if (a.filled) {
            // Size box: a rectangle labelled with its W × H.
            const QRectF r = a.rect.normalized();
            if (r.width() < 1.0 && r.height() < 1.0)
                break;
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            drawLabel(QStringLiteral("%1 × %2").arg(qRound(r.width())).arg(qRound(r.height())),
                      r.center());
            break;
        }
        // Distance line.
        const QPointF from = a.rect.topLeft();
        const QPointF to = a.rect.bottomRight();
        const QLineF line(from, to);
        if (line.length() < 1.0)
            break;
        p.drawLine(line);
        QLineF tick = line.normalVector();
        tick.setLength(qMax<qreal>(8.0, a.width * 2.5));
        const QPointF half = (tick.p2() - tick.p1()) / 2.0;
        p.drawLine(from - half, from + half);
        p.drawLine(to - half, to + half);
        drawLabel(QStringLiteral("%1 px · %2°")
                      .arg(qRound(line.length()))
                      .arg(qRound(-line.angle())),
                  (from + to) / 2.0 - half * 1.8);
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
    case Callout:
        if (a.filled) p.setBrush(a.fillColor);
        p.drawPath(calloutPath(a.rect));
        break;
    case SmartErase: {
        const QRect r = a.rect.normalized().toAlignedRect().intersected(m_base.rect());
        if (r.width() >= 2 && r.height() >= 2) {
            // Same patch cache as Blur (identical inputs → identical patch), and
            // the same fast-while-dragging rule.
            const bool wantFast = m_fxFast;
            if (a.fxBaseKey != m_base.cacheKey() || a.fxRect != a.rect
                || (a.fxFast && !wantFast)) {
                a.fxPatch = smartErasePatch(r, wantFast);
                a.fxBaseKey = m_base.cacheKey();
                a.fxRect = a.rect;
                a.fxFast = wantFast;
            }
            p.drawImage(r, a.fxPatch);
        }
        break;
    }
    case Text: {
        const QFont f = annotFontFor(a);
        p.setFont(f);
        const QFontMetricsF fm(f);
        const QStringList lines = a.text.split(QLatin1Char('\n'));
        const qreal lh = fm.lineSpacing();
        const QPointF origin = a.rect.topLeft();

        if (a.textBg) {
            p.setPen(Qt::NoPen);
            p.setBrush(a.textBgColor);
            p.drawRoundedRect(textBoundsImg(a).adjusted(-6, -4, 6, 4), 4, 4);
            p.setBrush(Qt::NoBrush);
        }

        if (a.outlined) {
            // Glyphs as a path: stroke with the outline color, fill with the
            // text color. addText() carries no underline decoration, so the
            // underline is drawn per line by hand.
            QPainterPath path;
            for (int i = 0; i < lines.size(); ++i)
                path.addText(origin + QPointF(0, a.fontSize + i * lh), f, lines[i]);
            p.setPen(QPen(a.outlineColor, qMax(1.0, a.fontSize / 12.0),
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(a.color);
            p.drawPath(path);
            if (a.underline) {
                p.setPen(QPen(a.color, qMax(1.0, fm.lineWidth())));
                for (int i = 0; i < lines.size(); ++i) {
                    const qreal y = origin.y() + a.fontSize + i * lh + fm.underlinePos();
                    p.drawLine(QPointF(origin.x(), y),
                               QPointF(origin.x() + fm.horizontalAdvance(lines[i]), y));
                }
            }
        } else {
            // Subtle contrast shadow so text is readable on any background —
            // skipped when a background box already provides the contrast.
            if (!a.textBg) {
                p.setPen(QColor(0, 0, 0, 160));
                for (int i = 0; i < lines.size(); ++i)
                    p.drawText(origin + QPointF(1.5, a.fontSize + 1.5 + i * lh), lines[i]);
            }
            p.setPen(a.color);
            for (int i = 0; i < lines.size(); ++i)
                p.drawText(origin + QPointF(0, a.fontSize + i * lh), lines[i]);
        }
        break;
    }
    case PastedImage:
        if (!a.image.isNull())
            p.drawImage(a.rect.normalized(), a.image);
        break;
    case Magnify: {
        const QRectF dest = a.rect.normalized();
        if (a.srcRect.isEmpty()) {
            // Live drag: the rect is still the SOURCE being picked out — show
            // it as a dashed frame so the user sees what will be magnified.
            p.setPen(QPen(a.color, a.width, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawRect(dest);
            break;
        }
        if (dest.width() < 2 || dest.height() < 2)
            break;
        // The loupe: the source pixels blown up to the destination rect.
        // Nearest-neighbour while enlarging — the point is readability, and
        // smooth sampling blurs the very pixels it is meant to blow up.
        p.setRenderHint(QPainter::SmoothPixmapTransform,
                        dest.width() < a.srcRect.width());
        p.drawImage(dest, m_base, a.srcRect);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // Halo under the border separates the loupe from identical content.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 90), a.width + 2.0,
                      Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawRect(dest);
        p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        p.drawRect(dest);
        break;
    }
    case Blur: {
        const QRect r = a.rect.normalized().toAlignedRect().intersected(m_base.rect());
        if (r.width() > 4 && r.height() > 4) {
            // Cached patch: identical inputs (base data, rect, width) produce a
            // byte-identical patch, so only recompute when one of them changed.
            // While the geometry is being dragged (m_fxFast) the three rescales
            // use fast (nearest) sampling — the smooth version, ~an order of
            // magnitude dearer, is rebuilt once when the drag ends (its cached
            // patch is invalidated by the fxFast term below).
            const bool wantFast = m_fxFast;
            if (a.fxBaseKey != m_base.cacheKey() || a.fxRect != a.rect
                || a.fxWidth != a.width || (a.fxFast && !wantFast)) {
                const Qt::TransformationMode tm =
                    wantFast ? Qt::FastTransformation : Qt::SmoothTransformation;
                QImage region = m_base.copy(r);
                const int f = 10;
                QImage small = region.scaled(qMax(1, r.width() / f), qMax(1, r.height() / f),
                                             Qt::IgnoreAspectRatio, tm);
                small = small.scaled(qMax(1, r.width() / (f / 2)), qMax(1, r.height() / (f / 2)),
                                     Qt::IgnoreAspectRatio, tm);
                a.fxPatch = small.scaled(r.size(), Qt::IgnoreAspectRatio, tm);
                a.fxBaseKey = m_base.cacheKey();
                a.fxRect = a.rect;
                a.fxWidth = a.width;
                a.fxFast = wantFast;
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
        if (!a.points.isEmpty()) {
            // Freehand marker: a translucent stroke that follows the pen.
            if (a.points.size() == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                const qreal rad = qMax(a.width, 1.0) / 2.0;
                p.drawEllipse(a.points.first(), rad, rad);
            } else {
                QPainterPath path;
                path.moveTo(a.points.first());
                for (int i = 1; i < a.points.size(); ++i)
                    path.lineTo(a.points[i]);
                p.setPen(QPen(c, qMax<qreal>(1.0, a.width),
                              Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
        } else {
            // Rectangle band (rect mode) or per-line text-snap band.
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRect(a.rect.normalized());
        }
        break;
    }
    case Step: {
        const qreal r = qMax(14.0, a.stepSize * 0.9);
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
    // Cull annotations outside the current clip. On a partial repaint the
    // painter is clipped to the dirty rect (in image space, since paint()
    // scales by renderScale before this), and building each Pen path / Text
    // glyph layout is not free — doing it for items QPainter would clip away
    // anyway made per-frame cost scale with total annotation count. An empty
    // clip means a full render (rendered()/renderedSelection() or a whole-item
    // repaint) → draw everything.
    const QRectF clip = p.clipBoundingRect();
    const bool cull = !clip.isEmpty();
    for (const Annot &a : m_items) {
        if (cull && !annotBoundsImg(a).intersects(clip))
            continue;
        drawAnnot(p, a);
    }
    if (m_drawing)
        drawAnnot(p, m_current);
}

void AnnotationCanvas::updateImgRect(const QRectF &imgRect)
{
    if (imgRect.isNull()) { update(); return; }
    const qreal s = renderScale();
    update(QRectF(imgRect.x() * s, imgRect.y() * s,
                  imgRect.width() * s, imgRect.height() * s)
               .toAlignedRect().adjusted(-8, -8, 8, 8));
}

QRectF AnnotationCanvas::annotBoundsImg(const Annot &a) const
{
    QRectF r;
    if (a.type == Measure) {
        // Measure draws far outside its line rect: perpendicular end ticks and
        // a floating label box near the midpoint (see drawAnnot Measure). The
        // default width-based pad misses both, so move/resize left a ghost trail
        // and clipped the label. Union the tick extents and a generous label box.
        const QPointF from = a.rect.topLeft();
        const QPointF to = a.rect.bottomRight();
        QLineF tick = QLineF(from, to).normalVector();
        tick.setLength(qMax<qreal>(8.0, a.width * 2.5));
        const QPointF half = (tick.p2() - tick.p1()) / 2.0;
        r = QRectF(from, to).normalized();
        r |= QRectF(from - half, from + half).normalized();
        r |= QRectF(to - half, to + half).normalized();
        const qreal fontPx = qMax<qreal>(12.0, a.width * 4.0);
        QRectF label(0, 0, fontPx * 9.0, fontPx * 1.8); // over-estimate the text box
        label.moveCenter((from + to) / 2.0 - half * 1.8);
        r |= label;
        const qreal pad = a.width / 2.0 + 4.0;
        return r.adjusted(-pad, -pad, pad, pad);
    }
    if (!a.points.isEmpty()) {
        r = QRectF(a.points.first(), QSizeF(0, 0));
        for (const QPointF &pt : a.points)
            r |= QRectF(pt, QSizeF(0, 0));
    } else if (a.type == Text) {
        // rect is a zero-size origin for text — measure the laid-out lines
        // (plus the background box padding, covered by the slack below).
        r = textBoundsImg(a);
    } else {
        r = a.rect.normalized();
    }
    if (a.type == Callout)
        r.setBottom(r.bottom() + calloutTailHeight(r));
    // Stroke width + the arrow head (largest overdraw any tool does) + slack
    // for antialiasing; over-estimating only repaints a few extra pixels.
    qreal pad = a.width / 2.0 + arrowHeadLen(a.width) + 4.0;
    // Step draws a circle of radius max(14, stepSize*0.9) centred on rect's
    // top-left (plus a 2px white ring + selection halo) — far past the default
    // width-based pad, which otherwise left drag trails at large sizes / zoom.
    if (a.type == Step)
        pad = qMax(pad, qMax(14.0, a.stepSize * 0.9) + 4.0);
    return r.adjusted(-pad, -pad, pad, pad);
}

// The pending-full-repaint latch (see the header): full stays full until this
// paint delivers it, no matter how many partial requests land in between.
// Empirically hit AGAIN by the loupe: a hover's partial loupe update right
// after a click's selectAll() full update (same sync cycle, different events)
// left everything outside the loupe trail stale — the loupe "cleaned" the
// overlay as it moved.
void AnnotationCanvas::update(const QRect &rect)
{
    if (rect.isNull())
        m_fullDirty = true;
    if (m_fullDirty)
        QQuickPaintedItem::update();
    else
        QQuickPaintedItem::update(rect);
}

void AnnotationCanvas::paint(QPainter *painter)
{
    // The scenegraph is now consuming the accumulated dirty region (GUI thread
    // blocked during sync) — a full repaint, if one was pending, is delivered
    // by this call, so partial updates may shrink the region again.
    m_fullDirty = false;
    if (m_base.isNull()) return;
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal s = renderScale();
    painter->save();
    painter->scale(s, s);
    painter->drawImage(0, 0, m_base);
    drawAll(*painter);

    if (m_selectionMode) {
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

    // Outline the selected annotation + draw its handles on top — under ANY
    // tool that can hold a selection (Edit tool or a drawing tool after a
    // click-select), so direct editing is always visible.
    if (m_selectedAnnot >= 0 && m_selectedAnnot < m_items.size()) {
        const Annot &a = m_items[m_selectedAnnot];
        QPointF pts[8];
        const int n = handlesForSelected(pts);
        // Dashed bounds around the shape (a light guide; the handles are the
        // real affordance). Pen/Text/Step have no scale handles → just bounds.
        QRectF bounds = (a.type == Text) ? textBoundsImg(a) : a.rect.normalized();
        if (a.type == Pen && !a.points.isEmpty()) {
            bounds = QRectF(a.points.first(), QSizeF(0, 0));
            for (const QPointF &pt : a.points) bounds |= QRectF(pt, QSizeF(0, 0));
        }
        if (a.type == Step) {
            const qreal r = qMax(14.0, a.stepSize * 0.9);
            bounds = QRectF(a.rect.topLeft() - QPointF(r, r), QSizeF(2 * r, 2 * r));
        }
        painter->setBrush(Qt::NoBrush);
        QPen halo(QColor(255, 255, 255, 150), 3.0 / s);
        halo.setStyle(Qt::SolidLine);
        painter->setPen(halo);
        painter->drawRect(bounds.adjusted(-2, -2, 2, 2));
        QPen dash(m_uiAccent, 1.5 / s);
        dash.setStyle(Qt::DashLine);
        painter->setPen(dash);
        painter->drawRect(bounds.adjusted(-2, -2, 2, 2));
        // Handles (rect-like / endpoints).
        painter->setBrush(m_uiAccent);
        painter->setPen(QPen(m_uiScrim, 1.0 / s));
        const qreal hs = 5.0 / s;
        for (int i = 0; i < n; ++i)
            painter->drawEllipse(pts[i], hs, hs);
    }

    // OCR text-pick overlay: a light scrim keeps the screenshot readable, every
    // recognized line is boxed so the user sees exactly what is selectable
    // (Windows-Snipping-style), and the selection is drawn as ONE clean bar per
    // line — full line-band height — so it reads as text.
    if (m_ocrMode) {
        QColor scrim = m_uiScrim; scrim.setAlpha(90);
        painter->setPen(Qt::NoPen);
        painter->setBrush(scrim);
        painter->drawRect(QRectF(QPointF(0, 0), QSizeF(m_base.size())));

        // Always-visible selectable-text regions: a faint rounded box hugging
        // each recognized line so the text to be selected is obvious over the
        // scrim, not hidden. The line under the pointer brightens.
        const qreal rad = 3.0 / s;
        for (int k = 0; k < m_ocrLines.size(); ++k) {
            const bool hot = (k == m_ocrHoverLine) && !m_ocrDragging;
            const QRectF b = m_ocrLines[k].band.adjusted(-2, -1, 2, 1);
            QColor fill = m_uiAccent; fill.setAlpha(hot ? 55 : 22);
            QColor bord = m_uiAccent; bord.setAlpha(hot ? 140 : 70);
            painter->setBrush(fill);
            painter->setPen(QPen(bord, 1.0 / s));
            painter->drawRoundedRect(b, rad, rad);
        }

        // Selection: one clean bar per line — full line-band height (consistent
        // regardless of which glyphs are picked), hugging the selected glyphs.
        if (hasOcrSelection()) {
            const int lo = qMin(m_ocrCaretA, m_ocrCaretB);
            const int hiEx = qMax(m_ocrCaretA, m_ocrCaretB);
            QColor fill = m_uiAccent; fill.setAlpha(150);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            int k = -1;
            qreal xL = 0, xR = 0;
            QRectF band;
            auto flush = [&] {
                if (k < 0) return;
                const QRectF r(xL, band.top(), xR - xL, band.height());
                painter->drawRoundedRect(r.adjusted(-1, -1, 1, 1), rad, rad);
            };
            for (int i = lo; i < hiEx && i < m_ocrWords.size(); ++i) {
                const int lk = ocrLineOfGlyph(i);
                const QRectF gr(m_ocrWords[i].rect);
                if (lk != k) {
                    flush();
                    k = lk;
                    band = (k >= 0 && k < m_ocrLines.size()) ? m_ocrLines[k].band : gr;
                    xL = gr.left(); xR = gr.right();
                } else {
                    xL = qMin(xL, gr.left()); xR = qMax(xR, gr.right());
                }
            }
            flush();
        }

        // Re-draw already-placed annotations ON TOP of the scrim so a fresh
        // highlight/redact stays visible and the actions can CHAIN without
        // leaving OCR mode (the scrim above would otherwise hide the new mark).
        for (const Annot &a : m_items)
            drawAnnot(*painter, a);
    }
    painter->restore();

    // Pixel loupe — drawn last, in ITEM space (UI chrome, never
    // exported: rendered()/renderedSelection() go through drawAll, not here).
    const QRectF panel = pixelLoupeRect();
    if (!panel.isEmpty()) {
        const int z = m_pixelLoupeZoom;
        const int n = loupeGridCells();
        // The exact image pixel under the cursor = the loupe's centre cell.
        QPoint c = toImage(m_hoverPoint.x(), m_hoverPoint.y()).toPoint();
        c.setX(qBound(0, c.x(), m_base.width() - 1));
        c.setY(qBound(0, c.y(), m_base.height() - 1));
        // copy() zero-fills the part of the window that falls outside the
        // image, which reads as the dark panel background — no clamping shift.
        const QImage patch = m_base.copy(c.x() - n / 2, c.y() - n / 2, n, n)
                                 .scaled(n * z, n * z, Qt::IgnoreAspectRatio,
                                         Qt::FastTransformation);
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(panel, QColor(20, 18, 45, 240));
        const QRectF body(panel.x() + 1, panel.y() + 1, n * z, n * z);
        painter->drawImage(body.topLeft(), patch);
        // Pixel grid once the cells are big enough to have one.
        if (z >= 6) {
            painter->setPen(QColor(0, 0, 0, 70));
            for (int i = 1; i < n; ++i) {
                const qreal gx = body.x() + i * z;
                const qreal gy = body.y() + i * z;
                painter->drawLine(QPointF(gx, body.top()), QPointF(gx, body.bottom()));
                painter->drawLine(QPointF(body.left(), gy), QPointF(body.right(), gy));
            }
        }
        // Crosshair bands through the hovered pixel's row/column + the cell
        // itself outlined accent-on-white so it reads on any content.
        const QRectF cell(body.x() + (n / 2) * z, body.y() + (n / 2) * z, z, z);
        QColor band = m_uiAccent;
        band.setAlpha(45);
        painter->fillRect(QRectF(cell.x(), body.y(), z, body.height()), band);
        painter->fillRect(QRectF(body.x(), cell.y(), body.width(), z), band);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 230), 1));
        painter->drawRect(cell.adjusted(1, 1, -1, -1));
        painter->setPen(QPen(m_uiAccent, 1));
        painter->drawRect(cell);
        // Readout bar: image-pixel position + the pixel's colour (swatch + hex).
        const QColor px = m_base.pixelColor(c);
        const QRectF bar(panel.x(), panel.bottom() - 20.0, panel.width(), 20.0);
        painter->fillRect(QRectF(bar.x() + 6, bar.y() + 5, 10, 10), px);
        painter->setPen(QColor(255, 255, 255, 120));
        painter->drawRect(QRectF(bar.x() + 6, bar.y() + 5, 10, 10));
        QFont f = painter->font();
        f.setPixelSize(11);
        painter->setFont(f);
        painter->setPen(Qt::white);
        painter->drawText(bar.adjusted(22, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          QStringLiteral("%1, %2  %3")
                              .arg(c.x()).arg(c.y())
                              .arg(px.name(QColor::HexRgb).toUpper()));
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(m_uiAccent, 1));
        painter->drawRect(panel.adjusted(0, 0, -1, -1));
        painter->setRenderHint(QPainter::Antialiasing, true);
    }
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

    if (e->button() == Qt::RightButton) {
        // Don't fire the copy/confirm shortcut mid-gesture: a right press during
        // an active left-drag would commit a half-drawn annotation, because
        // renderedSelection() draws m_current while m_drawing is true.
        if (!m_drawing && m_drag == NoDrag)
            emit copyRequested();
        e->accept();
        return;
    }

    if (m_colorPicking) {
        // Sample the pixel under the cursor from the frozen screenshot.
        const QPoint p = img.toPoint();
        if (m_base.rect().contains(p))
            emit colorPicked(m_base.pixelColor(p));
        setColorPicking(false);
        e->accept();
        return;
    }

    if (m_ocrMode) {
        // Press only arms a gesture; the selection is NOT changed yet, so a
        // mis-aimed click in a gutter keeps the current pick. A drag refines to a
        // caret range (mouseMove), a bare release selects the whole line under
        // the cursor, a double-click narrows to a word.
        m_ocrPressCaret = ocrCaretAt(img);
        m_ocrDragging = true;
        m_ocrDidDrag = false;
        e->accept();
        return;
    }

    if (m_tool == Eyedropper) {
        // Click adopts the pixel under the cursor as the stroke colour (same
        // sampling the loupe/colour-pick mode use: m_base is premultiplied, so
        // pixelColor un-premultiplies rather than converting the whole image).
        const QPoint p = img.toPoint();
        if (m_base.rect().contains(p))
            setStrokeColor(m_base.pixelColor(p));
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
        a.stepSize = m_stepSize;
        a.number = ++m_stepCounter;
        m_items.append(a);
        update();
        e->accept();
        return;
    }

    if (m_tool == EditShapes) {
        // A handle of the already-selected shape → resize; otherwise hit-test
        // the shapes and (re)select, arming a possible move.
        const int h = (m_selectedAnnot >= 0) ? hitAnnotHandle(img) : -1;
        if (h >= 0) {
            m_drag = ResizeAnnot;
            m_resizeHandle = h;
            m_annotStartRect = m_items[m_selectedAnnot].rect;
            m_annotStartPoints = m_items[m_selectedAnnot].points;
            // Defer the undo snapshot to the first real move, so clicking a
            // handle without dragging leaves no empty undo entry.
            m_resizeUndoPending = true;
        } else {
            const int hit = annotAt(img);
            selectAnnot(hit);
            if (hit >= 0) {
                m_drag = PendingMoveAnnot; // promotes to MoveAnnot past a drag threshold
                m_annotStartRect = m_items[hit].rect;
                m_annotStartPoints = m_items[hit].points;
            }
        }
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
        } else {
            m_drag = NewSelection;
            m_pressHadSelection = hasSelection();
            m_selection = QRectF(img, img);
            emit selectionRectChanged();
        }
        update();
        e->accept();
        return;
    }

    if (m_tool != None) {
        // Direct shape editing without switching to the Edit tool: a handle or
        // the body of the ALREADY-selected shape starts a resize/move exactly
        // like the Edit tool would; anywhere else arms PendingDraw — a real
        // drag draws (mouseMoveEvent promotes it), a bare click selects the
        // shape under the cursor. Pen keeps drawing immediately: a pen tap is
        // a deliberate dot, and freehand must not eat the first 4px of a
        // stroke on the promotion threshold.
        if (m_selectedAnnot >= 0) {
            const int h = hitAnnotHandle(img);
            if (h >= 0) {
                m_drag = ResizeAnnot;
                m_resizeHandle = h;
                m_annotStartRect = m_items[m_selectedAnnot].rect;
                m_annotStartPoints = m_items[m_selectedAnnot].points;
                m_resizeUndoPending = true;
                update();
                e->accept();
                return;
            }
            if (annotAt(img) == m_selectedAnnot) {
                m_drag = PendingMoveAnnot;
                m_annotStartRect = m_items[m_selectedAnnot].rect;
                m_annotStartPoints = m_items[m_selectedAnnot].points;
                update();
                e->accept();
                return;
            }
        }
        // Pen and the freehand-input highlighter modes (marker + text pen)
        // accumulate points from the first press; only the rectangle highlighter
        // and the other tools arm a drag-rect.
        if (m_tool == Pen
            || (m_tool == Highlight && m_highlightMode != HlRect)) {
            beginDraw(img);
        } else {
            m_drag = PendingDraw;
        }
        update();
    }
    e->accept();
}

void AnnotationCanvas::beginDraw(const QPointF &at)
{
    m_drag = DrawDrag;
    m_drawing = true;
    m_current = {};
    m_lastDragBoundsImg = QRectF();
    m_current.type = Tool(m_tool);
    m_current.color = m_color;
    m_current.fillColor = m_fillColor;
    m_current.filled = m_fillEnabled && (m_tool == Rect || m_tool == Ellipse || m_tool == Callout);
    // Measure reuses `filled` as its sub-mode marker: size-box (1) vs distance
    // line (0). Nothing paints a fill for Measure, so the field is free.
    if (m_tool == Measure)
        m_current.filled = (m_measureMode == 1);
    m_current.width = m_strokeWidth;
    m_current.arrowHeadStyle = m_arrowHeadStyle;
    m_current.fontSize = m_fontSize;
    m_current.rect = QRectF(m_dragStart, at);
    m_penShiftAnchor = -1;
    if (m_tool == Pen || (m_tool == Highlight && m_highlightMode != HlRect))
        m_current.points = {at};
}

// Shift is a precision modifier for geometry tools: first snap the cursor to
// the small image-pixel grid, then constrain lines/arrows to 45° increments
// and rectangles/ellipses to a square. Pen does NOT use this quantizing path
// (it would make freehand ink look broken); instead the Pen draw loop treats
// Shift as a straight-segment modifier (see mouseMoveEvent DrawDrag/Pen).
static QPointF snapToGrid(const QPointF &p)
{
    constexpr qreal step = 8.0;
    return {std::round(p.x() / step) * step, std::round(p.y() / step) * step};
}

static QPointF shiftConstrainedPoint(AnnotationCanvas::Tool tool, const QPointF &origin,
                                     QPointF point)
{
    point = snapToGrid(point);
    const qreal dx = point.x() - origin.x();
    const qreal dy = point.y() - origin.y();
    if (tool == AnnotationCanvas::Line || tool == AnnotationCanvas::Arrow
        || tool == AnnotationCanvas::Measure) {
        constexpr qreal pi = 3.14159265358979323846;
        const qreal length = std::hypot(dx, dy);
        if (length < 0.001)
            return origin;
        const qreal angle = std::round(std::atan2(dy, dx) / (pi / 4.0)) * (pi / 4.0);
        return origin + QPointF(std::cos(angle) * length, std::sin(angle) * length);
    }
    if (tool == AnnotationCanvas::Rect || tool == AnnotationCanvas::Ellipse
        || tool == AnnotationCanvas::Callout || tool == AnnotationCanvas::Magnify) {
        const qreal side = qMax(std::abs(dx), std::abs(dy));
        return origin + QPointF(dx < 0 ? -side : side, dy < 0 ? -side : side);
    }
    return point;
}

// Samples the perimeter of `r` in the current base image and returns the
// average color — the "background" a smart eraser fills the region with.
QImage AnnotationCanvas::smartErasePatch(const QRect &r, bool fast) const
{
    if (m_base.isNull() || r.width() < 1 || r.height() < 1)
        return {};

    // Built at a reduced resolution and upscaled: the patch is smooth by
    // construction (it is an interpolation), so the rescale loses nothing while
    // a rect the size of a 4K screen stays cheap to rebuild on every drag move.
    const int W = qBound(2, r.width(), 192);
    const int H = qBound(2, r.height(), 192);

    // pixelColor(), not a converted copy: the base is normally
    // ARGB32_Premultiplied, and converting the whole image up front would
    // allocate and rewrite ~33 MB of a 4K capture on every drag move. Only ~11k
    // pixels are ever sampled, and pixelColor un-premultiplies each one.
    //
    // One boundary sample = the per-channel MEDIAN of a small window just
    // outside the rect (3 rows out × 5 along the edge). Median, not mean:
    // whatever is being erased usually touches (or pokes through) the rect's
    // edge, and a mean lets those pixels tint the whole fill — which is exactly
    // what made this tool read as a smudge over the object instead of a clean
    // background. `fast` samples one row instead of three during a drag.
    auto boundary = [&](int x, int y, int nx, int ny) {
        int rs[15], gs[15], bs[15];
        int n = 0;
        const int depth = fast ? 1 : 3;
        for (int d = 1; d <= depth; ++d) {
            for (int t = -2; t <= 2; ++t) {
                // t runs ALONG the edge, d outward from it.
                const int sx = qBound(0, x + nx * d + (nx ? 0 : t), m_base.width() - 1);
                const int sy = qBound(0, y + ny * d + (ny ? 0 : t), m_base.height() - 1);
                const QColor c = m_base.pixelColor(sx, sy);
                rs[n] = c.red(); gs[n] = c.green(); bs[n] = c.blue();
                ++n;
            }
        }
        const int mid = n / 2;
        std::nth_element(rs, rs + mid, rs + n);
        std::nth_element(gs, gs + mid, gs + n);
        std::nth_element(bs, bs + mid, bs + n);
        return qRgb(rs[mid], gs[mid], bs[mid]);
    };

    // The four strips. Sampling is clamped into the image, so a rect flush with
    // a screen edge falls back to its own edge pixels rather than off-image junk.
    QVector<QRgb> top(W), bottom(W), left(H), right(H);
    for (int i = 0; i < W; ++i) {
        const int x = r.left() + int((i + 0.5) * r.width() / W);
        top[i] = boundary(x, r.top(), 0, -1);
        bottom[i] = boundary(x, r.bottom(), 0, 1);
    }
    for (int j = 0; j < H; ++j) {
        const int y = r.top() + int((j + 0.5) * r.height() / H);
        left[j] = boundary(r.left(), y, -1, 0);
        right[j] = boundary(r.right(), y, 1, 0);
    }

    // Reject whatever is NOT background before interpolating anything. A stroke
    // edge that cuts through the object being erased (scrubbing over a line of
    // text is the normal case) leaves that object's colour in the strip on that
    // side, and the interpolation below would then drag it right across the
    // patch — smears and bright bands, worse than the flat fill this replaced.
    // So: model the background as the median of the whole ring, call samples
    // further than a MAD-derived band from it outliers, and rebuild those by
    // interpolating along the strip between the inliers that bracket them. The
    // ring is mostly background, so the object loses the vote.
    QVector<QRgb *> strips{top.data(), bottom.data(), left.data(), right.data()};
    QVector<int> sizes{W, W, H, H};
    auto chan = [](QRgb c, int k) { return k == 0 ? qRed(c) : k == 1 ? qGreen(c) : qBlue(c); };

    int med[3];
    {
        QVector<int> v;
        v.reserve(2 * W + 2 * H);
        for (int k = 0; k < 3; ++k) {
            v.clear();
            for (int s = 0; s < strips.size(); ++s)
                for (int i = 0; i < sizes[s]; ++i)
                    v.append(chan(strips[s][i], k));
            std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
            med[k] = v[v.size() / 2];
        }
    }
    auto deviation = [&](QRgb c) {
        return qMax(qMax(qAbs(qRed(c) - med[0]), qAbs(qGreen(c) - med[1])),
                    qAbs(qBlue(c) - med[2]));
    };
    double thr;
    {
        QVector<int> devs;
        devs.reserve(2 * W + 2 * H);
        for (int s = 0; s < strips.size(); ++s)
            for (int i = 0; i < sizes[s]; ++i)
                devs.append(deviation(strips[s][i]));
        std::nth_element(devs.begin(), devs.begin() + devs.size() / 2, devs.end());
        // 1.4826·MAD ≈ σ for normal noise; ×3 keeps ordinary texture and the
        // spread of a gradient, and the floor keeps a perfectly flat ring from
        // making every faint variation an outlier.
        thr = qMax(24.0, 3.0 * 1.4826 * devs[devs.size() / 2]);
    }
    for (int s = 0; s < strips.size(); ++s) {
        QRgb *a = strips[s];
        const int n = sizes[s];
        QVector<bool> ok(n);
        int inliers = 0;
        for (int i = 0; i < n; ++i) {
            ok[i] = deviation(a[i]) <= thr;
            inliers += ok[i] ? 1 : 0;
        }
        if (inliers == 0) {
            // The whole side sits on the object (a stroke ending inside a big
            // dark block): nothing to interpolate from, so take the ring's
            // background estimate.
            for (int i = 0; i < n; ++i)
                a[i] = qRgb(med[0], med[1], med[2]);
            continue;
        }
        int i = 0;
        while (i < n) {
            if (ok[i]) { ++i; continue; }
            int end = i;
            while (end < n && !ok[end])
                ++end;
            const int lo = i - 1;          // last inlier before the gap, or -1
            const int hi = end < n ? end : -1;   // first inlier after it, or -1
            for (int g = i; g < end; ++g) {
                if (lo < 0) a[g] = a[hi];
                else if (hi < 0) a[g] = a[lo];
                else {
                    const double t = double(g - lo) / (hi - lo);
                    a[g] = qRgb(int(qRound((1 - t) * qRed(a[lo]) + t * qRed(a[hi]))),
                                int(qRound((1 - t) * qGreen(a[lo]) + t * qGreen(a[hi]))),
                                int(qRound((1 - t) * qBlue(a[lo]) + t * qBlue(a[hi]))));
                }
            }
            i = end;
        }
    }

    // Coons patch: interpolate the two opposite strips, then subtract the
    // bilinear surface through the corners so the two agree at the border. On a
    // flat background every sample is the same colour and the result is exactly
    // that colour; on a gradient it follows the gradient, which one averaged
    // fill colour could never do.
    auto corner = [](QRgb a, QRgb b) {
        return qRgb((qRed(a) + qRed(b)) / 2, (qGreen(a) + qGreen(b)) / 2,
                    (qBlue(a) + qBlue(b)) / 2);
    };
    const QRgb c00 = corner(top[0], left[0]);
    const QRgb c10 = corner(top[W - 1], right[0]);
    const QRgb c01 = corner(bottom[0], left[H - 1]);
    const QRgb c11 = corner(bottom[W - 1], right[H - 1]);

    QImage patch(W, H, QImage::Format_RGB32);
    for (int j = 0; j < H; ++j) {
        const double v = (j + 0.5) / H;
        QRgb *line = reinterpret_cast<QRgb *>(patch.scanLine(j));
        for (int i = 0; i < W; ++i) {
            const double u = (i + 0.5) / W;
            int ch[3];
            for (int k = 0; k < 3; ++k) {
                const auto get = [k](QRgb c) {
                    return k == 0 ? qRed(c) : k == 1 ? qGreen(c) : qBlue(c);
                };
                const double lr = (1 - u) * get(left[j]) + u * get(right[j]);
                const double tb = (1 - v) * get(top[i]) + v * get(bottom[i]);
                const double bilinear = (1 - u) * (1 - v) * get(c00) + u * (1 - v) * get(c10)
                                        + (1 - u) * v * get(c01) + u * v * get(c11);
                // Coons overshoots when the two rulings disagree — lr + tb can
                // land past white (a bright band) or below black. Clamping to
                // the four samples this pixel actually interpolates keeps the
                // patch inside the colours that surround it; the boundary is
                // unaffected, since there the formula already equals its strip.
                const int lo = qMin(qMin(get(left[j]), get(right[j])),
                                    qMin(get(top[i]), get(bottom[i])));
                const int hi = qMax(qMax(get(left[j]), get(right[j])),
                                    qMax(get(top[i]), get(bottom[i])));
                ch[k] = qBound(lo, int(qRound(lr + tb - bilinear)), hi);
            }
            line[i] = qRgb(ch[0], ch[1], ch[2]);
        }
    }
    if (patch.size() == r.size())
        return patch;
    return patch.scaled(r.size(), Qt::IgnoreAspectRatio,
                        fast ? Qt::FastTransformation : Qt::SmoothTransformation);
}

void AnnotationCanvas::mouseMoveEvent(QMouseEvent *e)
{
    // The loupe follows drags too (hover events stop during a grab) — the
    // whole point is seeing the pixel a selection EDGE lands on.
    const QRectF loupeBefore = pixelLoupeRect();
    m_hoverPoint = e->position();
    m_hoverInside = true;
    emit hoverPointChanged();
    updateLoupeRegion(loupeBefore);
    const QPointF img = toImage(e->position().x(), e->position().y());

    if (m_ocrMode && m_ocrDragging) {
        // Only a real drag (past a threshold) starts a caret range; a small
        // jitter on a click stays a click (→ whole-line select on release).
        const int c = ocrCaretAt(img);
        if (!m_ocrDidDrag) {
            const qreal thr = 5.0 / qMax(0.05, renderScale());
            if (QLineF(m_dragStart, img).length() < thr) { e->accept(); return; }
            m_ocrDidDrag = true;
            // A fresh range spanning the press caret to the current caret (so a
            // single fast flick already selects). FULL repaint — not a partial
            // updateImgRect — so a prior selection's bars, which may be anywhere
            // on screen, are cleared; and RETURN, because a partial rect in this
            // same event would clobber the full repaint (QQuickPaintedItem
            // coalesces a null dirty-rect |= rect down to just rect) and re-ghost
            // the old bars. Later moves extend caretB incrementally.
            m_ocrCaretA = m_ocrPressCaret;
            m_ocrCaretB = c;
            emit ocrChanged();
            update();
            e->accept();
            return;
        }
        if (c >= 0 && c != m_ocrCaretB) {
            const int prev = m_ocrCaretB;
            m_ocrCaretB = c;
            emit ocrChanged();
            // Only the lines whose selection bars changed need repainting — not
            // the whole scrim + base resample.
            updateImgRect(ocrCaretSpanDirty(prev, c));
        }
        e->accept();
        return;
    }

    switch (m_drag) {
    case DrawDrag: {
        QRectF nowB;
        if (isFreehandStroke(m_current)) {
            const qreal pad = m_current.width / 2.0 + 4.0;
            if ((e->modifiers() & Qt::ShiftModifier) && !m_current.points.isEmpty()) {
                // Hold Shift mid-stroke to lay a STRAIGHT segment from the last
                // committed point to the cursor (any angle). Releasing Shift
                // resumes freehand from that endpoint — chained straight +
                // freehand segments. A single live tail point is retained and
                // moved with the cursor; it is committed as an anchor on
                // release of Shift (below).
                if (m_penShiftAnchor < 0) {
                    m_penShiftAnchor = m_current.points.size() - 1;
                    m_current.points.append(img); // live straight-segment tail
                }
                const QPointF anchor = m_current.points[m_penShiftAnchor];
                m_current.points.last() = img;
                nowB = QRectF(anchor, img).normalized().adjusted(-pad, -pad, pad, pad);
            } else {
                // Shift just released mid-stroke: the straight endpoint becomes
                // a permanent anchor and freehand continues from it.
                m_penShiftAnchor = -1;
                // Retain enough detail for a smooth path while coalescing
                // sub-pixel duplicate samples from high-frequency mice/tablets.
                const qreal minDistance = qMax(0.75, m_current.width / 3.0);
                const QPointF prev = m_current.points.isEmpty()
                                         ? img : m_current.points.constLast();
                if (m_current.points.isEmpty()
                    || QLineF(m_current.points.constLast(), img).length() >= minDistance)
                    m_current.points.append(img);
                // Only the newest segment changed — the rest of the stroke is
                // already in the texture. Dirty just that segment; annotBoundsImg
                // would union ALL points, degrading to a near-full-canvas repaint
                // as a long freehand stroke grows.
                nowB = QRectF(prev, m_current.points.constLast()).normalized()
                           .adjusted(-pad, -pad, pad, pad);
            }
        } else {
            const QPointF endpoint = (e->modifiers() & Qt::ShiftModifier)
                                     ? shiftConstrainedPoint(m_current.type, m_dragStart, img) : img;
            m_current.rect.setBottomRight(endpoint);
            // Fast patch while sizing (see drawAnnot Blur / SmartErase); the
            // smooth one is rebuilt once the drag ends.
            if (m_current.type == Blur || m_current.type == SmartErase)
                m_fxFast = true;
            nowB = annotBoundsImg(m_current);
        }
        // Repaint only the union of the changed bounds and last frame's — a
        // bare update() re-rasterized the whole 4K base every mouse-move.
        const QRectF dirtyImg = nowB.united(m_lastDragBoundsImg);
        m_lastDragBoundsImg = nowB;
        const qreal s = renderScale();
        update(QRectF(dirtyImg.x() * s, dirtyImg.y() * s,
                      dirtyImg.width() * s, dirtyImg.height() * s)
                   .toAlignedRect().adjusted(-2, -2, 2, 2));
        break;
    }
    case PendingDraw: {
        // A genuine drag starts drawing; until then this may still become a
        // click-select on release. Any prior selection is dropped the moment
        // drawing starts — its handles must not hover over a fresh shape,
        // and style edits must go back to the tool defaults.
        const qreal threshold = 4.0 / qMax(0.05, renderScale());
        if (QLineF(m_dragStart, img).length() >= threshold) {
            if (m_selectedAnnot >= 0)
                selectAnnot(-1);
            const QPointF endpoint = (e->modifiers() & Qt::ShiftModifier)
                                     ? shiftConstrainedPoint(Tool(m_tool), m_dragStart, img) : img;
            beginDraw(endpoint);
            update();
        }
        break;
    }
    case NewSelection: {
        const QRectF oldSel = m_selection;
        const QPointF endpoint = (e->modifiers() & Qt::ShiftModifier)
                                 ? shiftConstrainedPoint(Rect, m_dragStart, img) : img;
        m_selection = QRectF(m_dragStart, endpoint).normalized();
        normalizeSelection();
        emit selectionRectChanged();
        // Only the scrim ring between the old and new selection edges changes.
        updateImgRect(oldSel.united(m_selection));
        break;
    }
    case MoveSelection: {
        const QRectF oldSel = m_selection;
        QRectF r = m_selStart.translated(img - m_dragStart);
        r.moveLeft(qBound(0.0, r.left(), qreal(m_base.width()) - r.width()));
        r.moveTop(qBound(0.0, r.top(), qreal(m_base.height()) - r.height()));
        m_selection = r;
        emit selectionRectChanged();
        updateImgRect(oldSel.united(m_selection));
        break;
    }
    case ResizeSelection: {
        const QRectF oldSel = m_selection;
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
        updateImgRect(oldSel.united(m_selection));
        break;
    }
    case PendingMoveAnnot: {
        // A real drag promotes select-then-hold into a move; a bare click just
        // selected (undo entry pushed here so the whole drag is one entry).
        const qreal threshold = 4.0 / qMax(0.05, renderScale());
        if (QLineF(m_dragStart, img).length() >= threshold) {
            m_drag = MoveAnnot;
            pushUndo();
        } else {
            break;
        }
    }
    Q_FALLTHROUGH();
    case MoveAnnot: {
        if (m_selectedAnnot < 0) break;
        Annot &a = m_items[m_selectedAnnot];
        if (a.type == Blur) m_fxFast = true; // fast preview while moving (drawAnnot)
        const QRectF oldB = annotBoundsImg(a);
        const QPointF d = img - m_dragStart;
        a.rect = m_annotStartRect.translated(d);
        a.points = m_annotStartPoints;
        for (QPointF &p : a.points)
            p += d;
        updateImgRect(oldB.united(annotBoundsImg(a)));
        break;
    }
    case ResizeAnnot: {
        if (m_selectedAnnot < 0) break;
        if (m_resizeUndoPending) { pushUndo(); m_resizeUndoPending = false; }
        Annot &a = m_items[m_selectedAnnot];
        if (a.type == Blur) m_fxFast = true; // fast preview while resizing (drawAnnot)
        const QRectF oldB = annotBoundsImg(a);
        QRectF r = m_annotStartRect;
        const QPointF d = img - m_dragStart;
        if (a.type == Line || a.type == Arrow || a.type == Measure) {
            // 2 endpoint handles map to the raw rect corners (p1→p2), so a
            // dragged endpoint follows the cursor without normalization.
            if (m_resizeHandle == 0) r.setTopLeft(r.topLeft() + d);
            else r.setBottomRight(r.bottomRight() + d);
            a.rect = r;
        } else {
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
            a.rect = r; // normalized on release
        }
        updateImgRect(oldB.united(annotBoundsImg(a)));
        break;
    }
    default:
        break;
    }
    e->accept();
}

void AnnotationCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_ocrMode && m_ocrDragging) {
        m_ocrDragging = false;
        const QPointF img = toImage(e->position().x(), e->position().y());
        // A bare click (no letter-drag) selects the WHOLE LINE under the cursor.
        // A click away from any text KEEPS the current selection (a mis-aimed
        // click in a gap must not wipe a careful pick); Esc exits OCR mode.
        if (!m_ocrDidDrag) {
            const int k = ocrLineAt(img);
            if (ocrOverLine(img, k)) {
                m_ocrCaretA = m_ocrLines[k].first;
                m_ocrCaretB = m_ocrLines[k].last + 1;
            }
        }
        emit ocrChanged();
        update();
        e->accept();
        return;
    }

    // A drawing-tool press that never moved: a plain CLICK — select (or
    // deselect, on empty space) the shape under the cursor, Edit-tool style.
    if (m_drag == PendingDraw) {
        selectAnnot(annotAt(toImage(e->position().x(), e->position().y())));
        update();
    }
    if (m_drag == DrawDrag && m_drawing) {
        // Preserve the stroke endpoint even when it falls within the sampling
        // threshold used while the pointer is moving.
        if (isFreehandStroke(m_current) && !m_current.points.isEmpty()) {
            const QPointF releasePoint = toImage(e->position().x(), e->position().y());
            if (QLineF(m_current.points.constLast(), releasePoint).length() > 0.0)
                m_current.points.append(releasePoint);
        }
        m_drawing = false;
        const bool tiny = !isFreehandStroke(m_current) &&
                          QLineF(m_current.rect.topLeft(), m_current.rect.bottomRight()).length() < 3;
        if (!tiny) {
            // Text-mode Highlight is a pen: snap the stroke to the per-line text
            // boxes it crossed (applyTextAwareHighlight appends its own annots +
            // one undo). Over a picture, or with no glyph boxes cached, it fails
            // and the freehand marker stroke is kept instead. Rect mode never
            // snaps; the freehand marker mode always appends its path.
            if (m_current.type == Magnify) {
                // The drag picked the SOURCE; the placed annotation is the
                // loupe — a 2x copy centred on the source, clamped into the
                // image. Resizing the loupe afterwards changes the
                // magnification (dest/source ratio); moving it leaves the
                // source anchored.
                const QRectF src = m_current.rect.normalized()
                        .intersected(QRectF(QPointF(0, 0), QSizeF(m_base.size())));
                if (src.width() >= 4 && src.height() >= 4) {
                    m_current.srcRect = src;
                    QRectF dest(0, 0,
                                qMin(src.width() * 2.0, qreal(m_base.width())),
                                qMin(src.height() * 2.0, qreal(m_base.height())));
                    dest.moveCenter(src.center());
                    dest.moveLeft(qBound(0.0, dest.left(),
                                         qreal(m_base.width()) - dest.width()));
                    dest.moveTop(qBound(0.0, dest.top(),
                                        qreal(m_base.height()) - dest.height()));
                    m_current.rect = dest;
                    pushUndo();
                    m_items.append(m_current);
                }
            } else if (m_current.type == Highlight && m_highlightMode == HlText
                && applyTextAwareHighlight(m_current)) {
                // snapped to text — nothing else to append
            } else {
                pushUndo();
                m_items.append(m_current);
            }
        }
        update();
    }
    // EditShapes: a resize handle leaves the rect un-normalized (negative
    // width/height if dragged past the opposite edge). Line/Arrow keep raw
    // corners (they encode p1→p2); everything else normalizes on release.
    if (m_drag == ResizeAnnot && m_selectedAnnot >= 0) {
        Annot &a = m_items[m_selectedAnnot];
        if (a.type != Line && a.type != Arrow && a.type != Measure)
            a.rect = a.rect.normalized();
        update();
    }
    // A Blur preview was rendered with fast (low-quality) scaling while its
    // geometry changed; the gesture ended, so clear the flag and repaint once so
    // the cache (keyed partly on m_fxFast) rebuilds the smoothed patch. update()
    // is deferred, so the coalesced paint runs with m_fxFast already false.
    if (m_fxFast) { m_fxFast = false; update(); }
    // A bare click (press+release without a real drag) on an EMPTY overlay
    // SELECTS the whole screen: promote the point "selection" to the full
    // image. Only when nothing was selected before the press — a click that
    // dismissed an existing rect must not resurrect it as a full-screen one.
    // With capture-on-release ON the user wants release to capture, click
    // included — confirm immediately; otherwise the full-screen selection
    // stays up for the normal annotate/confirm flow.
    if (m_clickSelectsAll && m_selectionMode && m_tool == None
        && m_drag == NewSelection && !hasSelection() && !m_pressHadSelection) {
        m_drag = NoDrag;
        m_resizeHandle = -1;
        selectAll();
        e->accept();
        if (m_confirmOnRelease)
            emit selectionConfirmed();
        return;
    }
    // Capture-on-release: only a gesture that PRODUCED the selection confirms
    // (manual drag = NewSelection). Move/resize of an existing selection and
    // empty clicks (hasSelection false) fall through to the normal confirm paths.
    if (m_confirmOnRelease && m_selectionMode && m_tool == None
        && m_drag == NewSelection
        && hasSelection()) {
        m_drag = NoDrag;
        m_resizeHandle = -1;
        e->accept();
        emit selectionConfirmed();
        return;
    }
    m_drag = NoDrag;
    m_resizeHandle = -1;
    m_resizeUndoPending = false;
    e->accept();
}

void AnnotationCanvas::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_ocrMode) {
        // Narrow the (line) selection to the WORD under the cursor. Qt delivers
        // Press,Release,DblClick,Release: the first Release selected the line;
        // this sets the word absolutely; the trailing Release is a no-op
        // (m_ocrDragging is false).
        const QPointF img = toImage(e->position().x(), e->position().y());
        const int k = ocrLineAt(img);
        const int g = ocrOverLine(img, k) ? ocrGlyphAtOnLine(k, img.x()) : -1;
        if (g >= 0) {
            int lo, hi;
            ocrExpandWord(g, lo, hi);
            m_ocrCaretA = lo;
            m_ocrCaretB = hi + 1;
            m_ocrDragging = false;
            m_ocrDidDrag = false;
            emit ocrChanged();
            update();
        }
        e->accept();
        return;
    }
    if (m_tool == EditShapes) {
        // Double-click a Text annotation to re-edit its string. The preceding
        // press already selected the shape under the cursor.
        const QPointF img = toImage(e->position().x(), e->position().y());
        int idx = annotAt(img);
        if (idx < 0) idx = m_selectedAnnot;
        if (idx >= 0 && idx < m_items.size() && m_items[idx].type == Text) {
            selectAnnot(idx);
            m_drag = NoDrag; // the pending move from the double-click's press
            const Annot &a = m_items[idx];
            emit textEditRequested(a.rect.left(), a.rect.top(), a.text);
        }
        e->accept();
        return;
    }
    if (m_selectionMode && hasSelection())
        emit selectionConfirmed();
    e->accept();
}

// ---------------------------------------------------------------- pixel loupe

void AnnotationCanvas::setPixelLoupe(bool on)
{
    if (m_pixelLoupe == on)
        return;
    m_pixelLoupe = on;
    emit pixelLoupeChanged();
    update();
}

void AnnotationCanvas::setPixelLoupeZoom(int z)
{
    z = qBound(4, z - (z & 1), 16);   // even factors only: crisp cell edges
    if (m_pixelLoupeZoom == z)
        return;
    m_pixelLoupeZoom = z;
    emit pixelLoupeZoomChanged();
    update();
}

bool AnnotationCanvas::loupeActive() const
{
    // Region picking only: with a drawing tool armed (or in the editor, which
    // never enables the loupe) the panel would be pure noise next to the
    // brush. m_hoverInside keeps it off the monitors the pointer is not on.
    return m_pixelLoupe && m_selectionMode && m_tool == None && !m_ocrMode
           && m_hoverInside && !m_base.isNull();
}

// Source pixels per loupe axis — an odd count so ONE cell is the exact hovered
// pixel; sized so the panel body stays ~130 item px across every zoom.
int AnnotationCanvas::loupeGridCells() const
{
    int n = 132 / qMax(1, m_pixelLoupeZoom);
    if ((n & 1) == 0)
        ++n;
    return qMax(5, n);
}

QRectF AnnotationCanvas::pixelLoupeRect() const
{
    if (!loupeActive())
        return {};
    const qreal body = qreal(loupeGridCells() * m_pixelLoupeZoom);
    const qreal barH = 20.0;
    const qreal w = body + 2.0;          // 1 px border each side
    const qreal h = body + barH + 2.0;
    // Offset to the bottom-right of the cursor; flip per-axis near the item
    // edges so the panel never covers the pixels being aimed at.
    const qreal off = 24.0;
    qreal x = m_hoverPoint.x() + off;
    qreal y = m_hoverPoint.y() + off;
    if (x + w > width())
        x = m_hoverPoint.x() - off - w;
    if (y + h > height())
        y = m_hoverPoint.y() - off - h;
    x = qBound(0.0, x, qMax(0.0, width() - w));
    y = qBound(0.0, y, qMax(0.0, height() - h));
    return QRectF(x, y, w, h);
}

// Repaint the union of the loupe's previous and current panel — called from
// the pointer handlers, which are the only thing that moves it.
void AnnotationCanvas::updateLoupeRegion(const QRectF &before)
{
    const QRectF now = pixelLoupeRect();
    if (before.isEmpty() && now.isEmpty())
        return;
    update((before | now).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void AnnotationCanvas::wheelEvent(QWheelEvent *e)
{
    // Ctrl+scroll on the region overlay changes the loupe magnification.
    if (loupeActive() && (e->modifiers() & Qt::ControlModifier)
        && e->angleDelta().y() != 0) {
        const QRectF before = pixelLoupeRect();
        setPixelLoupeZoom(m_pixelLoupeZoom + (e->angleDelta().y() > 0 ? 2 : -2));
        updateLoupeRegion(before);
        e->accept();
        return;
    }
    e->ignore();
}

void AnnotationCanvas::hoverLeaveEvent(QHoverEvent *e)
{
    // Pointer moved to another monitor's overlay (or off the item): drop the
    // loupe here — the sibling canvas under the pointer shows its own.
    if (m_hoverInside) {
        const QRectF before = pixelLoupeRect();
        m_hoverInside = false;
        if (!before.isEmpty())
            update(before.toAlignedRect().adjusted(-2, -2, 2, 2));
    }
    QQuickPaintedItem::hoverLeaveEvent(e);
}

void AnnotationCanvas::hoverMoveEvent(QHoverEvent *e)
{
    const QRectF loupeBefore = pixelLoupeRect();
    m_hoverPoint = e->position();
    m_hoverInside = true;
    emit hoverPointChanged();
    updateLoupeRegion(loupeBefore);
    const QPointF img = toImage(e->position().x(), e->position().y());
    if (m_colorPicking) {
        setCursor(Qt::CrossCursor);
        e->accept();
        return;
    }
    if (m_ocrMode) {
        // Brighten the line under the pointer and show an I-beam only over text;
        // a plain arrow over blank pixels tells the user where text is.
        const int k = ocrLineAt(img);
        const bool over = ocrOverLine(img, k);
        const int hv = over ? k : -1;
        if (hv != m_ocrHoverLine) {
            const int prev = m_ocrHoverLine;
            m_ocrHoverLine = hv;
            // Only the old/new hovered line's box changed — repaint just those
            // instead of the whole scrim + base resample.
            QRectF d;
            if (prev >= 0 && prev < m_ocrLines.size())
                d |= m_ocrLines[prev].band.adjusted(-3, -3, 3, 3);
            if (hv >= 0 && hv < m_ocrLines.size())
                d |= m_ocrLines[hv].band.adjusted(-3, -3, 3, 3);
            updateImgRect(d);
        }
        setCursor(over ? Qt::IBeamCursor : Qt::ArrowCursor);
        e->accept();
        return;
    }
    if (m_tool == EditShapes) {
        setCursor(annotEditCursor(img));
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
    } else if (m_selectedAnnot >= 0
               && (hitAnnotHandle(img) >= 0 || annotAt(img) == m_selectedAnnot)) {
        // Drawing-tool direct-manip: the selected shape can be resized/moved in
        // place (see mousePressEvent), so its handles/body get the same cursors
        // the Edit tool shows; empty space keeps the draw crosshair below.
        setCursor(annotEditCursor(img));
    } else {
        setCursor(Qt::CrossCursor);
    }
    e->accept();
}

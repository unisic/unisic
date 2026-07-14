#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QColor>
#include <QFont>
#include <QVector>
#include <QRect>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <qqmlregistration.h>
#include "ocr/OcrWord.h"

template <typename T> class QFutureWatcher;

// The single drawing surface used by both the region-selection overlay and
// the post-capture editor. Holds the base image plus a list of vector
// annotations; painting, hit-testing, undo/redo and final compositing all
// happen here in image-pixel space, so what is exported is exactly what is
// on screen.
class AnnotationCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor strokeColor READ strokeColor WRITE setStrokeColor NOTIFY strokeColorChanged)
    // Theme colours for the selection chrome (border, handles, dim-outside
    // scrim). Bound to Theme.accent / Theme.primary so the overlay follows
    // the SELECTED app theme instead of a fixed purple.
    Q_PROPERTY(QColor uiAccent READ uiAccent WRITE setUiAccent NOTIFY uiChromeChanged)
    Q_PROPERTY(QColor uiScrim READ uiScrim WRITE setUiScrim NOTIFY uiChromeChanged)
    Q_PROPERTY(bool colorPicking READ colorPicking WRITE setColorPicking NOTIFY colorPickingChanged)
    // True while the current color came from the automatic highlighter
    // red<->yellow swap (not a user pick) — consumers must not persist it.
    Q_PROPERTY(bool strokeColorIsAuto READ strokeColorIsAuto NOTIFY strokeColorChanged)
    // "shapeFill*" — QQuickPaintedItem already has a fillColor property (the
    // item background); reusing that name shadows it and breaks both.
    Q_PROPERTY(QColor shapeFillColor READ shapeFillColor WRITE setShapeFillColor NOTIFY shapeFillColorChanged)
    Q_PROPERTY(bool shapeFillEnabled READ shapeFillEnabled WRITE setShapeFillEnabled NOTIFY shapeFillEnabledChanged)
    Q_PROPERTY(int strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY strokeWidthChanged)
    Q_PROPERTY(int arrowHeadStyle READ arrowHeadStyle WRITE setArrowHeadStyle NOTIFY arrowHeadStyleChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    // Step-marker badge size (px, decoupled from the text fontSize — the
    // circle radius is max(14, stepSize*0.9)).
    Q_PROPERTY(int stepSize READ stepSize WRITE setStepSize NOTIFY stepSizeChanged)
    // Highlighter sub-mode (HighlightMode): freehand marker / rectangle band /
    // OCR text-snap. The Highlight tool paints and gestures differently per mode.
    Q_PROPERTY(int highlightMode READ highlightMode WRITE setHighlightMode NOTIFY highlightModeChanged)
    // Text styling for NEW text annotations (empty family = default UI font).
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(bool fontBold READ fontBold WRITE setFontBold NOTIFY fontBoldChanged)
    Q_PROPERTY(bool fontItalic READ fontItalic WRITE setFontItalic NOTIFY fontItalicChanged)
    Q_PROPERTY(bool fontUnderline READ fontUnderline WRITE setFontUnderline NOTIFY fontUnderlineChanged)
    Q_PROPERTY(bool textOutline READ textOutline WRITE setTextOutline NOTIFY textOutlineChanged)
    Q_PROPERTY(QColor textOutlineColor READ textOutlineColor WRITE setTextOutlineColor NOTIFY textOutlineColorChanged)
    Q_PROPERTY(bool textBackground READ textBackground WRITE setTextBackground NOTIFY textBackgroundChanged)
    Q_PROPERTY(QColor textBackgroundColor READ textBackgroundColor WRITE setTextBackgroundColor NOTIFY textBackgroundColorChanged)
    Q_PROPERTY(bool selectionMode READ selectionMode WRITE setSelectionMode NOTIFY selectionModeChanged)
    // Overlay capture-on-release: finishing a selection gesture (drag) with the
    // plain selection tool confirms immediately — no Enter/double-click.
    // Annotation drags never confirm.
    Q_PROPERTY(bool confirmOnRelease READ confirmOnRelease WRITE setConfirmOnRelease NOTIFY confirmOnReleaseChanged)
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY selectionRectChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionRectChanged)
    // Latest pointer position in ITEM coordinates, updated on hover AND while
    // dragging (a QML HoverHandler stops firing during a button grab). Drives
    // the overlay's selection guides so they track the cursor mid-drag.
    Q_PROPERTY(QPointF hoverPoint READ hoverPoint NOTIFY hoverPointChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageChanged)
    Q_PROPERTY(qreal renderScale READ renderScale NOTIFY renderScaleChanged)
    // EditShapes tool: index into m_items of the currently selected annotation
    // (-1 = none). selectedAnnotTool is that annotation's Tool enum (or -1) so
    // the QML props bar can show the right controls; hasAnnotSelection gates
    // the settings write-back guard (restyling a shape must not overwrite the
    // saved "next shape" defaults).
    Q_PROPERTY(bool hasAnnotSelection READ hasAnnotSelection NOTIFY selectedAnnotChanged)
    Q_PROPERTY(int selectedAnnotTool READ selectedAnnotTool NOTIFY selectedAnnotChanged)
    // OCR text-pick mode (editor): recognized words are highlighted and the
    // user clicks / rubber-bands to select them, then copies. ocrBusy is true
    // while recognition runs; hasOcrSelection gates the "Copy selection" button.
    Q_PROPERTY(bool ocrMode READ ocrMode WRITE setOcrMode NOTIFY ocrChanged)
    Q_PROPERTY(bool ocrBusy READ ocrBusy NOTIFY ocrChanged)
    Q_PROPERTY(bool hasOcrSelection READ hasOcrSelection NOTIFY ocrChanged)

public:
    enum Tool {
        None = 0, Pen, Line, Arrow, Rect, Ellipse, Text,
        Blur, Pixelate, Highlight, Step, Crop,
        SmartErase,
        ObjectPick,   // 13 — reserved (removed: was smart-pick overlay tool)
        EditShapes,  // 14 — select a placed annotation to move/resize/restyle/delete it
        PastedImage, // 15 — editor-only image inserted from the system clipboard
        Callout,     // 16 — speech-bubble shape; text is added with the text tool
        Measure      // 17 — distance/angle ruler retained in the export
    };
    Q_ENUM(Tool)

    // Highlighter sub-mode (Highlight tool). Freehand = translucent marker
    // stroke (follows the pen), Rect = drawn rectangular band, Text = snaps to
    // the OCR text lines the drag crosses. Persisted via Settings/highlightMode.
    enum HighlightMode { HlFreehand = 0, HlRect = 1, HlText = 2 };
    Q_ENUM(HighlightMode)

    explicit AnnotationCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    void setImage(const QImage &img);
    QImage image() const { return m_base; }

    int tool() const { return m_tool; }
    void setTool(int t);
    QColor strokeColor() const { return m_color; }
    bool strokeColorIsAuto() const { return m_strokeAuto; }
    void setStrokeColor(const QColor &c);
    QColor uiAccent() const { return m_uiAccent; }
    void setUiAccent(const QColor &c) { if (m_uiAccent == c) return; m_uiAccent = c; emit uiChromeChanged(); update(); }
    QColor uiScrim() const { return m_uiScrim; }
    void setUiScrim(const QColor &c) { if (m_uiScrim == c) return; m_uiScrim = c; emit uiChromeChanged(); update(); }
    // Screen colour-pick mode: the next click samples the pixel under the
    // cursor from the frozen base image and emits colorPicked, instead of
    // drawing or selecting. Enabled by the colour popup's eyedropper.
    bool colorPicking() const { return m_colorPicking; }
    void setColorPicking(bool on);
    QColor shapeFillColor() const { return m_fillColor; }
    void setShapeFillColor(const QColor &c);
    bool shapeFillEnabled() const { return m_fillEnabled; }
    void setShapeFillEnabled(bool on);
    int strokeWidth() const { return m_strokeWidth; }
    void setStrokeWidth(int w);
    int arrowHeadStyle() const { return m_arrowHeadStyle; }
    void setArrowHeadStyle(int style);
    int highlightMode() const { return m_highlightMode; }
    void setHighlightMode(int mode);
    int fontSize() const { return m_fontSize; }
    void setFontSize(int s);
    int stepSize() const { return m_stepSize; }
    void setStepSize(int s);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &f);
    bool fontBold() const { return m_fontBold; }
    void setFontBold(bool on);
    bool fontItalic() const { return m_fontItalic; }
    void setFontItalic(bool on);
    bool fontUnderline() const { return m_fontUnderline; }
    void setFontUnderline(bool on);
    bool textOutline() const { return m_textOutline; }
    void setTextOutline(bool on);
    QColor textOutlineColor() const { return m_textOutlineColor; }
    void setTextOutlineColor(const QColor &c);
    bool textBackground() const { return m_textBackground; }
    void setTextBackground(bool on);
    QColor textBackgroundColor() const { return m_textBgColor; }
    void setTextBackgroundColor(const QColor &c);
    bool selectionMode() const { return m_selectionMode; }
    bool confirmOnRelease() const { return m_confirmOnRelease; }
    void setConfirmOnRelease(bool on)
    {
        if (m_confirmOnRelease == on) return;
        m_confirmOnRelease = on;
        emit confirmOnReleaseChanged();
    }
    void setSelectionMode(bool on);
    QRectF selectionRect() const { return m_selection; }
    bool hasSelection() const { return m_selection.width() > 2 && m_selection.height() > 2; }
    QPointF hoverPoint() const { return m_hoverPoint; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QSize imageSize() const { return m_base.size(); }
    qreal renderScale() const;
    bool hasAnnotSelection() const { return m_selectedAnnot >= 0; }
    int selectedAnnotTool() const {
        return (m_selectedAnnot >= 0 && m_selectedAnnot < m_items.size())
                   ? int(m_items[m_selectedAnnot].type) : -1;
    }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void commitText(qreal imgX, qreal imgY, const QString &text);
    // Insert the current GUI clipboard at an image-space point. Images retain
    // their pixels as an annotation; plain text uses the existing Text path.
    Q_INVOKABLE bool pasteClipboard(qreal imgX, qreal imgY);
    // EditShapes: delete the selected annotation, nudge it by whole image
    // pixels, or replace a selected Text annotation's string (double-click
    // re-edit). No-ops when nothing / a non-Text shape is selected.
    Q_INVOKABLE void removeSelectedAnnot();
    Q_INVOKABLE void nudgeSelectedAnnot(qreal dx, qreal dy);
    Q_INVOKABLE void clearAnnotSelection();
    Q_INVOKABLE void commitTextEdit(const QString &text);
    // Select the topmost annotation at the given image-space point (-1 → none).
    Q_INVOKABLE void selectAnnotAt(qreal imgX, qreal imgY);
    Q_INVOKABLE int annotCount() const { return int(m_items.size()); }

    // OCR text-pick mode.
    bool ocrMode() const { return m_ocrMode; }
    void setOcrMode(bool on);
    // Bumped whenever OCR state is invalidated (mode toggled, base replaced).
    // The async OCR runner captures this before recognizing and drops its
    // result if the value changed meanwhile (a dismissed or superseded pick).
    quint64 ocrSeq() const { return m_ocrSeq; }
    bool ocrBusy() const { return m_ocrBusy; }
    void setOcrBusy(bool on);
    bool hasOcrSelection() const;
    // Called from the OCR callback with the recognized words (image px).
    void setOcrWords(const QVector<OcrWord> &words);
    Q_INVOKABLE void ocrSelectAll();
    Q_INVOKABLE void clearOcrMode();
    // Selected words joined in reading order (spaces within a line, newlines
    // between lines).
    Q_INVOKABLE QString ocrSelectedText() const;
    // Turn the transient OCR range into permanent, exportable annotations.
    // A multi-line range becomes one annotation per line and one undo step.
    Q_INVOKABLE bool highlightOcrSelection();
    Q_INVOKABLE bool redactOcrSelection(bool pixelate);
    // Text-aware Highlight: the editor recognizes glyph boxes and feeds them
    // here so a plain Highlight drag over text snaps to the text line(s).
    // Requested lazily via glyphBoxesRequested() when the Highlight tool is
    // picked; the overlay (no OCR) simply never delivers and stays plain.
    void setGlyphBoxes(const QVector<OcrWord> &words);
    Q_INVOKABLE void nudgeSelection(qreal dx, qreal dy);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void applyCrop();
    Q_INVOKABLE QPointF toImage(qreal itemX, qreal itemY) const;

    // Final composite at full image resolution (annotations burnt in).
    QImage rendered() const;
    // rendered() cropped to the selection (overlay flow).
    QImage renderedSelection() const;

signals:
    void toolChanged();
    void strokeColorChanged();
    void uiChromeChanged();
    void colorPickingChanged();
    // A pixel was sampled in screen colour-pick mode.
    void colorPicked(const QColor &c);
    void shapeFillColorChanged();
    void shapeFillEnabledChanged();
    void strokeWidthChanged();
    void arrowHeadStyleChanged();
    void fontSizeChanged();
    void stepSizeChanged();
    void fontFamilyChanged();
    void fontBoldChanged();
    void fontItalicChanged();
    void fontUnderlineChanged();
    void textOutlineChanged();
    void textOutlineColorChanged();
    void textBackgroundChanged();
    void textBackgroundColorChanged();
    void selectionModeChanged();
    void confirmOnReleaseChanged();
    void selectionRectChanged();
    void hoverPointChanged();
    void historyChanged();
    void imageChanged();
    void renderScaleChanged();
    void textRequested(qreal imgX, qreal imgY);
    // EditShapes: a Text annotation was double-clicked — QML reopens the
    // floating editor at (imgX,imgY) prefilled with `text`; commitTextEdit
    // writes the result back into the selected annotation.
    void textEditRequested(qreal imgX, qreal imgY, const QString &text);
    void selectionConfirmed();
    // Right click is deliberately a context-free quick copy. The editor copies
    // its composite; the screenshot overlay confirms + forces a copy.
    void copyRequested();
    void selectedAnnotChanged();
    void ocrChanged();
    // Emitted when the Highlight tool is selected and no glyph boxes are cached
    // yet — the editor answers by OCR-ing the base and calling setGlyphBoxes().
    void glyphBoxesRequested();
    void highlightModeChanged();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;

private:
    struct Annot {
        Tool type = None;
        QRectF rect;                // line tools use topLeft->bottomRight as p1->p2
        QVector<QPointF> points;    // freehand
        QColor color;
        QColor fillColor;           // shape fill (when filled)
        bool filled = false;
        qreal width = 3;
        int arrowHeadStyle = 0;      // 0 filled, 1 open, 2 both ends
        QString text;               // Text tool: may contain '\n' (multi-line)
        QImage image;                // PastedImage: implicitly shared, image-pixel space
        int fontSize = 18;
        QString fontFamily;         // empty = default UI font
        bool bold = true;
        bool italic = false;
        bool underline = false;
        bool outlined = false;      // stroke each glyph with outlineColor
        QColor outlineColor = QColor(0, 0, 0);
        bool textBg = false;        // rounded box behind the text
        QColor textBgColor = QColor(0, 0, 0, 179);
        int number = 0;             // step marker
        int stepSize = 22;          // step marker badge size (independent of fontSize)
        // Blur/Pixelate patch cache: recomputing the smooth down/up-scale of the
        // base region on EVERY repaint (i.e. every drag mouse-move) burned
        // milliseconds per patch for byte-identical output. Keyed on the base's
        // cacheKey (changes whenever the shared data is swapped or detached),
        // the rect and the width. mutable: drawAnnot is const.
        mutable QImage fxPatch;
        mutable qint64 fxBaseKey = -1;
        mutable QRectF fxRect;
        mutable qreal fxWidth = -1;
        // Whether the cached patch was built with fast (drag-preview) sampling —
        // when the interactive gesture ends the smooth version is rebuilt once.
        mutable bool fxFast = false;
    };

    // MoveAnnot/ResizeAnnot: drag the selected placed annotation (EditShapes).
    // PendingMoveAnnot: a press that selected/re-hit a shape but has not yet
    // moved — a bare click just selects; a real drag promotes it to MoveAnnot.
    // PendingDraw: a press with a drawing tool that has not moved yet — a real
    // drag promotes it to DrawDrag (beginDraw), a bare click instead
    // selects/deselects the shape under the cursor (direct shape editing
    // without switching to the Edit tool).
    enum DragMode { NoDrag, DrawDrag, PendingDraw, NewSelection, MoveSelection, ResizeSelection,
                    PendingMoveAnnot, MoveAnnot, ResizeAnnot };

    // Start a fresh in-progress annotation for the active tool at `at`
    // (shared by the immediate Pen path and the PendingDraw promotion).
    void beginDraw(const QPointF &at);

    void pushUndo();
    void drawAnnot(QPainter &p, const Annot &a) const;
    void drawAll(QPainter &p) const;
    // A stroke drawn as a freehand path (accumulated points) rather than a
    // dragged rect: the Pen, or the Highlight tool in freehand marker mode.
    static bool isFreehandStroke(const Annot &a)
    { return a.type == Pen || (a.type == Highlight && !a.points.isEmpty()); }
    // Image-space bounds of an annotation incl. stroke/arrow-head slack; used
    // to repaint only the dirty region while drag-drawing.
    QRectF annotBoundsImg(const Annot &a) const;
    // Repaint only the given image-space rect (converted to item coords with a
    // small slack for borders/handles) instead of the whole ~MP base — used by
    // the move/resize/selection drags so a 4K capture doesn't get resampled in
    // full every frame.
    void updateImgRect(const QRectF &imgRect);
    // Tight multi-line bounds of a Text annotation (no padding/outline slack).
    QRectF textBoundsImg(const Annot &a) const;
    QFont annotFontFor(const Annot &a) const;
    int hitHandle(const QPointF &imgPos) const; // 0..7 handles, -1 none
    void normalizeSelection();
    // EditShapes helpers. annotAt: reverse-z-order hit-test over m_items,
    // returns the topmost index under imgPos (-1 none). The handle helpers
    // operate on the selected annotation: handlesForSelected fills up to 8
    // handle points (rect-like) or 2 endpoints (line/arrow) and returns the
    // count (0 = move-only: pen/text/step); hitAnnotHandle returns the handle
    // index under imgPos (-1 none). selectAnnot copies the shape's style into
    // the canvas "current" props so the props bar reflects it, then emits.
    int annotAt(const QPointF &imgPos) const;
    int handlesForSelected(QPointF out[8]) const;
    int hitAnnotHandle(const QPointF &imgPos) const;
    // Cursor for direct shape editing (resize handle → directional, body →
    // pointing hand, none → arrow). Shared by the Edit tool and the drawing-
    // tool direct-manipulation path so both give the same handle feedback.
    Qt::CursorShape annotEditCursor(const QPointF &imgPos) const;
    void selectAnnot(int index);
    // Restore the user's pre-selection style captured in m_styleBackup. Called
    // from every deselect path so a clicked shape's style can't leak into the
    // drawing defaults. Caller must have already cleared m_selectedAnnot.
    void restoreStyleBackup();
    void applyStyleToSelected(); // push current props onto m_items[m_selectedAnnot]
    // A style property setter calls this AFTER updating the "current" prop: when
    // a shape is selected it coalesces an undo entry, copies the props onto the
    // selected annotation and repaints. propId groups repeats for coalescing.
    void routeToSelected(int propId);
    // Undo coalescing for a repeated edit of the same (shape, property) within
    // a short window — a slider drag becomes one undo entry, not dozens.
    void pushUndoCoalesced(int propId);
    QColor sampleEdgeColor(const QRectF &r) const;

    QImage m_base;
    QVector<Annot> m_items;
    struct Snapshot { QImage base; QVector<Annot> items; int stepCounter = 0; };
    QVector<Snapshot> m_undo, m_redo;

    int m_tool = None;
    QColor m_color = QColor(QStringLiteral("#FF4757"));
    QColor m_uiAccent = QColor(200, 172, 214); // #C8ACD6 default (unisic accent)
    QColor m_uiScrim = QColor(23, 21, 59);     // #17153B default (unisic primary)
    bool m_colorPicking = false;
    // An explicit color pick disables the highlighter's automatic yellow
    // default (see setTool) — the chosen color is then always drawn as-is.
    bool m_strokeColorTouched = false;
    bool m_strokeAuto = false; // current color set by the auto-swap, not the user
    QColor m_fillColor = QColor(255, 71, 87, 60);
    bool m_fillEnabled = false;
    int m_strokeWidth = 4;
    int m_fontSize = 22;
    QString m_fontFamily;
    bool m_fontBold = true;
    bool m_fontItalic = false;
    bool m_fontUnderline = false;
    bool m_textOutline = false;
    QColor m_textOutlineColor = QColor(0, 0, 0);
    bool m_textBackground = false;
    QColor m_textBgColor = QColor(0, 0, 0, 179);
    int m_stepCounter = 0;
    int m_stepSize = 22;
    int m_arrowHeadStyle = 0;
    int m_highlightMode = HlText; // see HighlightMode; text-snap by default

    // Style snapshot taken when a selection is made from NO selection: a
    // click-select seeds the props bar from the clicked shape, and deselecting
    // must hand the user their own defaults back — not the shape's style.
    Annot m_styleBackup;
    bool m_styleBackupValid = false;

    bool m_selectionMode = false;
    bool m_confirmOnRelease = false;
    QRectF m_selection;
    QPointF m_hoverPoint;
    QRectF m_lastDragBoundsImg;   // previous m_current bounds during DrawDrag
    // Pen straight-line: while Shift is held mid-stroke, the freehand tail is
    // replaced by a single straight segment from this committed anchor index to
    // the cursor. -1 = not currently in a Shift straight-segment.
    int m_penShiftAnchor = -1;
    DragMode m_drag = NoDrag;
    int m_resizeHandle = -1;
    QPointF m_dragStart;      // image coords
    QRectF m_selStart;
    Annot m_current;
    bool m_drawing = false;
    // True while a Blur's geometry is being dragged: its patch is rebuilt with
    // fast sampling each frame (drawAnnot) and re-smoothed once on release.
    bool m_fxFast = false;

    // EditShapes selection.
    int m_selectedAnnot = -1;
    QRectF m_annotStartRect;               // rect at the start of a move/resize
    QVector<QPointF> m_annotStartPoints;   // pen points at the start of a move
    bool m_suppressStyleToSelected = false;// true while selectAnnot seeds props
    bool m_resizeUndoPending = false;      // push one undo on the resize's first move

    // OCR text-pick mode. Glyphs are in reading order; the selection is two
    // CARET positions in [0..N] (a caret at k sits before glyph k), dragged like
    // a text cursor. Selected glyphs = [min..max-1]; equal carets = empty; -1 =
    // no selection. m_ocrLines caches each text line's contiguous glyph run and
    // its clean band (union of the line's glyph rects) — the source of the
    // always-visible selectable-text boxes, line-constrained caret hit-testing,
    // and consistent-height selection bands.
    struct OcrLine { int first = 0; int last = 0; QRectF band; };
    quint64 m_ocrSeq = 0;                  // staleness guard for async OCR results
    bool m_ocrMode = false;
    bool m_ocrBusy = false;
    QVector<OcrWord> m_ocrWords;
    QVector<OcrLine> m_ocrLines;
    int m_ocrCaretA = -1;       // anchor caret [0..N]
    int m_ocrCaretB = -1;       // active caret [0..N]
    int m_ocrPressCaret = -1;   // caret under the press, applied on the first drag
    bool m_ocrDragging = false;
    int m_ocrHoverLine = -1;    // line (cache index) hover-highlighted (-1 none)
    bool m_ocrDidDrag = false;  // a press-drag passed the letter-select threshold

    // Text-aware Highlight glyph cache — DISTINCT from m_ocrWords / OCR mode:
    // populated silently so a normal Highlight drag can snap to text without
    // entering the OCR selection overlay.
    QVector<OcrWord> m_glyphBoxes;
    bool m_glyphBoxesReady = false;
    bool m_glyphBoxesRequested = false;  // don't re-ask on every tool switch
    qint64 m_glyphBoxesBaseKey = -1;     // invalidate when the base image changes
    // Snap a plain Highlight drag to per-line text boxes when it lands over
    // detected text. Returns true when it appended snapped annots (the caller
    // then skips the single plain rectangle); false to fall back to a plain rect.
    bool applyTextAwareHighlight(const Annot &drag);
    // Rebuild m_ocrLines from m_ocrWords (contiguous same-line glyph runs).
    void rebuildOcrLines();
    // Cache index of the line containing glyph g (-1 if none).
    int ocrLineOfGlyph(int g) const;
    // Line (cache index) nearest an image point, vertical distance dominating;
    // -1 when there are no lines.
    int ocrLineAt(const QPointF &imgPos) const;
    // Is the point actually over line k's text band (with a small margin)?
    bool ocrOverLine(const QPointF &imgPos, int line) const;
    // Caret position [0..N] under an image point: the inter-glyph gap nearest the
    // cursor x, constrained to the line under the cursor. -1 when there is no text.
    int ocrCaretAt(const QPointF &imgPos) const;
    // Nearest glyph on line k to x (for the word double-click). -1 out of range.
    int ocrGlyphAtOnLine(int line, qreal x) const;
    // Expand a glyph index to the inclusive [lo..hi] range of its whole word
    // (word boundary = OcrWord::spaceBefore), within its text line.
    void ocrExpandWord(int i, int &lo, int &hi) const;
    // Union (with slack) of the full line bands touched by the caret range
    // [min..max] — dirties exactly the lines whose selection bars changed.
    QRectF ocrCaretSpanDirty(int cA, int cB) const;
    // Adds one permanent annotation for each text line spanned by the current
    // OCR range. Keeping the batch here ensures highlight/redact share the
    // same endpoint clipping and a single undo snapshot.
    bool addOcrSelectionAnnotations(Tool type, const QColor &color,
                                    bool filled = false, const QColor &fillColor = QColor());
    // Coalesce-window bookkeeping (see pushUndoCoalesced).
    int m_lastCoalesceProp = -1;
    int m_lastCoalesceIndex = -1;
    QElapsedTimer m_coalesceTimer;
};

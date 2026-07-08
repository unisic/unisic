#pragma once
#include <QQuickPaintedItem>
#include <QImage>
#include <QColor>
#include <QVector>
#include <QRect>
#include <QTimer>
#include <qqmlregistration.h>

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
    // "shapeFill*" — QQuickPaintedItem already has a fillColor property (the
    // item background); reusing that name shadows it and breaks both.
    Q_PROPERTY(QColor shapeFillColor READ shapeFillColor WRITE setShapeFillColor NOTIFY shapeFillColorChanged)
    Q_PROPERTY(bool shapeFillEnabled READ shapeFillEnabled WRITE setShapeFillEnabled NOTIFY shapeFillEnabledChanged)
    Q_PROPERTY(int strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY strokeWidthChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool selectionMode READ selectionMode WRITE setSelectionMode NOTIFY selectionModeChanged)
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY selectionRectChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionRectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageChanged)
    Q_PROPERTY(qreal renderScale READ renderScale NOTIFY renderScaleChanged)
    // Object-pick: true while the foreground segmentation runs off-thread;
    // hasObjectMask flips once a usable mask is previewed on the selection.
    Q_PROPERTY(bool segmenting READ segmenting NOTIFY segmentingChanged)
    Q_PROPERTY(bool hasObjectMask READ hasObjectMask NOTIFY segmentingChanged)

public:
    enum Tool {
        None = 0, Pen, Line, Arrow, Rect, Ellipse, Text,
        Blur, Pixelate, Highlight, Step, Crop,
        SmartErase,
        ObjectPick   // 13 — overlay-only: hover to highlight a detected object, click to capture it
    };
    Q_ENUM(Tool)

    explicit AnnotationCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    void setImage(const QImage &img);
    QImage image() const { return m_base; }

    int tool() const { return m_tool; }
    void setTool(int t);
    QColor strokeColor() const { return m_color; }
    void setStrokeColor(const QColor &c);
    QColor shapeFillColor() const { return m_fillColor; }
    void setShapeFillColor(const QColor &c);
    bool shapeFillEnabled() const { return m_fillEnabled; }
    void setShapeFillEnabled(bool on);
    int strokeWidth() const { return m_strokeWidth; }
    void setStrokeWidth(int w);
    int fontSize() const { return m_fontSize; }
    void setFontSize(int s);
    bool selectionMode() const { return m_selectionMode; }
    void setSelectionMode(bool on);
    QRectF selectionRect() const { return m_selection; }
    bool hasSelection() const { return m_selection.width() > 2 && m_selection.height() > 2; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QSize imageSize() const { return m_base.size(); }
    qreal renderScale() const;
    // "Segmenting" includes a pending nudge-debounce: a confirm arriving in
    // that window must also wait, or it would export the stale/absent mask.
    bool segmenting() const;
    bool hasObjectMask() const { return !m_objectMask.isNull(); }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearAnnotations();
    Q_INVOKABLE void commitText(qreal imgX, qreal imgY, const QString &text);
    Q_INVOKABLE void nudgeSelection(qreal dx, qreal dy);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void applyCrop();
    Q_INVOKABLE QPointF toImage(qreal itemX, qreal itemY) const;
    Q_INVOKABLE QRectF selectionInItemCoords() const;

    // Final composite at full image resolution (annotations burnt in).
    QImage rendered() const;
    // rendered() cropped to the selection (overlay flow).
    QImage renderedSelection() const;

signals:
    void toolChanged();
    void strokeColorChanged();
    void shapeFillColorChanged();
    void shapeFillEnabledChanged();
    void strokeWidthChanged();
    void fontSizeChanged();
    void selectionModeChanged();
    void selectionRectChanged();
    void historyChanged();
    void imageChanged();
    void renderScaleChanged();
    void textRequested(qreal imgX, qreal imgY);
    void selectionConfirmed();
    void segmentingChanged();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
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
        QString text;
        int fontSize = 18;
        int number = 0;             // step marker
    };

    // PendingNewSelection: an ObjectPick press that did NOT hit a candidate is
    // held back until real drag movement — a bare press must not destroy the
    // current selection/mask, because it may be the first half of the
    // double-click-confirm gesture.
    enum DragMode { NoDrag, DrawDrag, NewSelection, MoveSelection, ResizeSelection,
                    PendingNewSelection };

    void pushUndo();
    void drawAnnot(QPainter &p, const Annot &a) const;
    void drawAll(QPainter &p) const;
    int hitHandle(const QPointF &imgPos) const; // 0..7 handles, -1 none
    void normalizeSelection();
    QColor sampleEdgeColor(const QRectF &r) const;
    void startSegmentation();
    void clearObjectMask();

    QImage m_base;
    QVector<Annot> m_items;
    struct Snapshot { QImage base; QVector<Annot> items; int stepCounter = 0; };
    QVector<Snapshot> m_undo, m_redo;

    int m_tool = None;
    QColor m_color = QColor(QStringLiteral("#FF4757"));
    QColor m_fillColor = QColor(255, 71, 87, 60);
    bool m_fillEnabled = false;
    int m_strokeWidth = 4;
    int m_fontSize = 22;
    int m_stepCounter = 0;

    bool m_selectionMode = false;
    QRectF m_selection;
    DragMode m_drag = NoDrag;
    int m_resizeHandle = -1;
    QPointF m_dragStart;      // image coords
    QRectF m_selStart;
    Annot m_current;
    bool m_drawing = false;

    // Object-pick mode (overlay): detected candidate rects + the one under the
    // cursor. Detection runs off-thread the first time the tool is selected.
    QVector<QRect> m_objectCandidates;
    QRect m_hoverObject;
    QFutureWatcher<QVector<QRect>> *m_detectWatcher = nullptr;

    // Object-pick foreground mask for the current selection (Grayscale8 at
    // region size, 255 = keep). Computed off-thread; m_segmentSeq drops stale
    // results, m_maskOverlay is the cached dim-the-background preview,
    // m_segmentRect is the region of the last started run (done OR in flight —
    // re-clicking it must not restart and wipe the mask mid-double-click),
    // m_nudgeTimer debounces re-segmentation during arrow-key autorepeat.
    QImage m_objectMask;
    QRect m_objectMaskRect;
    QImage m_maskOverlay;
    QRect m_segmentRect;
    quint64 m_segmentSeq = 0;
    int m_segmentActive = 0;
    QTimer *m_nudgeTimer = nullptr;
};

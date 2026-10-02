#pragma once
#include <QImage>
#include <QVector>

// Stitches the frames of a region the user scrolls into one tall image.
//
// Alignment is pixel-exact: a per-row luminance profile proposes candidate
// shifts, and every candidate is verified row by row against the real pixels.
// Stream frames are lossless, so the right shift matches byte for byte while a
// shift that is off by one row breaks every line of text. Rows are copied
// verbatim and never resampled, so the result is exactly as sharp as the
// screen and reads at any zoom.
//
// Rows that stay put while the content moves (sticky headers and footers,
// columns that do not scroll like a sidebar) are left out of the matching and
// kept once instead of repeating in every slice. The viewport position is
// tracked in document coordinates, so scrolling back up, or starting at the
// bottom of a chat and scrolling up, extends the image at the top.
class ScrollStitcher
{
public:
    enum class Result {
        Started,    // first frame, or a replacement before the first scroll
        Stitched,   // new rows were added
        Unchanged,  // no scroll (identical, or only a small part changed)
        Unmatched,  // the content moved but no shift fits: scrolled too fast
        Full        // the size cap is reached
    };

    struct Shift {
        bool changed = false;     // most of the area between the sticky bands changed
        bool ok = false;          // a verified shift was found
        int dy = 0;               // >0 content moved up (scrolled down)
        int header = 0;           // top rows that did not move
        int footer = 0;           // bottom rows that did not move
        QVector<int> staleRows;   // overlapping rows of curr that differ from prev
    };

    // Upper bound for the stitched image. A 1920px-wide region gets ~34000 rows.
    static constexpr qsizetype kMaxBytes = qsizetype(256) * 1024 * 1024;

    void reset();
    Result addFrame(const QImage &frame);

    // The full stitched image, at the frames' native resolution.
    QImage stitchedImage() const;
    // Small preview of the stitched body (nearest-neighbour, cheap at any height).
    QImage previewThumbnail(int maxW, int maxH) const;

    int stitchedWidth() const { return m_prev.width(); }
    int stitchedHeight() const;
    int frameCount() const { return m_frameCount; }
    bool isFull() const { return m_full; }
    bool isEmpty() const { return m_frameCount == 0; }

    // Pure alignment of two equally sized 32-bit frames.
    static Shift findShift(const QImage &prev, const QImage &curr);

private:
    void restart(const QImage &frame);
    bool ensureCanvas(int top, int bottom);
    void writeRow(int docRow, const QImage &src, int srcRow);

    QImage m_prev;          // last aligned frame
    int m_pos = 0;          // document row of m_prev's row 0
    bool m_moved = false;   // a scroll has been stitched since the first frame
    bool m_full = false;    // the size cap was reached

    QImage m_canvas;        // body rows, document row r at canvas row r - m_canvasTop
    int m_canvasTop = 0;
    int m_top = 0;          // body range [m_top, m_bottom) in document rows
    int m_bottom = 0;
    QImage m_head;          // sticky header shown above the body
    QImage m_tail;          // sticky footer shown below the body
    int m_headPos = 0;      // document row of m_head's first row
    int m_tailEnd = 0;      // document row just past m_tail

    int m_frameCount = 0;
};

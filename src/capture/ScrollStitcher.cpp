#include "ScrollStitcher.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

// Per-channel tolerance of a "matching" pixel: absorbs dithering and colour
// management rounding, far below the contrast a misaligned glyph edge makes.
constexpr int kTol = 8;
// Contrast inside a row that makes it carry information (text, edges). Flat
// rows match at every shift, so they never decide an alignment.
constexpr int kFlat = 24;

// Only colour bytes 0..2 are compared: every 32-bit format the grabbers
// deliver (RGB32/ARGB32 = B,G,R,A and RGBX/RGBA8888 = R,G,B,A in memory)
// keeps alpha or padding in byte 3, which may be garbage for the x formats.
inline bool pxEq(const uchar *a, const uchar *b)
{
    return std::abs(a[0] - b[0]) <= kTol && std::abs(a[1] - b[1]) <= kTol
        && std::abs(a[2] - b[2]) <= kTol;
}

inline int lum(const uchar *p) { return p[0] + 2 * p[1] + p[2]; }

struct Rows {
    const QImage &prev;
    const QImage &curr;
    const QVector<int> &cols;   // byte offsets of the sampled, non-static columns
    int allowed;                // mismatching pixels a row may have (scrollbar thumb, caret)

    bool equal(int prevRow, int currRow) const
    {
        const uchar *a = prev.constScanLine(prevRow);
        const uchar *b = curr.constScanLine(currRow);
        int bad = 0;
        for (int off : cols) {
            if (!pxEq(a + off, b + off) && ++bad > allowed)
                return false;
        }
        return true;
    }

    bool informative(int currRow) const
    {
        const uchar *b = curr.constScanLine(currRow);
        const uchar *ref = b + cols.first();
        for (int off : cols) {
            const uchar *p = b + off;
            if (std::abs(p[0] - ref[0]) > kFlat || std::abs(p[1] - ref[1]) > kFlat
                || std::abs(p[2] - ref[2]) > kFlat)
                return true;
        }
        return false;
    }
};

QImage as32(const QImage &img)
{
    return img.depth() == 32 ? img : img.convertToFormat(QImage::Format_RGB32);
}

} // namespace

ScrollStitcher::Shift ScrollStitcher::findShift(const QImage &prevIn, const QImage &currIn)
{
    Shift out;
    if (prevIn.isNull() || prevIn.size() != currIn.size())
        return out;
    const QImage prev = as32(prevIn);
    const QImage curr = as32(currIn);
    const int w = curr.width();
    const int h = curr.height();
    if (w < 8 || h < 32)
        return out;

    // Sample at most ~640 columns: vertical alignment only needs enough of each
    // row to tell text lines apart, and this bounds the cost on 4K regions.
    const int sx = std::max(1, w / 640);

    // Columns that did not change at all do not scroll (sidebar, blank margin)
    // and would only dilute the comparison. The right edge is skipped too: the
    // scrollbar thumb moves at its own pace, overlay scrollbars fade in there.
    const int scrollbar = std::clamp(w / 16, 8, 40);
    QVector<int> cols;
    cols.reserve(w / sx + 1);
    for (int x = 0; x < w - scrollbar; x += sx) {
        const int off = x * 4;
        for (int y = 0; y < h; ++y) {
            if (!pxEq(prev.constScanLine(y) + off, curr.constScanLine(y) + off)) {
                cols.append(off);
                break;
            }
        }
    }
    if (cols.size() < 4)
        return out;   // identical, or a few pixels flickered

    // A row still matches with a few differing pixels (a blinking caret).
    // Any looser and the ascender/descender rows of a misaligned text line,
    // which carry only a handful of ink pixels, would pass as matches.
    const Rows rows{prev, curr, cols, std::max(1, int(cols.size()) / 50)};

    // Sticky rows: identical at the same position in both frames.
    int header = 0;
    while (header < h && rows.equal(header, header))
        ++header;
    if (header == h)
        return out;
    int footer = 0;
    while (footer < h - header && rows.equal(h - 1 - footer, h - 1 - footer))
        ++footer;

    out.header = header;
    out.footer = footer;

    // A scroll moves nearly everything between the sticky bands. A change
    // confined to a small band is an animation, a hover or a caret.
    const int areaTop = header;
    const int areaBottom = h - footer;
    const int areaH = areaBottom - areaTop;
    if (areaH < std::max(32, h / 4))
        return out;
    out.changed = true;
    const int minOverlap = std::max(16, areaH / 8);
    const int maxShift = areaH - minOverlap;
    if (maxShift < 1)
        return out;

    // Coarse search: row luminance sums over the sampled columns.
    QVector<qint64> pp(h), pc(h);
    for (int y = 0; y < h; ++y) {
        const uchar *a = prev.constScanLine(y);
        const uchar *b = curr.constScanLine(y);
        qint64 sa = 0, sb = 0;
        for (int off : cols) {
            sa += lum(a + off);
            sb += lum(b + off);
        }
        pp[y] = sa;
        pc[y] = sb;
    }
    // Cap a single row's contribution so one changed row (a loaded image)
    // cannot push the true shift out of the candidate list.
    const qint64 cap = qint64(cols.size()) * 4 * 16;

    struct Cand { int d; double score; };
    QVector<Cand> cands;
    cands.reserve(2 * maxShift);
    for (int d = -maxShift; d <= maxShift; ++d) {
        if (d == 0)
            continue;
        const int y0 = d > 0 ? areaTop : areaTop - d;
        const int y1 = d > 0 ? areaBottom - d : areaBottom;
        qint64 sum = 0;
        for (int y = y0; y < y1; ++y)
            sum += std::min(cap, std::abs(pc[y] - pp[y + d]));
        cands.append({d, double(sum) / (y1 - y0)});
    }
    const int top = std::min<int>(6, cands.size());
    std::partial_sort(cands.begin(), cands.begin() + top, cands.end(),
                      [](const Cand &a, const Cand &b) { return a.score < b.score; });
    QVector<int> check;
    for (int i = 0; i < top; ++i) {
        for (int d = cands[i].d - 1; d <= cands[i].d + 1; ++d) {
            if (d != 0 && std::abs(d) <= maxShift && !check.contains(d))
                check.append(d);
        }
    }

    // Exact verification against the pixels.
    int bestInf = -1, bestBad = 0, bestD = 0;
    for (int d : check) {
        const int y0 = d > 0 ? areaTop : areaTop - d;
        const int y1 = d > 0 ? areaBottom - d : areaBottom;
        const int overlap = y1 - y0;
        int inf = 0, infOk = 0, bad = 0;
        bool dead = false;
        for (int y = y0; y < y1; ++y) {
            const bool ok = rows.equal(y + d, y);
            const bool info = rows.informative(y);
            inf += info;
            infOk += info && ok;
            bad += !ok;
            if (info && !ok && (inf - infOk) * 4 > overlap) {
                dead = true;   // cannot reach 75% any more
                break;
            }
        }
        if (dead || infOk < 6 || infOk * 4 < inf * 3)
            continue;
        if (infOk > bestInf || (infOk == bestInf && (bad < bestBad
                || (bad == bestBad && std::abs(d) < std::abs(bestD))))) {
            bestInf = infOk;
            bestBad = bad;
            bestD = d;
        }
    }
    if (bestInf < 0)
        return out;

    out.ok = true;
    out.dy = bestD;
    const int y0 = bestD > 0 ? areaTop : areaTop - bestD;
    const int y1 = bestD > 0 ? areaBottom - bestD : areaBottom;
    for (int y = y0; y < y1; ++y) {
        if (!rows.equal(y + bestD, y))
            out.staleRows.append(y);
    }
    return out;
}

void ScrollStitcher::reset()
{
    *this = ScrollStitcher();
}

void ScrollStitcher::restart(const QImage &frame)
{
    m_prev = frame;
    m_pos = 0;
    m_head = QImage();
    m_tail = QImage();
    m_canvas = QImage();
    m_canvasTop = 0;
    m_top = 0;
    m_bottom = 0;
    ensureCanvas(0, frame.height());
    for (int y = 0; y < frame.height(); ++y)
        writeRow(y, frame, y);
    m_top = 0;
    m_bottom = frame.height();
    m_headPos = 0;
    m_tailEnd = frame.height();
    m_frameCount = 1;
}

bool ScrollStitcher::ensureCanvas(int top, int bottom)
{
    const int maxRows = int(kMaxBytes / (qsizetype(m_prev.width()) * 4));
    if (bottom - top > maxRows)
        return false;
    const int cap = m_canvas.height();
    if (!m_canvas.isNull() && top >= m_canvasTop && bottom <= m_canvasTop + cap)
        return true;

    const int need = bottom - top;
    const int newCap = std::min(maxRows, std::max(need + 2048, cap * 3 / 2));
    // Headroom goes where the growth happened: below for scrolling down,
    // above for scrolling up.
    const int newTop = (!m_canvas.isNull() && top < m_canvasTop) ? bottom - newCap : top;
    QImage bigger(m_prev.width(), newCap, m_prev.format());
    if (bigger.isNull())
        return false;   // allocation failed
    const qsizetype rowBytes = qsizetype(m_prev.width()) * 4;
    for (int r = m_top; r < m_bottom; ++r)
        std::memcpy(bigger.scanLine(r - newTop), m_canvas.constScanLine(r - m_canvasTop), rowBytes);
    m_canvas = bigger;
    m_canvasTop = newTop;
    return true;
}

void ScrollStitcher::writeRow(int docRow, const QImage &src, int srcRow)
{
    std::memcpy(m_canvas.scanLine(docRow - m_canvasTop), src.constScanLine(srcRow),
                qsizetype(src.width()) * 4);
}

ScrollStitcher::Result ScrollStitcher::addFrame(const QImage &frameIn)
{
    if (frameIn.isNull() || frameIn.width() < 8 || frameIn.height() < 32)
        return Result::Unchanged;
    if (m_full)
        return Result::Full;
    QImage frame = as32(frameIn);

    if (m_frameCount == 0 || frame.size() != m_prev.size()) {
        if (m_frameCount != 0 && m_moved)
            return Result::Unchanged;   // geometry changed mid-capture: keep what we have
        restart(frame);
        return Result::Started;
    }
    if (frame.format() != m_prev.format())
        frame = frame.convertToFormat(m_prev.format());

    const Shift s = findShift(m_prev, frame);
    if (!s.changed)
        return Result::Unchanged;
    if (!s.ok) {
        // Before the first scroll the newest frame is the truth: the region
        // overlay may still have been fading out, or the page was settling.
        if (!m_moved) {
            restart(frame);
            return Result::Started;
        }
        return Result::Unmatched;
    }

    const int h = frame.height();
    const int w = frame.width();
    const int H = s.header;
    const int F = s.footer;

    // The head covers document rows [m_headPos, m_top), the tail
    // [m_bottom, m_tailEnd). When m_prev's sticky band is bigger than what is
    // filed as head/tail (the first frame went in whole, before anything was
    // known to be sticky), move those rows out of the body into the head/tail.
    if (m_headPos == m_pos && m_top < m_pos + H) {
        m_top = m_pos + H;
        m_head = m_prev.copy(0, 0, w, H);
    }
    if (m_tailEnd == m_pos + h && m_bottom > m_pos + h - F) {
        m_bottom = m_pos + h - F;
        m_tail = m_prev.copy(0, h - F, w, F);
    }

    const int pos = m_pos + s.dy;
    const int aTop = pos + H;
    const int aBottom = pos + h - F;
    if (aTop > m_bottom || aBottom < m_top)
        return Result::Unmatched;   // cannot happen with a verified overlap

    // Grow upwards only when this frame also replaces the whole head (its top
    // is at or above the head's), downwards only when it replaces the whole
    // tail; otherwise rows that live in the head/tail would be lost or doubled.
    const bool upOk = pos <= m_headPos;
    const bool downOk = pos + h >= m_tailEnd;
    int newTop = upOk ? std::min(m_top, aTop) : m_top;
    int newBottom = downOk ? std::max(m_bottom, aBottom) : m_bottom;

    // Clamp the growth to the size cap, keeping the body contiguous.
    const int maxRows = int(kMaxBytes / (qsizetype(w) * 4)) - m_head.height() - m_tail.height();
    bool clamped = false;
    if (newBottom - newTop > maxRows) {
        clamped = true;
        newBottom = std::max(m_bottom, newTop + maxRows);
        newTop = std::min(m_top, newBottom - maxRows);
    }
    if (!ensureCanvas(newTop, newBottom)) {
        m_full = true;
        return Result::Full;
    }

    for (int r = newTop; r < m_top; ++r)
        writeRow(r, frame, r - pos);
    for (int r = m_bottom; r < newBottom; ++r)
        writeRow(r, frame, r - pos);
    // Rows that changed in place (an image finished loading) take the newer pixels.
    for (int y : s.staleRows) {
        const int r = pos + y;
        if (r >= m_top && r < m_bottom)
            writeRow(r, frame, y);
    }

    const bool grew = newTop < m_top || newBottom > m_bottom;
    if (clamped) {
        // The capped edge no longer meets this frame's sticky band.
        if (newTop < m_top || aTop < newTop) { m_head = QImage(); m_headPos = newTop; }
        if (newBottom > m_bottom || aBottom > newBottom) { m_tail = QImage(); m_tailEnd = newBottom; }
    } else {
        // This frame reaches past the head/tail, so its rows above the body
        // (below it) become the new head (tail). Usually that is its sticky
        // band; when the band overlaps rows the body already has, only the
        // part outside the body.
        if (upOk) {
            const int rows = newTop - pos;
            m_head = rows > 0 ? frame.copy(0, 0, w, rows) : QImage();
            m_headPos = pos;
        }
        if (downOk) {
            const int rows = pos + h - newBottom;
            m_tail = rows > 0 ? frame.copy(0, h - rows, w, rows) : QImage();
            m_tailEnd = pos + h;
        }
    }
    m_top = newTop;
    m_bottom = newBottom;
    m_prev = frame;
    m_pos = pos;
    m_moved = true;
    m_full = clamped;

    if (grew) {
        ++m_frameCount;
        return Result::Stitched;
    }
    return clamped ? Result::Full : Result::Unchanged;
}

int ScrollStitcher::stitchedHeight() const
{
    return m_frameCount == 0 ? 0 : m_head.height() + (m_bottom - m_top) + m_tail.height();
}

QImage ScrollStitcher::stitchedImage() const
{
    if (m_frameCount == 0)
        return {};
    const int w = m_prev.width();
    QImage out(w, stitchedHeight(), m_prev.format());
    if (out.isNull())
        return {};
    const qsizetype rowBytes = qsizetype(w) * 4;
    int y = 0;
    for (int r = 0; r < m_head.height(); ++r)
        std::memcpy(out.scanLine(y++), m_head.constScanLine(r), rowBytes);
    for (int r = m_top; r < m_bottom; ++r)
        std::memcpy(out.scanLine(y++), m_canvas.constScanLine(r - m_canvasTop), rowBytes);
    for (int r = 0; r < m_tail.height(); ++r)
        std::memcpy(out.scanLine(y++), m_tail.constScanLine(r), rowBytes);
    return out;
}

QImage ScrollStitcher::previewThumbnail(int maxW, int maxH) const
{
    if (m_frameCount == 0 || m_bottom <= m_top)
        return {};
    // A view over the canvas, no copy; FastTransformation reads only the
    // sampled pixels, so this stays cheap on a 30000-row capture.
    const QImage body(m_canvas.constScanLine(m_top - m_canvasTop), m_canvas.width(),
                      m_bottom - m_top, m_canvas.bytesPerLine(), m_canvas.format());
    return body.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::FastTransformation);
}

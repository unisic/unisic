#include "ObjectDetector.h"
#include <QImage>
#include <QDebug>
#include <QStack>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Strongest-gradient refinement of one rect side at FULL resolution: the
// detection pass runs downscaled, so a mapped-back edge can sit several real
// pixels off the true border. Scores every candidate line within ±pad by its
// summed cross-line contrast (sampled along the side's span) and returns the
// winner — the input position when nothing beats it.
int snapLine(const QImage &gray, bool vertical, int pos, int pad, int spanA, int spanB)
{
    const int limit = vertical ? gray.width() : gray.height();
    spanA = std::max(spanA, 0);
    spanB = std::min(spanB, (vertical ? gray.height() : gray.width()) - 1);
    if (spanB - spanA < 4 || limit < 4)
        return pos;
    const int step = std::max(1, (spanB - spanA) / 64);
    int best = pos;
    qint64 bestScore = -1;
    const int lo = std::max(1, pos - pad), hi = std::min(limit - 2, pos + pad);
    for (int c = lo; c <= hi; ++c) {
        qint64 score = 0;
        if (vertical) {
            for (int y = spanA; y <= spanB; y += step)
                score += std::abs(int(gray.constScanLine(y)[c + 1])
                                  - int(gray.constScanLine(y)[c - 1]));
        } else {
            const uchar *up = gray.constScanLine(c - 1);
            const uchar *dn = gray.constScanLine(c + 1);
            for (int x = spanA; x <= spanB; x += step)
                score += std::abs(int(dn[x]) - int(up[x]));
        }
        if (score > bestScore) { bestScore = score; best = c; }
    }
    return best;
}

} // namespace

namespace {

struct Seg { int pos; int a; int b; }; // line at `pos`, spanning [a, b]

// Long straight runs of orientation-dominant edge pixels — the borders of
// windows, panels, cards and buttons. Small gaps are bridged (title-bar
// buttons, tab separators cut through a border line).
QVector<Seg> extractSegments(const QVector<uchar> &mask, int w, int h,
                             bool horizontal, int minLen, int maxGap)
{
    QVector<Seg> out;
    const int lines = horizontal ? h : w;
    const int span = horizontal ? w : h;
    for (int l = 0; l < lines; ++l) {
        int runStart = -1, lastHit = -1;
        for (int c = 0; c <= span; ++c) {
            const bool hit = c < span
                && mask[horizontal ? l * w + c : c * w + l];
            if (hit) {
                if (runStart < 0)
                    runStart = c;
                lastHit = c;
            } else if (runStart >= 0 && (c - lastHit > maxGap || c == span)) {
                if (lastHit - runStart + 1 >= minLen)
                    out.append({l, runStart, lastHit});
                runStart = -1;
            }
        }
    }
    return out;
}

// Thick (anti-aliased / shadowed) borders yield the same segment on 2-3
// adjacent lines — keep the longest of each cluster.
QVector<Seg> mergeCollinear(QVector<Seg> segs)
{
    std::sort(segs.begin(), segs.end(), [](const Seg &x, const Seg &y) {
        return (x.b - x.a) > (y.b - y.a);
    });
    QVector<Seg> kept;
    for (const Seg &s : std::as_const(segs)) {
        bool dup = false;
        for (const Seg &k : std::as_const(kept)) {
            if (std::abs(k.pos - s.pos) > 2)
                continue;
            const int ov = std::min(k.b, s.b) - std::max(k.a, s.a) + 1;
            if (ov > 0 && ov >= (s.b - s.a + 1) * 7 / 10) { dup = true; break; }
        }
        if (!dup)
            kept.append(s);
    }
    return kept;
}

} // namespace

QVector<QRect> ObjectDetector::detect(const QImage &src)
{
    struct Scored { QRect r; double q; };
    QVector<Scored> scored;
    if (src.isNull() || src.width() < 16 || src.height() < 16)
        return {};

    // UI elements are axis-aligned rectangles whose borders are long straight
    // lines. Connected-component blobs fail on a real desktop — tiled windows
    // share borders, so every outline merges into one screen-sized component.
    // Instead: find long horizontal/vertical edge segments, hypothesize rects
    // from (left,right)x(top,bottom) line pairs, and keep the hypotheses whose
    // four sides are actually traced by edges.

    // 1. Downscaled grayscale for analysis; full-res kept for edge snapping.
    const int maxDim = 1024;
    const int longest = std::max(src.width(), src.height());
    const double scale = longest > maxDim ? double(longest) / maxDim : 1.0;
    const int sw = std::max(3, int(std::lround(src.width() / scale)));
    const int sh = std::max(3, int(std::lround(src.height() / scale)));
    const QImage grayFull = src.convertToFormat(QImage::Format_Grayscale8);
    const QImage gray = scale > 1.0
                            ? grayFull.scaled(sw, sh, Qt::IgnoreAspectRatio,
                                              Qt::SmoothTransformation)
                            : grayFull;
    const int w = gray.width();
    const int h = gray.height();
    if (w < 8 || h < 8)
        return {};

    auto px = [&](int x, int y) -> int { return gray.constScanLine(y)[x]; };

    // 2. Sobel, orientation-split: a pixel belongs to a horizontal border when
    //    the vertical gradient dominates, and vice versa. Adaptive threshold
    //    from the combined magnitude (mean + sigma).
    QVector<uchar> hEdge(w * h, 0), vEdge(w * h, 0);
    {
        QVector<int> gxs(w * h, 0), gys(w * h, 0);
        double sum = 0.0, sumSq = 0.0;
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                const int gx = -px(x-1,y-1) - 2*px(x-1,y) - px(x-1,y+1)
                               + px(x+1,y-1) + 2*px(x+1,y) + px(x+1,y+1);
                const int gy = -px(x-1,y-1) - 2*px(x,y-1) - px(x+1,y-1)
                               + px(x-1,y+1) + 2*px(x,y+1) + px(x+1,y+1);
                gxs[y*w + x] = std::abs(gx);
                gys[y*w + x] = std::abs(gy);
                const int m = std::abs(gx) + std::abs(gy);
                sum += m;
                sumSq += double(m) * m;
            }
        }
        const double n = double((w - 2) * (h - 2));
        const double mean = sum / n;
        const double var = std::max(0.0, sumSq / n - mean * mean);
        // Clamp BOTH ends: mean+sigma explodes when part of the screen is
        // high-contrast content (a video/photo), which starved every subtle
        // UI border elsewhere of edge pixels — a panel edge of ~20 gray
        // levels is mag ~80, so the usable band is narrow.
        const int thresh = std::clamp(int(mean + std::sqrt(var)), 24, 110);
        if (qEnvironmentVariableIsSet("UNISIC_DET_DEBUG"))
            qInfo() << "det: mean" << mean << "sigma" << std::sqrt(var) << "thresh" << thresh;
        for (int i = 0; i < w * h; ++i) {
            // Soft dominance (2x): where a horizontal divider CROSSES a
            // vertical border both gradients spike — strict dominance would
            // punch a hole into one of the lines at every crossing and chop
            // long borders into sub-minLen pieces.
            hEdge[i] = gys[i] > thresh && gys[i] * 2 >= gxs[i] ? 1 : 0;
            vEdge[i] = gxs[i] > thresh && gxs[i] * 2 >= gys[i] ? 1 : 0;
        }
    }

    // 3. Long segments (border candidates), deduped across line thickness and
    //    capped by length so text rows can't flood the pair search.
    const int minSide = 20;                 // downscaled px (~50 real at 2560)
    QVector<Seg> hSegs = mergeCollinear(extractSegments(hEdge, w, h, true, minSide, 3));
    QVector<Seg> vSegs = mergeCollinear(extractSegments(vEdge, w, h, false, minSide, 3));
    const int cap = 224;
    if (hSegs.size() > cap) hSegs.resize(cap); // mergeCollinear left them length-sorted
    if (vSegs.size() > cap) vSegs.resize(cap);
    // The image boundary is a border every screen-edge-touching window shares
    // but no gradient can ever mark — add the four boundary lines as virtual
    // segments (their coverage checks below auto-pass).
    vSegs.append({0, 0, h - 1});
    vSegs.append({w - 1, 0, h - 1});
    hSegs.append({0, 0, w - 1});
    hSegs.append({h - 1, 0, w - 1});

    if (qEnvironmentVariableIsSet("UNISIC_DET_DEBUG"))
        qInfo() << "det: img" << w << "x" << h << "scale" << scale
                 << "hSegs" << hSegs.size() << "vSegs" << vSegs.size();

    // 4. Per-row/per-column prefix sums for O(1) side-coverage checks.
    QVector<int> hPre((w + 1) * h, 0), vPre((h + 1) * w, 0);
    for (int y = 0; y < h; ++y) {
        int *row = hPre.data() + qsizetype(y) * (w + 1);
        for (int x = 0; x < w; ++x)
            row[x + 1] = row[x] + hEdge[y*w + x];
    }
    for (int x = 0; x < w; ++x) {
        int *col = vPre.data() + qsizetype(x) * (h + 1);
        for (int y = 0; y < h; ++y)
            col[y + 1] = col[y] + vEdge[y*w + x];
    }
    // Coverage of a horizontal line at y over [x0,x1], best row in a ±2 band.
    auto hCov = [&](int y, int x0, int x1) -> double {
        double best = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            const int yy = y + dy;
            if (yy < 0 || yy >= h) continue;
            const int *row = hPre.constData() + qsizetype(yy) * (w + 1);
            best = std::max(best, double(row[x1 + 1] - row[x0]) / (x1 - x0 + 1));
        }
        return best;
    };
    auto vCov = [&](int x, int y0, int y1) -> double {
        double best = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            const int xx = x + dx;
            if (xx < 0 || xx >= w) continue;
            const int *col = vPre.constData() + qsizetype(xx) * (h + 1);
            best = std::max(best, double(col[y1 + 1] - col[y0]) / (y1 - y0 + 1));
        }
        return best;
    };
    // Boundary lines are always "covered" — the screen simply ends there.
    auto hCovB = [&](int y, int x0, int x1) -> double {
        return (y <= 1 || y >= h - 2) ? 1.0 : hCov(y, x0, x1);
    };
    auto vCovB = [&](int x, int y0, int y1) -> double {
        return (x <= 1 || x >= w - 2) ? 1.0 : vCov(x, y0, y1);
    };

    // 5. Hypotheses: every (left,right) segment pair, with the horizontal
    //    lines that span it as (top,bottom). All four sides must be traced
    //    (coverage >= 0.55 — rounded corners and crossing content eat a bit).
    const int tol = 4;
    for (int i = 0; i < vSegs.size(); ++i) {
        for (int j = 0; j < vSegs.size(); ++j) {
            const Seg &L = vSegs[i];
            const Seg &R = vSegs[j];
            if (R.pos - L.pos < minSide)
                continue;
            const int yLo = std::max(L.a, R.a);
            const int yHi = std::min(L.b, R.b);
            if (yHi - yLo < minSide)
                continue;
            // Horizontal border candidates: a segment only nominates the Y —
            // BOTH span checks run on COVERAGE (prefix sums), so a border
            // chopped by buttons/tabs still counts, and a top/bottom line
            // outside the V-segments' literal overlap is fine as long as the
            // final vCovB validation reaches it (title bars break vertical
            // borders into pieces shorter than the window).
            QVector<int> tops;
            for (const Seg &t : std::as_const(hSegs)) {
                if (hCovB(t.pos, L.pos, R.pos) >= 0.50)
                    tops.append(t.pos);
            }
            if (tops.size() < 2)
                continue;
            std::sort(tops.begin(), tops.end());
            tops.erase(std::unique(tops.begin(), tops.end(),
                                   [](int a, int b) { return b - a <= 2; }),
                       tops.end());
            if (tops.size() < 2)
                continue;
            // Pair generation: ALL (top,bottom) combinations while the list is
            // small — limiting to outer-frame + adjacent bands skipped many
            // true window rects (their exact borders were neither first/last
            // nor adjacent), which read as "selects too much or too little".
            QVector<QPair<int,int>> pairs;
            if (tops.size() <= 14) {
                for (int a = 0; a < tops.size(); ++a)
                    for (int b = a + 1; b < tops.size(); ++b)
                        pairs.append({tops[a], tops[b]});
            } else {
                pairs.append({tops.first(), tops.last()});
                for (int k = 0; k + 1 < tops.size(); ++k) {
                    pairs.append({tops[k], tops[k + 1]});
                    if (k + 2 < tops.size())
                        pairs.append({tops[k], tops[k + 2]});
                }
            }
            for (const auto &pr : std::as_const(pairs)) {
                const int y0 = pr.first, y1 = pr.second;
                if (y1 - y0 < minSide)
                    continue;
                const double cL = vCovB(L.pos, y0, y1);
                const double cR = vCovB(R.pos, y0, y1);
                if (cL < 0.50 || cR < 0.50)
                    continue;
                // Quality = side coverage, with image-boundary sides slightly
                // discounted: a band that merely runs from screen edge to
                // screen edge must lose a dedup against a window whose four
                // sides are all REAL borders.
                auto side = [](double cov, bool boundary) {
                    return boundary ? cov * 0.8 : cov;
                };
                const double quality =
                    side(cL, L.pos <= 1 || L.pos >= w - 2)
                    + side(cR, R.pos <= 1 || R.pos >= w - 2)
                    + side(hCovB(y0, L.pos, R.pos), y0 <= 1 || y0 >= h - 2)
                    + side(hCovB(y1, L.pos, R.pos), y1 <= 1 || y1 >= h - 2);
                QRect r(int(L.pos * scale), int(y0 * scale),
                        int((R.pos - L.pos + 1) * scale), int((y1 - y0 + 1) * scale));
                r = r.intersected(QRect(0, 0, src.width(), src.height()));
                if (r.width() < 8 || r.height() < 8)
                    continue;
                // 6. Snap each side onto the strongest full-res gradient line.
                // Snap interior sides to the strongest full-res gradient; a
                // side ON the image boundary stays put (snapping would drag
                // it onto the nearest content line).
                const int pad = int(std::ceil(scale)) + 2;
                const int margin = std::min(r.width(), r.height()) / 8;
                const int fw = src.width(), fh = src.height();
                const int nL = r.left() <= 2 ? r.left()
                    : snapLine(grayFull, true,  r.left(),   pad, r.top() + margin,  r.bottom() - margin);
                const int nR = r.right() >= fw - 3 ? r.right()
                    : snapLine(grayFull, true,  r.right(),  pad, r.top() + margin,  r.bottom() - margin);
                const int nT = r.top() <= 2 ? r.top()
                    : snapLine(grayFull, false, r.top(),    pad, r.left() + margin, r.right() - margin);
                const int nB = r.bottom() >= fh - 3 ? r.bottom()
                    : snapLine(grayFull, false, r.bottom(), pad, r.left() + margin, r.right() - margin);
                if (nR - nL >= 8 && nB - nT >= 8)
                    r = QRect(QPoint(nL, nT), QPoint(nR, nB));
                // Near-full-screen hypotheses would duplicate the explicit
                // whole-image candidate appended below.
                if (qint64(r.width()) * r.height()
                    < qint64(src.width()) * src.height() * 96 / 100)
                    scored.append({r, quality});
            }
        }
    }

    if (qEnvironmentVariableIsSet("UNISIC_DET_DEBUG"))
        qInfo() << "det: raw hypotheses" << scored.size();

    // 7. Drop near-duplicates (IoU > 0.75). Priority is QUALITY, not size:
    //    higher-scored (real-bordered, well-covered) hypotheses survive and
    //    absorb their sloppier overlaps; ties go to the tighter rect.
    std::sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.q != b.q)
            return a.q > b.q;
        return qint64(a.r.width()) * a.r.height() < qint64(b.r.width()) * b.r.height();
    });
    if (scored.size() > 1500)
        scored.resize(1500); // O(n^2) dedup below — the tail is lowest-quality anyway
    QVector<QRect> merged;
    for (const Scored &sc : std::as_const(scored)) {
        const QRect &r = sc.r;
        bool dup = false;
        for (const QRect &m : std::as_const(merged)) {
            const QRect inter = r.intersected(m);
            if (inter.isEmpty()) continue;
            const double ia = double(inter.width()) * inter.height();
            const double ua = double(r.width()) * r.height()
                              + double(m.width()) * m.height() - ia;
            if (ua > 0 && ia / ua > 0.75) { dup = true; break; }
        }
        if (!dup)
            merged.append(r);
        if (merged.size() >= 400)
            break; // hover does a linear scan per move — keep it bounded
    }

    // 8. The whole image is always the outermost candidate: scrolling the
    //    nesting level up ends at "this entire screen".
    merged.append(QRect(0, 0, src.width(), src.height()));

    std::sort(merged.begin(), merged.end(), [](const QRect &a, const QRect &b) {
        return qint64(a.width()) * a.height() < qint64(b.width()) * b.height();
    });
    return merged;
}

// ------------------------------------------------------------- segmentation

namespace {

struct Rgb { float r = 0, g = 0, b = 0; };

inline Rgb rgbAt(const QImage &img, int x, int y)
{
    const QRgb px = reinterpret_cast<const QRgb *>(img.constScanLine(y))[x];
    return {float(qRed(px)), float(qGreen(px)), float(qBlue(px))};
}

inline float dist2(const Rgb &a, const Rgb &b)
{
    const float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

// Plain Lloyd k-means over a sample list (k capped by sample count). The
// samples are screenshot colors, so a handful of iterations converges.
QVector<Rgb> kmeans(const QVector<Rgb> &samples, int k, int iters = 8)
{
    if (samples.isEmpty())
        return {};
    k = std::min<qsizetype>(k, samples.size());
    QVector<Rgb> centers(k);
    for (int i = 0; i < k; ++i)
        centers[i] = samples[qsizetype(i) * (samples.size() - 1) / std::max(1, k - 1)];
    QVector<int> assign(samples.size(), 0);
    for (int it = 0; it < iters; ++it) {
        bool moved = false;
        for (qsizetype i = 0; i < samples.size(); ++i) {
            int best = 0;
            float bd = dist2(samples[i], centers[0]);
            for (int c = 1; c < k; ++c) {
                const float d = dist2(samples[i], centers[c]);
                if (d < bd) { bd = d; best = c; }
            }
            if (assign[i] != best) { assign[i] = best; moved = true; }
        }
        QVector<Rgb> sum(k);
        QVector<int> cnt(k, 0);
        for (qsizetype i = 0; i < samples.size(); ++i) {
            Rgb &s = sum[assign[i]];
            const Rgb &p = samples[i];
            s.r += p.r; s.g += p.g; s.b += p.b;
            ++cnt[assign[i]];
        }
        for (int c = 0; c < k; ++c)
            if (cnt[c])
                centers[c] = {sum[c].r / cnt[c], sum[c].g / cnt[c], sum[c].b / cnt[c]};
        if (!moved)
            break;
    }
    return centers;
}

inline float minDist2(const Rgb &p, const QVector<Rgb> &centers)
{
    float best = std::numeric_limits<float>::max();
    for (const Rgb &c : centers)
        best = std::min(best, dist2(p, c));
    return best;
}

// Collect at most `cap` label-matching pixels, striding uniformly.
QVector<Rgb> sampleLabeled(const QImage &img, const QVector<quint8> &label,
                           quint8 want, int cap)
{
    const int w = img.width(), h = img.height();
    int count = 0;
    for (quint8 l : label)
        count += (l == want);
    if (!count)
        return {};
    const int stride = std::max(1, count / cap);
    QVector<Rgb> out;
    out.reserve(std::min(count, cap) + 8);
    int seen = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (label[y * w + x] != want)
                continue;
            if (seen++ % stride == 0)
                out.append(rgbAt(img, x, y));
        }
    return out;
}

} // namespace

QImage ObjectDetector::segment(const QImage &src, const QRect &regionIn)
{
    const QRect region = regionIn.intersected(src.rect());
    if (src.isNull() || region.width() < 12 || region.height() < 12)
        return {};

    // Work on a downscaled copy; the mask is upscaled back at the end. The
    // full-resolution crop lives in a nested scope so it is released before
    // the multi-pass segmentation — it used to pin a select-all-at-4K ~33 MB
    // buffer for the whole hundreds-of-ms run (per concurrent nudge, too).
    // When scale == 1.0, `small` shares crop's data and keeps it alive anyway.
    QImage small;
    {
        QImage crop = src.copy(region).convertToFormat(QImage::Format_RGB32);
        const int longSide = std::max(crop.width(), crop.height());
        const double scale = longSide > 384 ? 384.0 / longSide : 1.0;
        small = scale < 1.0
                    ? crop.scaled(std::max(12, int(std::lround(crop.width() * scale))),
                                  std::max(12, int(std::lround(crop.height() * scale))),
                                  Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    : crop;
    }
    const int w = small.width(), h = small.height();
    const int n = w * h;

    // Priors: the border ring is background (the rectangle surrounds the
    // object); a small center box seeds the foreground model.
    const int ring = std::max(2, std::min(w, h) / 24);
    auto inRing = [&](int x, int y) {
        return x < ring || y < ring || x >= w - ring || y >= h - ring;
    };
    QVector<Rgb> bgSamples, fgSamples;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (inRing(x, y))
                bgSamples.append(rgbAt(small, x, y));
    for (int y = h * 3 / 8; y < h * 5 / 8; ++y)
        for (int x = w * 3 / 8; x < w * 5 / 8; ++x)
            fgSamples.append(rgbAt(small, x, y));

    QVector<Rgb> bgC = kmeans(bgSamples, 5);
    QVector<Rgb> fgC = kmeans(fgSamples, 5);
    if (bgC.isEmpty() || fgC.isEmpty())
        return {};

    // GrabCut-style refinement: classify by nearest color model, smooth with a
    // 3x3 majority vote, then re-fit both models from the current labels. The
    // border ring stays pinned to background throughout.
    QVector<quint8> label(n, 0), next(n, 0);
    for (int round = 0; round < 3; ++round) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (inRing(x, y)) { label[y * w + x] = 0; continue; }
                const Rgb p = rgbAt(small, x, y);
                label[y * w + x] = minDist2(p, fgC) < minDist2(p, bgC) ? 1 : 0;
            }
        // Majority smooth (spatial coherence stand-in for the graph cut).
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int votes = 0, total = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                            continue;
                        votes += label[ny * w + nx];
                        ++total;
                    }
                next[y * w + x] = inRing(x, y) ? 0 : (votes * 2 > total ? 1 : 0);
            }
        label.swap(next);
        if (round == 2)
            break;
        QVector<Rgb> fgNow = sampleLabeled(small, label, 1, 4000);
        QVector<Rgb> bgNow = sampleLabeled(small, label, 0, 4000);
        if (fgNow.isEmpty() || bgNow.isEmpty())
            return {};
        fgC = kmeans(fgNow, 5);
        bgC = kmeans(bgNow, 5);
    }

    // Connectivity cleanup: keep only the largest foreground component.
    QVector<int> comp(n, -1);
    int compCount = 0, bestComp = -1, bestSize = 0;
    {
        QStack<int> stack;
        for (int i = 0; i < n; ++i) {
            if (label[i] != 1 || comp[i] != -1)
                continue;
            int size = 0;
            stack.push(i);
            comp[i] = compCount;
            while (!stack.isEmpty()) {
                const int cur = stack.pop();
                ++size;
                const int cx = cur % w, cy = cur / w;
                const int nbs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto &d : nbs) {
                    const int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const int ni = ny * w + nx;
                    if (label[ni] == 1 && comp[ni] == -1) {
                        comp[ni] = compCount;
                        stack.push(ni);
                    }
                }
            }
            if (size > bestSize) { bestSize = size; bestComp = compCount; }
            ++compCount;
        }
    }
    if (bestComp < 0)
        return {};
    for (int i = 0; i < n; ++i)
        label[i] = (label[i] == 1 && comp[i] == bestComp) ? 1 : 0;

    // Fill small enclosed background holes (< 2% of the region): flood the
    // background from the border; unreached small pockets become foreground.
    // Larger pockets are kept as genuine see-through gaps (e.g. arm vs torso).
    {
        QVector<quint8> reach(n, 0);
        QStack<int> stack;
        for (int x = 0; x < w; ++x) {
            if (!label[x]) { reach[x] = 1; stack.push(x); }
            const int b = (h - 1) * w + x;
            if (!label[b] && !reach[b]) { reach[b] = 1; stack.push(b); }
        }
        for (int y = 0; y < h; ++y) {
            const int l = y * w;
            if (!label[l] && !reach[l]) { reach[l] = 1; stack.push(l); }
            const int r = y * w + w - 1;
            if (!label[r] && !reach[r]) { reach[r] = 1; stack.push(r); }
        }
        while (!stack.isEmpty()) {
            const int cur = stack.pop();
            const int cx = cur % w, cy = cur / w;
            const int nbs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto &d : nbs) {
                const int nx = cx + d[0], ny = cy + d[1];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const int ni = ny * w + nx;
                if (!label[ni] && !reach[ni]) { reach[ni] = 1; stack.push(ni); }
            }
        }
        // Group unreached background into components; small ones become fg.
        QVector<int> holeComp(n, -1);
        int holeCount = 0;
        QVector<int> holeSizes;
        for (int i = 0; i < n; ++i) {
            if (label[i] || reach[i] || holeComp[i] != -1)
                continue;
            int size = 0;
            stack.push(i);
            holeComp[i] = holeCount;
            while (!stack.isEmpty()) {
                const int cur = stack.pop();
                ++size;
                const int cx = cur % w, cy = cur / w;
                const int nbs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto &d : nbs) {
                    const int nx = cx + d[0], ny = cy + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const int ni = ny * w + nx;
                    if (!label[ni] && !reach[ni] && holeComp[ni] == -1) {
                        holeComp[ni] = holeCount;
                        stack.push(ni);
                    }
                }
            }
            holeSizes.append(size);
            ++holeCount;
        }
        const int holeCap = std::max(4, n / 50);
        for (int i = 0; i < n; ++i)
            if (holeComp[i] != -1 && holeSizes[holeComp[i]] <= holeCap)
                label[i] = 1;
    }

    // Degenerate results (all-background noise or near-full rectangle) are
    // worse than the plain crop — bail and let the caller fall back.
    int fgCount = 0;
    for (quint8 l : label)
        fgCount += l;
    if (fgCount < n / 100 || fgCount > n * 97 / 100)
        return {};

    // Binary mask -> Grayscale8, one 3x3 box blur to feather the edge, then
    // upscale to the full region size (smooth = another feathering step).
    QImage mask(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar *line = mask.scanLine(y);
        for (int x = 0; x < w; ++x)
            line[x] = label[y * w + x] ? 255 : 0;
    }
    QImage blurred(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar *out = blurred.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int sum = 0, cnt = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h)
                    continue;
                const uchar *in = mask.constScanLine(ny);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= w)
                        continue;
                    sum += in[nx];
                    ++cnt;
                }
            }
            out[x] = uchar(sum / cnt);
        }
    }
    return blurred.scaled(region.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

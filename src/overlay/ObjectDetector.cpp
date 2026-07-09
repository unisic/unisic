#include "ObjectDetector.h"
#include <QImage>
#include <QStack>
#include <algorithm>
#include <cmath>
#include <limits>

QVector<QRect> ObjectDetector::detect(const QImage &src)
{
    QVector<QRect> result;
    if (src.isNull() || src.width() < 16 || src.height() < 16)
        return result;

    // 1. Downscale for speed; remember the factor to map rects back.
    const int maxDim = 640;
    const int longest = std::max(src.width(), src.height());
    const double scale = longest > maxDim ? double(longest) / maxDim : 1.0;
    const int sw = std::max(3, int(std::lround(src.width() / scale)));
    const int sh = std::max(3, int(std::lround(src.height() / scale)));
    const QImage gray = src.scaled(sw, sh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width();
    const int h = gray.height();
    if (w < 3 || h < 3)
        return result;

    auto px = [&](int x, int y) -> int { return gray.constScanLine(y)[x]; };

    // 2. Sobel gradient magnitude + running mean/stddev for an adaptive threshold.
    QVector<int> mag(w * h, 0);
    double sum = 0.0, sumSq = 0.0;
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const int gx = -px(x-1,y-1) - 2*px(x-1,y) - px(x-1,y+1)
                           + px(x+1,y-1) + 2*px(x+1,y) + px(x+1,y+1);
            const int gy = -px(x-1,y-1) - 2*px(x,y-1) - px(x+1,y-1)
                           + px(x-1,y+1) + 2*px(x,y+1) + px(x+1,y+1);
            const int m = std::abs(gx) + std::abs(gy);
            mag[y*w + x] = m;
            sum += m;
            sumSq += double(m) * m;
        }
    }
    const double n = double((w - 2) * (h - 2));
    const double mean = sum / n;
    const double var = std::max(0.0, sumSq / n - mean * mean);
    const int thresh = int(mean + std::sqrt(var));

    // 3. Binary edge map, then a single 3x3 dilation to bridge 1px gaps so an
    //    object's outline reads as one connected component.
    QVector<uchar> edge(w * h, 0);
    for (int i = 0; i < w * h; ++i)
        edge[i] = mag[i] > thresh ? 1 : 0;
    QVector<uchar> dil(w * h, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!edge[y*w + x]) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h)
                        dil[ny*w + nx] = 1;
                }
        }

    // 4. Connected components (8-connectivity); a component's bounding box
    //    approximates the object it outlines.
    QVector<uchar> seen(w * h, 0);
    QStack<int> stack;
    const int minSide = 24; // downscaled px
    for (int start = 0; start < w * h; ++start) {
        if (!dil[start] || seen[start])
            continue;
        int minx = w, miny = h, maxx = 0, maxy = 0;
        stack.push(start);
        seen[start] = 1;
        while (!stack.isEmpty()) {
            const int p = stack.pop();
            const int x = p % w, y = p / w;
            minx = std::min(minx, x); maxx = std::max(maxx, x);
            miny = std::min(miny, y); maxy = std::max(maxy, y);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    const int np = ny*w + nx;
                    if (dil[np] && !seen[np]) {
                        seen[np] = 1;
                        stack.push(np);
                    }
                }
        }
        const int bw = maxx - minx + 1, bh = maxy - miny + 1;
        if (bw < minSide || bh < minSide) continue;
        if (bw > w * 0.97 && bh > h * 0.97) continue; // whole screen, not useful
        QRect r(int(minx * scale), int(miny * scale), int(bw * scale), int(bh * scale));
        r = r.intersected(QRect(0, 0, src.width(), src.height()));
        if (r.width() >= 8 && r.height() >= 8)
            result.append(r);
    }

    // 5. Drop near-duplicates (IoU > 0.8), keeping the larger of each pair.
    std::sort(result.begin(), result.end(), [](const QRect &a, const QRect &b) {
        return qint64(a.width()) * a.height() > qint64(b.width()) * b.height();
    });
    QVector<QRect> merged;
    for (const QRect &r : std::as_const(result)) {
        bool dup = false;
        for (const QRect &m : std::as_const(merged)) {
            const QRect inter = r.intersected(m);
            if (inter.isEmpty()) continue;
            const double ia = double(inter.width()) * inter.height();
            const double ua = double(r.width()) * r.height()
                              + double(m.width()) * m.height() - ia;
            if (ua > 0 && ia / ua > 0.8) { dup = true; break; }
        }
        if (!dup)
            merged.append(r);
    }
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

#include "ObjectDetector.h"
#include <QImage>
#include <QStack>
#include <algorithm>
#include <cmath>

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

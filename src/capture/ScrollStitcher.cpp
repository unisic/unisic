#include "ScrollStitcher.h"

#include <cmath>
#include <algorithm>
#include <QPainter>

ScrollStitcher::ScrollStitcher() = default;

void ScrollStitcher::reset()
{
    m_canvas = QImage();
    m_prevFrame = QImage();
    m_prevProfiles.clear();
    m_actualWidth = 0;
    m_actualHeight = 0;
    m_frameCount = 0;
    m_leftMargin = 0;
    m_rightMargin = 0;
}

QVector<ScrollStitcher::RowProfile> ScrollStitcher::computeProfiles(const QImage &img,
                                                                   int leftMargin,
                                                                   int rightMargin)
{
    const int w = img.width();
    const int h = img.height();
    if (w <= 0 || h <= 0)
        return {};

    const int effW = std::max(16, w - leftMargin - rightMargin);
    const int startX = (w - leftMargin - rightMargin >= 16) ? leftMargin : 0;

    QVector<RowProfile> profiles(h);

    for (int y = 0; y < h; ++y) {
        RowProfile &p = profiles[y];
        int sum = 0;

        for (int k = 0; k < 16; ++k) {
            const int x = startX + (k * effW) / 16;
            const QRgb pixel = img.pixel(x, y);
            const int r = qRed(pixel);
            const int g = qGreen(pixel);
            const int b = qBlue(pixel);
            const quint8 lum = static_cast<quint8>((77 * r + 150 * g + 29 * b) >> 8);
            p.samples[k] = lum;
            sum += lum;
        }
        p.mean = static_cast<double>(sum) / 16.0;
    }

    return profiles;
}

int ScrollStitcher::detectStickyHeader(const QVector<RowProfile> &prev,
                                       const QVector<RowProfile> &curr)
{
    const int h = std::min(prev.size(), curr.size());
    if (h < 40)
        return 0;

    const int maxHeaderRows = h / 3;
    int hdrCount = 0;

    for (int y = 0; y < maxHeaderRows; ++y) {
        if (std::abs(prev[y].mean - curr[y].mean) > 2.0)
            break;

        int sampleDiffSum = 0;
        for (int k = 0; k < 16; ++k)
            sampleDiffSum += std::abs(int(prev[y].samples[k]) - int(curr[y].samples[k]));

        if (sampleDiffSum > 16 * 3) // average error > 3 per sample
            break;

        ++hdrCount;
    }

    // Only count as sticky header if there are at least 8 contiguous stationary rows
    return (hdrCount >= 8) ? hdrCount : 0;
}

int ScrollStitcher::detectVerticalShift(const QImage &prev, const QImage &curr,
                                       Direction dir, double *confidenceOut)
{
    if (confidenceOut)
        *confidenceOut = 0.0;

    if (prev.isNull() || curr.isNull() || prev.size() != curr.size())
        return 0;

    const int w = prev.width();
    const int h = prev.height();
    if (w < 10 || h < 20)
        return 0;

    // Ignore margins (scrollbar on right, padding on left)
    const int leftMargin = std::clamp(w / 16, 4, 24);
    const int rightMargin = std::clamp(w / 10, 16, 48);

    const auto prevProf = computeProfiles(prev, leftMargin, rightMargin);
    const auto currProf = computeProfiles(curr, leftMargin, rightMargin);

    // Fast check: identical frames (no scrolling)
    double totalDiff = 0.0;
    for (int y = 0; y < h; ++y)
        totalDiff += std::abs(prevProf[y].mean - currProf[y].mean);
    if ((totalDiff / h) < 0.6) {
        if (confidenceOut)
            *confidenceOut = 1.0;
        return 0;
    }

    const int stickyHdr = detectStickyHeader(prevProf, currProf);
    const int minOverlap = std::max(20, h / 8);
    const int maxShift = h - minOverlap - stickyHdr;

    if (maxShift <= 1)
        return 0;

    auto testDirection = [&](bool testDown, int &bestShift, double &bestError, double &bestVar) {
        bestShift = 0;
        bestError = 999999.0;
        bestVar = 0.0;

        // Step 1: Coarse 1D profile search
        struct Candidate {
            int d = 0;
            double score = 999999.0;
        };
        QVector<Candidate> candidates;

        for (int d = 1; d <= maxShift; ++d) {
            const int overlap = h - d - stickyHdr;
            if (overlap < minOverlap)
                break;

            double diff1D = 0.0;
            int count = 0;

            if (testDown) {
                // Downward scroll: content moves UP.
                // Row y in curr matches row y + d in prev.
                for (int y = stickyHdr; y < stickyHdr + overlap; y += 2) {
                    diff1D += std::abs(currProf[y].mean - prevProf[y + d].mean);
                    ++count;
                }
            } else {
                // Upward scroll: content moves DOWN.
                // Row y + d in curr matches row y in prev.
                for (int y = stickyHdr; y < stickyHdr + overlap; y += 2) {
                    diff1D += std::abs(currProf[y + d].mean - prevProf[y].mean);
                    ++count;
                }
            }

            const double avg1D = count > 0 ? (diff1D / count) : 999999.0;
            candidates.append({d, avg1D});
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
            return a.score < b.score;
        });

        // Step 2: Fine 2D multi-sample verification on top candidates
        const int numToCheck = std::min<int>(5, candidates.size());
        QVector<int> fineShifts;
        for (int i = 0; i < numToCheck; ++i) {
            const int baseD = candidates[i].d;
            for (int delta = -2; delta <= 2; ++delta) {
                const int d = baseD + delta;
                if (d >= 1 && d <= maxShift && !fineShifts.contains(d))
                    fineShifts.append(d);
            }
        }

        for (int d : fineShifts) {
            const int overlap = h - d - stickyHdr;
            if (overlap < minOverlap)
                continue;

            double errSum = 0.0;
            double varSum = 0.0;
            double varSumSq = 0.0;
            int sampleCount = 0;

            for (int y = stickyHdr; y < stickyHdr + overlap; ++y) {
                const auto &pRow = testDown ? prevProf[y + d] : prevProf[y];
                const auto &cRow = testDown ? currProf[y] : currProf[y + d];

                varSum += pRow.mean;
                varSumSq += pRow.mean * pRow.mean;

                for (int k = 0; k < 16; ++k) {
                    errSum += std::abs(int(cRow.samples[k]) - int(pRow.samples[k]));
                    ++sampleCount;
                }
            }

            const double avgErr = sampleCount > 0 ? (errSum / sampleCount) : 999999.0;
            const double mean = overlap > 0 ? (varSum / overlap) : 0.0;
            const double variance = overlap > 0 ? std::max(0.0, (varSumSq / overlap) - (mean * mean)) : 0.0;

            if (avgErr < bestError) {
                bestError = avgErr;
                bestShift = d;
                bestVar = variance;
            }
        }
    };

    int bestDownD = 0;
    double bestDownErr = 999999.0;
    double bestDownVar = 0.0;

    int bestUpD = 0;
    double bestUpErr = 999999.0;
    double bestUpVar = 0.0;

    if (dir == Direction::Down || dir == Direction::Auto)
        testDirection(true, bestDownD, bestDownErr, bestDownVar);

    if (dir == Direction::Up || (dir == Direction::Auto && bestDownErr > 8.0))
        testDirection(false, bestUpD, bestUpErr, bestUpVar);

    int finalD = 0;
    double finalErr = 999999.0;
    double finalVar = 0.0;

    if (dir == Direction::Up) {
        finalD = -bestUpD;
        finalErr = bestUpErr;
        finalVar = bestUpVar;
    } else if (dir == Direction::Down) {
        finalD = bestDownD;
        finalErr = bestDownErr;
        finalVar = bestDownVar;
    } else { // Auto
        if (bestDownErr <= bestUpErr && bestDownErr < 999999.0) {
            finalD = bestDownD;
            finalErr = bestDownErr;
            finalVar = bestDownVar;
        } else if (bestUpD > 0) {
            finalD = -bestUpD;
            finalErr = bestUpErr;
            finalVar = bestUpVar;
        }
    }

    // Require sufficient variation in overlap to avoid matching blank/flat background
    if (finalVar < 3.0) {
        if (confidenceOut)
            *confidenceOut = 0.0;
        return 0;
    }

    // Error threshold: average sample error must be within acceptable range
    if (finalErr <= 14.0) {
        const double conf = std::clamp(1.0 - (finalErr / 14.0) * 0.5, 0.5, 1.0);
        if (confidenceOut)
            *confidenceOut = conf;
        return finalD;
    }

    return 0;
}

bool ScrollStitcher::addFrame(const QImage &frame)
{
    if (frame.isNull() || frame.width() < 10 || frame.height() < 10)
        return false;

    // Ensure compatible format
    QImage f = frame;
    if (f.format() != QImage::Format_RGB32 && f.format() != QImage::Format_ARGB32_Premultiplied)
        f = f.convertToFormat(QImage::Format_RGB32);

    if (m_frameCount == 0) {
        m_actualWidth = f.width();
        m_actualHeight = f.height();
        m_leftMargin = std::clamp(m_actualWidth / 16, 4, 24);
        m_rightMargin = std::clamp(m_actualWidth / 10, 16, 48);

        // Pre-allocate canvas with headroom
        const int cap = std::min(m_actualHeight + 4000, kMaxStitchedHeight);
        m_canvas = QImage(m_actualWidth, cap, f.format());
        m_canvas.fill(Qt::transparent);

        QPainter p(&m_canvas);
        p.drawImage(0, 0, f);
        p.end();

        m_prevFrame = f;
        m_prevProfiles = computeProfiles(f, m_leftMargin, m_rightMargin);
        m_frameCount = 1;
        return true;
    }

    if (f.width() != m_actualWidth || f.height() != m_prevFrame.height())
        return false;

    double confidence = 0.0;
    const int d = detectVerticalShift(m_prevFrame, f, m_direction, &confidence);

    if (confidence < 0.5 || d == 0)
        return false;

    if (d > 0) {
        // Downward scroll: new slice at bottom of current frame
        int sliceH = d;
        if (m_actualHeight + sliceH > kMaxStitchedHeight)
            sliceH = kMaxStitchedHeight - m_actualHeight;

        if (sliceH <= 0)
            return false;

        // Reallocate canvas if needed
        if (m_actualHeight + sliceH > m_canvas.height()) {
            const int newCap = std::min(kMaxStitchedHeight,
                                        std::max(m_actualHeight + sliceH + 4000, m_canvas.height() * 3 / 2));
            QImage bigger(m_actualWidth, newCap, m_canvas.format());
            bigger.fill(Qt::transparent);
            QPainter p(&bigger);
            p.drawImage(0, 0, m_canvas.copy(0, 0, m_actualWidth, m_actualHeight));
            p.end();
            m_canvas = bigger;
        }

        // Copy newly revealed slice
        const int rowBytes = m_actualWidth * 4;
        const int srcStartY = f.height() - sliceH;
        for (int r = 0; r < sliceH; ++r) {
            const uchar *srcRow = f.constScanLine(srcStartY + r);
            uchar *dstRow = m_canvas.scanLine(m_actualHeight + r);
            std::memcpy(dstRow, srcRow, rowBytes);
        }

        m_actualHeight += sliceH;
        m_prevFrame = f;
        m_prevProfiles = computeProfiles(f, m_leftMargin, m_rightMargin);
        ++m_frameCount;
        return true;
    } else {
        // Upward scroll: new slice at top of current frame
        int sliceH = -d;
        if (m_actualHeight + sliceH > kMaxStitchedHeight)
            sliceH = kMaxStitchedHeight - m_actualHeight;

        if (sliceH <= 0)
            return false;

        const int newH = m_actualHeight + sliceH;
        const int newCap = std::min(kMaxStitchedHeight,
                                    std::max(newH + 4000, m_canvas.height() * 3 / 2));
        QImage bigger(m_actualWidth, newCap, m_canvas.format());
        bigger.fill(Qt::transparent);

        // Prepend new slice at top
        const int rowBytes = m_actualWidth * 4;
        for (int r = 0; r < sliceH; ++r) {
            const uchar *srcRow = f.constScanLine(r);
            uchar *dstRow = bigger.scanLine(r);
            std::memcpy(dstRow, srcRow, rowBytes);
        }

        // Shift existing content down
        for (int r = 0; r < m_actualHeight; ++r) {
            const uchar *srcRow = m_canvas.constScanLine(r);
            uchar *dstRow = bigger.scanLine(sliceH + r);
            std::memcpy(dstRow, srcRow, rowBytes);
        }

        m_canvas = bigger;
        m_actualHeight = newH;
        m_prevFrame = f;
        m_prevProfiles = computeProfiles(f, m_leftMargin, m_rightMargin);
        ++m_frameCount;
        return true;
    }
}

QImage ScrollStitcher::stitchedImage() const
{
    if (m_frameCount == 0 || m_actualWidth <= 0 || m_actualHeight <= 0)
        return {};

    return m_canvas.copy(0, 0, m_actualWidth, m_actualHeight);
}

QImage ScrollStitcher::previewThumbnail(int maxW, int maxH) const
{
    if (m_frameCount == 0 || m_actualWidth <= 0 || m_actualHeight <= 0)
        return {};

    const QImage current = stitchedImage();
    return current.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

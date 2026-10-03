#include <algorithm>
#include <QTest>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include "capture/ScrollStitcher.h"

using R = ScrollStitcher::Result;

class ScrollStitchTest : public QObject
{
    Q_OBJECT

private slots:
    void firstFrameStarts();
    void identicalFrameIsUnchanged();
    void scrollDownIsByteExact();
    void irregularStepsAreByteExact();
    void stickyHeaderAndFooterAppearOnce();
    void staticSidebarAndScrollbarDoNotBreakAlignment();
    void changingStickySidebarDoesNotBreakAlignment();
    void scrollingUpExtendsTheTop();
    void scrollingBackAndForthDoesNotDuplicate();
    void tooFastScrollIsUnmatched();
    void localAnimationIsUnchanged();
    void scatteredSmallChangesAreUnchanged();
    void hoverCardOverTheViewDoesNotBlockTheShift();
    void veryWideFrameStillScrolls();
    void textSmoothingChangeIsNotAChange();
    void lateLoadedContentTakesNewerPixels();
    void replacesBaseBeforeFirstScroll();
    void sizeCapStopsGrowth();
    void randomWalkIsByteExact();
    void largeFrameCost();

private:
    static QImage document(int w, int h);
    static QImage subpixel(const QImage &gray);
};

// Text-like content: dense lines with real glyphs, blank gaps and repeated
// identical lines, so a shift that is off by a row or matches a repeated line
// would show up as a pixel difference.
QImage ScrollStitchTest::document(int w, int h)
{
    QImage doc(w, h, QImage::Format_RGB32);
    doc.fill(QColor(250, 250, 250));
    QPainter p(&doc);
    QFont f = p.font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(QColor(20, 20, 20));
    int line = 0;
    for (int y = 16; y < h - 4; y += 18, ++line) {
        if (line % 7 == 3)
            continue;                                   // blank line
        if (line % 5 == 0) {
            p.drawText(12, y, QStringLiteral("}"));    // repeated identical line
            continue;
        }
        p.drawText(12, y, QStringLiteral("%1: the quick brown fox %2 jumps over lazy dog")
                              .arg(line).arg(line * 37 % 1000));
    }
    p.end();
    return doc;
}

// The same picture with coloured fringes on every edge, the way a browser draws
// text with sub-pixel smoothing instead of the plain gray one: red and blue
// are sampled a third of a pixel to either side of green.
QImage ScrollStitchTest::subpixel(const QImage &gray)
{
    const QImage src = gray.convertToFormat(QImage::Format_RGB32);
    QImage out(src.size(), QImage::Format_RGB32);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb *p = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *o = reinterpret_cast<QRgb *>(out.scanLine(y));
        const int last = src.width() - 1;
        for (int x = 0; x <= last; ++x) {
            const QRgb l = p[std::max(x - 1, 0)], r = p[std::min(x + 1, last)];
            o[x] = qRgb((2 * qRed(p[x]) + qRed(l)) / 3, qGreen(p[x]), (2 * qBlue(p[x]) + qBlue(r)) / 3);
        }
    }
    return out;
}

void ScrollStitchTest::firstFrameStarts()
{
    const QImage doc = document(300, 600);
    ScrollStitcher s;
    QVERIFY(s.isEmpty());
    QCOMPARE(s.addFrame(doc.copy(0, 0, 300, 200)), R::Started);
    QCOMPARE(s.frameCount(), 1);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 300, 200));
    const QImage thumb = s.previewThumbnail(100, 100);
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 100 && thumb.height() <= 100);
}

void ScrollStitchTest::identicalFrameIsUnchanged()
{
    const QImage doc = document(300, 600);
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 300, 200));
    QCOMPARE(s.addFrame(doc.copy(0, 0, 300, 200)), R::Unchanged);
    QCOMPARE(s.stitchedHeight(), 200);
}

void ScrollStitchTest::scrollDownIsByteExact()
{
    const QImage doc = document(360, 800);
    const auto sh = ScrollStitcher::findShift(doc.copy(0, 0, 360, 250), doc.copy(0, 50, 360, 250));
    QVERIFY(sh.ok);
    QCOMPARE(sh.dy, 50);

    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 360, 250));
    QCOMPARE(s.addFrame(doc.copy(0, 50, 360, 250)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 360, 300));
}

void ScrollStitchTest::irregularStepsAreByteExact()
{
    const QImage doc = document(420, 3000);
    const int vh = 300;
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 420, vh));
    // 1px steps (smooth scrolling), a wheel notch, and near the overlap limit.
    const int steps[] = {1, 1, 3, 54, 120, 7, 230, 18, 1, 90, 250, 2, 66};
    int y = 0;
    for (int d : steps) {
        y += d;
        QCOMPARE(s.addFrame(doc.copy(0, y, 420, vh)), R::Stitched);
    }
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 420, y + vh));
}

void ScrollStitchTest::stickyHeaderAndFooterAppearOnce()
{
    const int w = 350, vh = 260, hdr = 36, ftr = 28;
    const QImage doc = document(w, 1200);
    auto frame = [&](int y) {
        QImage f(w, vh, QImage::Format_RGB32);
        QPainter p(&f);
        p.drawImage(0, hdr, doc.copy(0, y, w, vh - hdr - ftr));
        p.fillRect(0, 0, w, hdr, QColor(30, 30, 60));
        p.setPen(Qt::white);
        p.drawText(15, 22, QStringLiteral("Fixed Top Navigation Bar"));
        p.fillRect(0, vh - ftr, w, ftr, QColor(60, 30, 30));
        p.drawText(15, vh - 9, QStringLiteral("Cookie banner footer"));
        return f;
    };
    ScrollStitcher s;
    s.addFrame(frame(0));
    int y = 0;
    for (int d : {40, 75, 12, 120, 60}) {
        y += d;
        QCOMPARE(s.addFrame(frame(y)), R::Stitched);
    }
    const int body = y + vh - hdr - ftr;
    QImage expect(w, hdr + body + ftr, QImage::Format_RGB32);
    QPainter p(&expect);
    p.drawImage(0, 0, frame(y).copy(0, 0, w, hdr));
    p.drawImage(0, hdr, doc.copy(0, 0, w, body));
    p.drawImage(0, hdr + body, frame(y).copy(0, vh - ftr, w, ftr));
    p.end();
    QCOMPARE(s.stitchedImage(), expect);
}

void ScrollStitchTest::staticSidebarAndScrollbarDoNotBreakAlignment()
{
    const int w = 400, vh = 280, side = 90, bar = 12;
    const QImage doc = document(w, 1500);
    auto frame = [&](int y) {
        QImage f = doc.copy(0, y, w, vh);
        QPainter p(&f);
        p.fillRect(0, 0, side, vh, QColor(40, 44, 52));
        p.setPen(Qt::white);
        for (int i = 0; i < 8; ++i)
            p.drawText(8, 24 + i * 30, QStringLiteral("Nav %1").arg(i));
        p.fillRect(w - bar, 0, bar, vh, QColor(220, 220, 220));
        p.fillRect(w - bar + 2, 10 + y / 6, bar - 4, 50, QColor(90, 90, 90));
        return f;
    };
    ScrollStitcher s;
    s.addFrame(frame(0));
    int y = 0;
    for (int d : {30, 80, 5, 140}) {
        y += d;
        QCOMPARE(s.addFrame(frame(y)), R::Stitched);
    }
    const QImage got = s.stitchedImage();
    QCOMPARE(got.height(), y + vh);
    // The scrolling column is the document, row for row.
    QCOMPARE(got.copy(side, 0, w - side - bar, got.height()),
             doc.copy(side, 0, w - side - bar, y + vh));
}

// A sticky contents list beside the article (Wikipedia): it does not scroll
// with the page, yet the highlight of the current section moves between
// frames, so its columns join the compared ones and every row with list text
// would mismatch at the true shift. They leave the comparison because the list
// stays put: only a few of its rows differ in place, unlike the article's.
void ScrollStitchTest::changingStickySidebarDoesNotBreakAlignment()
{
    const int side = 110, gap = 20, aw = 390, w = side + gap + aw, vh = 300;
    const QImage doc = document(aw, 1800);
    auto frame = [&](int y) {
        QImage f(w, vh, QImage::Format_RGB32);
        f.fill(Qt::white);
        QPainter p(&f);
        p.drawImage(side + gap, 0, doc.copy(0, y, aw, vh));
        p.fillRect(4, 10 + (y / 90 % 8) * 30 - 14, side - 8, 22, QColor(200, 220, 255));
        p.setPen(Qt::black);
        for (int i = 0; i < 8; ++i)
            p.drawText(8, 10 + i * 30, QStringLiteral("Section %1").arg(i));
        return f;
    };
    ScrollStitcher s;
    s.addFrame(frame(0));
    int y = 0;
    for (int d : {40, 75, 12, 120, 60, 90}) {
        y += d;
        QVERIFY2(s.addFrame(frame(y)) == R::Stitched, qPrintable(QStringLiteral("y %1").arg(y)));
    }
    const QImage got = s.stitchedImage();
    QCOMPARE(got.height(), y + vh);
    QCOMPARE(got.copy(side + gap, 0, aw, got.height()), doc.copy(0, 0, aw, y + vh));
}

void ScrollStitchTest::scrollingUpExtendsTheTop()
{
    const QImage doc = document(350, 1400);
    const int vh = 250;
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 900, 350, vh));
    int y = 900;
    for (int d : {40, 3, 150, 77}) {
        y -= d;
        QCOMPARE(s.addFrame(doc.copy(0, y, 350, vh)), R::Stitched);
    }
    QCOMPARE(s.stitchedImage(), doc.copy(0, y, 350, 900 + vh - y));
}

void ScrollStitchTest::scrollingBackAndForthDoesNotDuplicate()
{
    const QImage doc = document(350, 2000);
    const int vh = 250;
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 300, 350, vh));
    int lo = 300, hi = 300;
    for (int y : {400, 520, 450, 330, 200, 260, 420, 600, 700}) {
        s.addFrame(doc.copy(0, y, 350, vh));
        lo = std::min(lo, y);
        hi = std::max(hi, y);
    }
    QCOMPARE(s.stitchedImage(), doc.copy(0, lo, 350, hi + vh - lo));
}

void ScrollStitchTest::tooFastScrollIsUnmatched()
{
    const QImage doc = document(350, 2000);
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 350, 250));
    QCOMPARE(s.addFrame(doc.copy(0, 40, 350, 250)), R::Stitched);
    // Jumped past the whole viewport: no overlap, nothing may be guessed.
    QCOMPARE(s.addFrame(doc.copy(0, 600, 350, 250)), R::Unmatched);
    QCOMPARE(s.stitchedHeight(), 290);
    // Scrolling back to where the overlap is resumes the capture.
    QCOMPARE(s.addFrame(doc.copy(0, 150, 350, 250)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 350, 400));
}

void ScrollStitchTest::localAnimationIsUnchanged()
{
    const QImage doc = document(350, 800);
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 350, 250));
    s.addFrame(doc.copy(0, 30, 350, 250));
    QImage f = doc.copy(0, 30, 350, 250);
    QPainter p(&f);
    p.fillRect(20, 100, 120, 30, Qt::red);   // spinner / hover / caret
    p.end();
    QCOMPARE(s.addFrame(f), R::Unchanged);
    QCOMPARE(s.stitchedHeight(), 280);
}

// A link underlined under the pointer and a highlighted entry of a contents
// list, far apart: together they span a third of the frame, yet nothing
// scrolled. This must not read as a scroll that found no shift, or a capture
// that is merely paused counts up to "scrolled too far".
void ScrollStitchTest::scatteredSmallChangesAreUnchanged()
{
    const QImage doc = document(350, 1500);
    const int vh = 300;
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 350, vh));
    QCOMPARE(s.addFrame(doc.copy(0, 50, 350, vh)), R::Stitched);
    for (int i = 0; i < 12; ++i) {
        QImage f = doc.copy(0, 50, 350, vh);
        QPainter p(&f);
        p.fillRect(250, 8 + i % 3, 80, 14, QColor(200, 220, 255));   // highlight
        p.fillRect(12, 200, 170, 1, QColor(20, 20, 20));             // underline
        p.end();
        QCOMPARE(s.addFrame(f), R::Unchanged);
    }
    QCOMPARE(s.addFrame(doc.copy(0, 110, 350, vh)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 350, 110 + vh));
}

// The card Wikipedia opens over a link or a footnote hovered by the pointer
// covers a good part of the view for a few frames. On a standing page it is not
// a scroll; during one it is a block of rows that differ and must not make the
// true shift fail, and the rows it covered are repainted once it is gone.
void ScrollStitchTest::hoverCardOverTheViewDoesNotBlockTheShift()
{
    const QImage doc = document(350, 1500);
    const int vh = 300;
    auto withCard = [&](int y) {
        QImage f = doc.copy(0, y, 350, vh);
        QPainter p(&f);
        p.fillRect(30, 90, 280, 72, QColor(235, 235, 250));
        p.setPen(QColor(60, 60, 120));
        p.drawText(40, 110, QStringLiteral("Phelan, William (2012). What Is Sui Generis"));
        p.drawText(40, 130, QStringLiteral("About the European Union? Costly Cooperation"));
        p.end();
        return f;
    };
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 350, vh));
    QCOMPARE(s.addFrame(withCard(0)), R::Unchanged);
    QCOMPARE(s.addFrame(withCard(40)), R::Stitched);
    QCOMPARE(s.addFrame(doc.copy(0, 100, 350, vh)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 350, 100 + vh));
}

// Columns are sampled every w/640 px, so on a region several monitors wide the
// sampled columns of one text block are further apart than any fixed gap.
void ScrollStitchTest::veryWideFrameStillScrolls()
{
    const QImage doc = document(12000, 900);
    const int vh = 250;
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 12000, vh));
    QCOMPARE(s.addFrame(doc.copy(0, 40, 12000, vh)), R::Stitched);
    QCOMPARE(s.addFrame(doc.copy(0, 130, 12000, vh)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 12000, 130 + vh));
}

// Firefox on Wikipedia redraws the text of a block of the page with sub-pixel
// smoothing at one moment and with gray smoothing at the next. Every glyph edge
// then differs between two frames of the same page, which once made a page
// standing still look scrolled with no shift to fit ("scrolled too far"), and a
// real scroll between a frame of one kind and a frame of the other unmatched.
void ScrollStitchTest::textSmoothingChangeIsNotAChange()
{
    const QImage doc = document(350, 1500);
    const int vh = 300;
    ScrollStitcher s;
    s.addFrame(subpixel(doc.copy(0, 0, 350, vh)));
    QCOMPARE(s.addFrame(doc.copy(0, 0, 350, vh)), R::Unchanged);
    QCOMPARE(s.addFrame(doc.copy(0, 3, 350, vh)), R::Stitched);
    QCOMPARE(s.addFrame(subpixel(doc.copy(0, 3, 350, vh))), R::Unchanged);
    QCOMPARE(s.addFrame(subpixel(doc.copy(0, 70, 350, vh))), R::Stitched);
    QCOMPARE(s.addFrame(doc.copy(0, 150, 350, vh)), R::Stitched);
    QCOMPARE(s.stitchedImage().height(), 150 + vh);
}

void ScrollStitchTest::lateLoadedContentTakesNewerPixels()
{
    QImage doc = document(350, 900);
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 350, 250));
    // An image placeholder at doc rows 150..175 loads between the frames.
    QPainter p(&doc);
    p.fillRect(30, 150, 200, 25, QColor(0, 120, 200));
    p.end();
    QCOMPARE(s.addFrame(doc.copy(0, 60, 350, 250)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 350, 310));
}

void ScrollStitchTest::replacesBaseBeforeFirstScroll()
{
    const QImage doc = document(350, 900);
    QImage dimmed = doc.copy(0, 0, 350, 250);
    QPainter p(&dimmed);
    p.fillRect(dimmed.rect(), QColor(0, 0, 0, 120));   // selection overlay fading out
    p.end();
    ScrollStitcher s;
    s.addFrame(dimmed);
    QCOMPARE(s.addFrame(doc.copy(0, 0, 350, 250)), R::Started);
    QCOMPARE(s.addFrame(doc.copy(0, 70, 350, 250)), R::Stitched);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 350, 320));
}

void ScrollStitchTest::sizeCapStopsGrowth()
{
    // 8192px wide: the 256 MiB cap is 8192 rows.
    const int w = 8192, vh = 300;
    QImage doc(w, 9000, QImage::Format_RGB32);
    doc.fill(Qt::white);
    for (int y = 0; y < doc.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(doc.scanLine(y));
        for (int x = 0; x < w; x += 7)
            row[(x + y * 13) % w] = qRgb((y * 31) & 255, (y * 7) & 255, (x * 3) & 255);
    }
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, w, vh));
    R last = R::Stitched;
    for (int y = 200; y < 8800 && last != R::Full; y += 200)
        last = s.addFrame(doc.copy(0, y, w, vh));
    QCOMPARE(last, R::Full);
    QVERIFY(qsizetype(s.stitchedHeight()) * w * 4 <= ScrollStitcher::kMaxBytes);
    const QImage got = s.stitchedImage();
    QCOMPARE(got, doc.copy(0, 0, w, got.height()));
}

void ScrollStitchTest::randomWalkIsByteExact()
{
    // Seeded random scrolling in both directions, with a sticky header, a
    // sticky footer, both or neither: whatever range was visited must come
    // out exact, sticky bands once.
    for (int seed = 1; seed <= 40; ++seed) {
        QRandomGenerator rng(seed);
        const int w = 320, vh = 240;
        const int hdr = (seed % 2) ? 30 : 0;
        const int ftr = (seed % 3 == 0) ? 22 : 0;
        const int body = vh - hdr - ftr;
        const QImage doc = document(w, 2400);
        auto frame = [&](int y) {
            QImage f(w, vh, QImage::Format_RGB32);
            QPainter p(&f);
            p.drawImage(0, hdr, doc.copy(0, y, w, body));
            p.fillRect(0, 0, w, hdr, QColor(30, 30, 60));
            p.fillRect(0, vh - ftr, w, ftr, QColor(60, 30, 30));
            return f;
        };
        ScrollStitcher s;
        int y = 1000, lo = y, hi = y;
        s.addFrame(frame(y));
        for (int i = 0; i < 30; ++i) {
            const int d = int(rng.bounded(-body / 2, body / 2 + 1));
            const int ny = std::clamp(y + d, 0, 2400 - body);
            if (ny == y)
                continue;
            const R r = s.addFrame(frame(ny));
            QVERIFY2(r == R::Stitched || r == R::Unchanged,
                     qPrintable(QStringLiteral("seed %1 step %2: %3").arg(seed).arg(i).arg(int(r))));
            y = ny;
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
        QImage expect(w, hdr + hi + body - lo + ftr, QImage::Format_RGB32);
        QPainter p(&expect);
        p.drawImage(0, 0, frame(lo).copy(0, 0, w, hdr));
        p.drawImage(0, hdr, doc.copy(0, lo, w, hi + body - lo));
        p.drawImage(0, hdr + hi + body - lo, frame(hi).copy(0, vh - ftr, w, ftr));
        p.end();
        QVERIFY2(s.stitchedImage() == expect, qPrintable(QStringLiteral("seed %1").arg(seed)));
    }
}

void ScrollStitchTest::largeFrameCost()
{
    // A 1920x1200 physical region scrolled by a wheel notch: the per-frame
    // cost runs on the GUI thread every 33 ms, so it has to stay small.
    const QImage doc = document(1920, 4000);
    ScrollStitcher s;
    s.addFrame(doc.copy(0, 0, 1920, 1200));
    QElapsedTimer t;
    t.start();
    int y = 0;
    for (int i = 0; i < 10; ++i) {
        y += 57;
        QCOMPARE(s.addFrame(doc.copy(0, y, 1920, 1200)), R::Stitched);
    }
    const qint64 perFrame = t.elapsed() / 10;
    qInfo("1920x1200 frame: %lld ms per addFrame", perFrame);
    QCOMPARE(s.stitchedImage(), doc.copy(0, 0, 1920, y + 1200));
}

QTEST_MAIN(ScrollStitchTest)
#include "ScrollStitchTest.moc"

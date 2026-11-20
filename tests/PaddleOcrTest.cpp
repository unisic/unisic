// The better OCR model's pure pieces (src/ocr/PaddleOcr.h), no runtime and no
// model: the DB box post-processing on a synthetic probability map, the CTC
// greedy decode, glyph boxes from timesteps, and visual rows.
#include "ocr/PaddleOcr.h"

#include <QtTest>

#include <vector>

class PaddleOcrTest : public QObject
{
    Q_OBJECT
private slots:
    // The build tree mirrors the install (CMake links share/unisic/models), so
    // this checks the lookup every Linux package relies on.
    void modelShipsWithTheBuild()
    {
#ifdef _WIN32
        QSKIP("Windows reads with Windows OCR and ships no model");
#endif
        QVERIFY(PaddleOcr::detPath().endsWith(QLatin1String("/PP-OCRv6_det_tiny.onnx")));
        QVERIFY(PaddleOcr::recPath().endsWith(QLatin1String("/PP-OCRv6_rec_tiny.onnx")));
        QCOMPARE(QFileInfo(PaddleOcr::recPath()).size(), qint64(4489813));
    }
    void detectorBoxes()
    {
        const int w = 100, h = 60;
        std::vector<float> map(size_t(w) * h, 0.f);
        auto fill = [&](int x0, int y0, int x1, int y1, float v) {
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    map[size_t(y) * w + x] = v;
        };
        fill(10, 20, 49, 29, 0.9f); // a text kernel
        fill(80, 5, 81, 6, 0.9f);   // a speck: too small
        fill(70, 40, 95, 55, 0.35f); // over the threshold, under the box score

        // Same scale: the 2x2 dilation grows the kernel to 10..50 x 20..30,
        // the unclip by 40 * 10 * 1.6 / 100 = 6.4 px each way.
        QVector<PaddleOcr::DetBox> b = PaddleOcr::boxesFromMap(map.data(), w, h, w, h);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].x0, 4);
        QCOMPARE(b[0].x1, 56);
        QCOMPARE(b[0].y0, 14);
        QCOMPARE(b[0].y1, 36);
        QVERIFY(qAbs(b[0].score - 360.f / 451.f) < 1e-4f);
        QVERIFY(qAbs(b[0].pad - 6.4f) < 1e-4f);

        // Twice the size: coordinates and margin scale with it.
        b = PaddleOcr::boxesFromMap(map.data(), w, h, 2 * w, 2 * h);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].x0, 8);
        QCOMPARE(b[0].x1, 112);
        QCOMPARE(b[0].y0, 28);
        QCOMPARE(b[0].y1, 72);
        QVERIFY(qAbs(b[0].pad - 12.8f) < 1e-4f);

        // Clipped inside the destination at the edges.
        std::fill(map.begin(), map.end(), 0.f);
        fill(0, 0, 30, 9, 0.9f);
        b = PaddleOcr::boxesFromMap(map.data(), w, h, w, h);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].x0, 0);
        QCOMPARE(b[0].y0, 0);
    }

    void ctcDecode()
    {
        // Classes: blank, "a", "b", space.
        const QStringList dict{QStringLiteral("a"), QStringLiteral("b")};
        const int seq[] = {1, 1, 0, 1, 3, 2};
        std::vector<float> probs(6 * 4, 0.05f);
        for (int t = 0; t < 6; ++t)
            probs[t * 4 + seq[t]] = 0.8f;
        const PaddleOcr::Line l = PaddleOcr::ctcDecode(probs.data(), 6, 4, dict);
        QCOMPARE(l.text(), QStringLiteral("aa b"));
        QCOMPARE(l.cols, (QVector<int>{0, 3, 4, 5}));
        QVERIFY(qAbs(l.score - 0.8f) < 1e-6f);

        const std::vector<float> blank{0.9f, 0.05f, 0.05f, 0.f};
        QCOMPARE(PaddleOcr::ctcDecode(blank.data(), 1, 4, dict).score, 0.f);
    }

    void glyphBoxes()
    {
        PaddleOcr::Line l;
        l.chars = {QStringLiteral("a"), QStringLiteral("a"), QStringLiteral(" "), QStringLiteral("b")};
        l.cols = {0, 3, 4, 5};
        l.score = 0.9f;
        QVector<OcrWord> g;
        PaddleOcr::appendGlyphs(g, l, QRect(0, 0, 100, 10), 10.0, 2, false);
        QCOMPARE(g.size(), 3);
        // "aa": pitch 3 steps; edges halfway between the timesteps' centres.
        QCOMPARE(g[0].rect, QRect(0, 0, 20, 10));
        QCOMPARE(g[1].rect, QRect(20, 0, 30, 10));
        // "b" alone: two steps wide around step 5.5.
        QCOMPARE(g[2].rect, QRect(45, 0, 20, 10));
        QCOMPARE(g[2].text, QStringLiteral("b"));
        QVERIFY(!g[0].spaceBefore && !g[1].spaceBefore && g[2].spaceBefore);
        QCOMPARE(g[1].line, 2);

        g.clear();
        PaddleOcr::appendGlyphs(g, l, QRect(0, 0, 100, 10), 10.0, 0, true);
        QVERIFY(g[0].spaceBefore);
    }

    void rows()
    {
        const QVector<QRect> boxes{QRect(60, 2, 40, 10), QRect(0, 30, 50, 10), QRect(0, 0, 50, 10)};
        const QVector<QVector<int>> r = PaddleOcr::groupRows(boxes);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0], (QVector<int>{2, 0}));
        QCOMPARE(r[1], (QVector<int>{1}));
    }
};

QTEST_GUILESS_MAIN(PaddleOcrTest)
#include "PaddleOcrTest.moc"

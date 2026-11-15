#include <QtTest>
#include <QFont>
#include <QImage>
#include <QPainter>
#include "ocr/OcrEngines.h"
#include "ocr/WindowsOcr.h"

// The OCR engine choice (default per platform, fallback for an id this build
// cannot run), the word-to-glyph split the Windows engine relies on, and on
// Windows one real Windows.Media.Ocr run. Everything but that last case is
// pure logic and runs on Linux too.
class OcrEngineTest : public QObject
{
    Q_OBJECT
private slots:
    void defaultEngineIsFirstAndRunnable()
    {
        const QStringList engines = OcrEngines::available();
        QCOMPARE(engines.value(0), QStringLiteral(UNISIC_OCR_DEFAULT_ENGINE));
        QVERIFY(engines.contains(QStringLiteral("tesseract")));
        QCOMPARE(engines.contains(QStringLiteral("windows")), WindowsOcr::built());
        QCOMPARE(OcrEngines::resolve(QStringLiteral(UNISIC_OCR_DEFAULT_ENGINE)),
                 QStringLiteral(UNISIC_OCR_DEFAULT_ENGINE));
    }

    void foreignIdRunsTesseract()
    {
        QCOMPARE(OcrEngines::resolve(QString()), QStringLiteral("tesseract"));
        QCOMPARE(OcrEngines::resolve(QStringLiteral("paddle")), QStringLiteral("tesseract"));
        // Settings imported from Windows into a build without the engine.
        QCOMPARE(OcrEngines::resolve(QStringLiteral("windows")),
                 WindowsOcr::built() ? QStringLiteral("windows") : QStringLiteral("tesseract"));
    }

    void unusableWindowsOcrRunsTesseract()
    {
        const QString windows = QStringLiteral("windows"), tesseract = QStringLiteral("tesseract");
        // No OCR language installed: Tesseract runs for the default and for an explicit pick alike.
        QCOMPARE(OcrEngines::effective(windows, false), tesseract);
        QCOMPARE(OcrEngines::effective(windows, true), OcrEngines::resolve(windows));
        QCOMPARE(OcrEngines::effective(tesseract, false), tesseract);
        QCOMPARE(OcrEngines::effective(tesseract, true), tesseract);
        QCOMPARE(OcrEngines::effective(QStringLiteral("paddle"), true), tesseract);
        QCOMPARE(OcrEngines::effective(QStringLiteral(UNISIC_OCR_DEFAULT_ENGINE), false), tesseract);
    }

    void wordSplitsIntoTilingGlyphs()
    {
        QVector<OcrWord> g;
        WindowsOcr::appendWordGlyphs(g, QRect(10, 5, 30, 12), QStringLiteral("abc"), 2, true);
        QCOMPARE(g.size(), 3);
        QCOMPARE(g[0].rect, QRect(10, 5, 10, 12));
        QCOMPARE(g[1].rect, QRect(20, 5, 10, 12));
        QCOMPARE(g[2].rect, QRect(30, 5, 10, 12));
        QCOMPARE(g[0].text + g[1].text + g[2].text, QStringLiteral("abc"));
        QVERIFY(g[0].spaceBefore);
        QVERIFY(!g[1].spaceBefore && !g[2].spaceBefore);
        for (const OcrWord &w : g)
            QCOMPARE(w.line, 2);

        // Appends after what is already there; no space before a line's first word.
        WindowsOcr::appendWordGlyphs(g, QRect(0, 30, 7, 10), QStringLiteral("xy"), 3, false);
        QCOMPARE(g.size(), 5);
        QVERIFY(!g[3].spaceBefore);
        QCOMPARE(g[3].rect.left(), 0);
        QCOMPARE(g[4].rect.right(), 6);
    }

    void splitKeepsGraphemesAndDirection()
    {
        QVector<OcrWord> g;
        // "e" + combining acute is ONE letter to select, not two.
        WindowsOcr::appendWordGlyphs(g, QRect(0, 0, 20, 10), QStringLiteral("e\u0301x"), 0, false);
        QCOMPARE(g.size(), 2);
        QCOMPARE(g[0].text, QStringLiteral("e\u0301"));

        // Hebrew reads right to left: the first letter takes the rightmost slice.
        g.clear();
        WindowsOcr::appendWordGlyphs(g, QRect(0, 0, 20, 10), QStringLiteral("\u05D0\u05D1"), 0, false);
        QCOMPARE(g.size(), 2);
        QCOMPARE(g[0].rect, QRect(10, 0, 10, 10));
        QCOMPARE(g[1].rect, QRect(0, 0, 10, 10));

        // More letters than pixels: every glyph keeps a box inside the word.
        g.clear();
        const QRect tiny(4, 4, 2, 6);
        WindowsOcr::appendWordGlyphs(g, tiny, QStringLiteral("abcde"), 0, false);
        QCOMPARE(g.size(), 5);
        for (const OcrWord &w : g)
            QVERIFY(!w.rect.isEmpty() && tiny.contains(w.rect));

        g.clear();
        WindowsOcr::appendWordGlyphs(g, QRect(0, 0, 20, 10), QString(), 0, false);
        WindowsOcr::appendWordGlyphs(g, QRect(), QStringLiteral("a"), 0, false);
        QVERIFY(g.isEmpty());
    }

    // The real recognizer, on the Windows CI runner. SKIPs where it cannot run
    // for a reason that is the machine's and not ours: not Windows, or no OCR
    // language installed for the user's profile languages.
    void windowsOcrReadsRenderedText()
    {
        if (!WindowsOcr::built())
            QSKIP("Windows OCR exists only in the Windows build");
        QImage img(640, 160, QImage::Format_ARGB32);
        img.fill(Qt::white);
        {
            QPainter p(&img);
            p.setPen(Qt::black);
            QFont f;
            f.setPixelSize(56);
            p.setFont(f);
            p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("Hello 2048"));
        }
        const std::atomic_bool cancelled{false};
        QVector<OcrWord> glyphs;
        QString text, why;
        const bool ok = WindowsOcr::recognize(img, cancelled, &glyphs, &text, &why);
        if (!ok && why.startsWith(QLatin1String("no Windows OCR language")))
            QSKIP(qPrintable(why));
        QVERIFY2(ok, qPrintable(why));
        QVERIFY2(text.contains(QLatin1String("2048")), qPrintable(text));
        QVERIFY2(glyphs.size() >= 4, qPrintable(QString::number(glyphs.size())));
        for (const OcrWord &w : glyphs)
            QVERIFY(img.rect().contains(w.rect));
    }
};

QTEST_MAIN(OcrEngineTest)
#include "OcrEngineTest.moc"

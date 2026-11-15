#pragma once
#include <QString>
#include <QTextBoundaryFinder>
#include <QVector>
#include <atomic>
#include "OcrWord.h"

class QImage; // declared only: Settings.h reaches this header through OcrEngines.h

// The OS recognizer on Windows (Windows.Media.Ocr through the C++/WinRT
// projection, src/ocr/WindowsOcr_win.cpp). Every other platform links the stub
// in WindowsOcr.cpp, where built() is false and recognize() always declines.
namespace WindowsOcr {

bool built();

// Recognizes on the CALLING thread, which must be a worker: it joins the
// multithreaded apartment for the call and blocks until the result is in.
// Returns false, with `why` set, when the engine cannot run here (no OCR
// language installed, a WinRT call failed) or `cancelled` was raised; the
// caller then falls back to Tesseract. `glyphs` and `text` are optional.
bool recognize(const QImage &img, const std::atomic_bool &cancelled,
               QVector<OcrWord> *glyphs, QString *text, QString *why);

// Windows OCR reports word boxes and no letter boxes, while the editor selects
// per glyph. Split the word box into equal slices, one per grapheme, laid out
// right to left for right-to-left text: a proportional font makes the inner
// edges approximate (an "m" gets an "i"'s width), but every slice stays inside
// the word, in order, and copying a range still yields the right characters.
inline void appendWordGlyphs(QVector<OcrWord> &out, const QRect &word, const QString &text,
                             int line, bool spaceBefore)
{
    QVector<QString> parts;
    QTextBoundaryFinder bf(QTextBoundaryFinder::Grapheme, text);
    for (qsizetype from = 0, to = bf.toNextBoundary(); to > 0; from = to, to = bf.toNextBoundary())
        parts.append(text.mid(from, to - from));
    const qsizetype n = parts.size();
    if (n == 0 || word.isEmpty())
        return;
    const bool rtl = text.isRightToLeft();
    const int w = word.width();
    for (qsizetype i = 0; i < n; ++i) {
        const qsizetype slot = rtl ? n - 1 - i : i;
        const int x0 = word.left() + int(w * slot / n);
        const int x1 = word.left() + int(w * (slot + 1) / n);
        OcrWord g;
        g.rect = QRect(x0, word.top(), qMax(1, x1 - x0), word.height());
        g.text = parts[i];
        g.line = line;
        g.spaceBefore = spaceBefore && i == 0;
        out.append(g);
    }
}

} // namespace WindowsOcr

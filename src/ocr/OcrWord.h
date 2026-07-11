#pragma once
#include <QRect>
#include <QString>

// One recognized glyph (single symbol/letter) with its image-pixel bounding
// box, in reading order. Defined here (not in OcrEngine.h) so AnnotationCanvas
// can hold a QVector<OcrWord> without pulling in tesseract — the engine is
// compiled only under HAVE_TESSERACT, but the editor's selectable-text overlay
// must build either way. Glyph granularity lets the user select individual
// letters; `line` groups glyphs into underlined text lines and `spaceBefore`
// marks a word boundary so a copied range keeps its spacing.
struct OcrWord {
    QRect rect;            // glyph bounding box in image pixels
    QString text;          // the glyph (usually one character)
    int line = 0;          // 0-based text-line index
    float confidence = 0.f;
    bool spaceBefore = false; // a space precedes this glyph (word boundary)
};

#pragma once
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include "OcrWord.h"

class QImage;
struct OrtApi;
struct OrtEnv;

// Linux's OCR engine: PP-OCRv6 text detection and recognition (PaddleOCR,
// Apache-2.0, the ONNX export RapidOCR 3.10 ships) on the ONNX Runtime that
// BackgroundRemover dlopen()s; the model ships with the package
// (docs/dev/packaging.md "OCR"). The
// pipeline is RapidOCR's, rewritten without OpenCV, with the settings the
// screenshot benchmark ran (PaddleOcr.cpp lists them); the angle classifier is
// left out because a screenshot's text is upright.
namespace PaddleOcr {

// The tiny pair every Linux package installs into share/unisic/models (6.3 MB
// against 31 MB for small and 139 MB for medium). File names are the upstream
// ones. "" when the file is missing.
QString detPath();
QString recPath();

struct Setup {
    const OrtApi *api = nullptr;
    OrtEnv *env = nullptr;
    QString det, rec;
};

// Recognizes on the calling thread, which must be a worker. Both sessions are
// created for the run and released before it returns, so nothing stays in
// memory between runs. Returns false with `why` set when the model cannot run
// or `cancelled` was raised; the caller then falls back to Tesseract.
// `glyphs` and `text` are optional. `threads` 0 lets ORT pick.
bool recognize(const Setup &s, const QImage &img, const std::atomic_bool &cancelled,
               QVector<OcrWord> *glyphs, QString *text, QString *why, int threads = 0);

// The pure pieces, public for the unit tests.

// One text box from the DB detector, in the coordinates of the image the
// probability map was made for: pixels [x0, x1) x [y0, y1). `pad` is how far
// the unclip grew it past the detected text kernel, so a glyph box can hug the
// letters instead of the padded crop.
struct DetBox {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float score = 0.f;
    float pad = 0.f;
};
// DB post-processing (RapidOCR DBPostProcess, defaults thresh 0.3, box_thresh
// 0.5, unclip_ratio 1.6, 2x2 dilation, score_mode fast) on a w x h probability
// map, scaled to a destW x destH image.
QVector<DetBox> boxesFromMap(const float *prob, int w, int h, int destW, int destH);

// CTC greedy decode of one line: `probs` is steps x classes softmax output,
// class 0 the blank, 1..dict.size() the dictionary, the last class a space.
struct Line {
    QStringList chars;   // one entry per kept timestep, " " for a space
    QVector<int> cols;   // the timestep each char was read at
    float score = 0.f;   // mean probability of the kept timesteps
    QString text() const { return chars.join(QString()); }
};
Line ctcDecode(const float *probs, int steps, int classes, const QStringList &dict);

// Glyph boxes for a decoded line read from `box` (image pixels), where
// `steps` timesteps span the box's width. Each glyph reaches halfway to its
// neighbours' timesteps; a space only marks the next glyph's spaceBefore.
void appendGlyphs(QVector<OcrWord> &out, const Line &line, const QRect &box, double steps,
                  int row, bool spaceBefore);

// Visual rows of line boxes, top to bottom, each left to right: a box joins
// the row whose first box's vertical centre is within half the taller height.
QVector<QVector<int>> groupRows(const QVector<QRect> &boxes);

} // namespace PaddleOcr

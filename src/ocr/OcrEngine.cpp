#include "OcrEngine.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <tesseract/baseapi.h>

namespace {
struct OcrResult { QString text; QString error; };

// Runs on a worker thread. A fresh TessBaseAPI per call keeps this reentrant.
OcrResult runOcr(QImage img, QString langs)
{
    OcrResult r;
    if (img.isNull()) {
        r.error = QObject::tr("No image to recognize");
        return r;
    }
    tesseract::TessBaseAPI api;
    // Init returns non-zero when any requested language's data is missing.
    if (api.Init(nullptr, langs.toUtf8().constData()) != 0) {
        r.error = QObject::tr("OCR language data for \"%1\" not found — "
                              "install the Tesseract language packs").arg(langs);
        return r;
    }
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    api.SetImage(rgba.constBits(), rgba.width(), rgba.height(), 4,
                 static_cast<int>(rgba.bytesPerLine()));
    api.SetSourceResolution(96); // screenshots have no DPI metadata; silences a warning
    char *out = api.GetUTF8Text();
    r.text = out ? QString::fromUtf8(out).trimmed() : QString();
    delete[] out;
    api.End();
    return r;
}
} // namespace

OcrEngine::OcrEngine(QObject *parent) : QObject(parent) {}

void OcrEngine::recognize(const QImage &img, const QString &langs, Result cb)
{
    auto *fw = new QFutureWatcher<OcrResult>(this);
    connect(fw, &QFutureWatcher<OcrResult>::finished, this, [fw, cb]() {
        const OcrResult r = fw->result();
        fw->deleteLater();
        cb(r.text, r.error);
    });
    fw->setFuture(QtConcurrent::run(runOcr, img, langs));
}

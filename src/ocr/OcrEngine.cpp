#include "OcrEngine.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h> // ETEXT_DESC — cancellable Recognize()
#ifdef HAVE_ZXING
#include <ZXing/ReadBarcode.h>
// DecodeHints was renamed to ReaderOptions in zxing-cpp 2.2; the version
// header itself moved names across releases, so probe the header directly.
#if __has_include(<ZXing/ReaderOptions.h>)
using ZXingOptions = ZXing::ReaderOptions;
#else
using ZXingOptions = ZXing::DecodeHints;
#endif
// Result::text() returns std::string in zxing-cpp 2.x but std::wstring in
// 1.x (still what openSUSE Leap 15.6 ships) — overloads pick the right
// conversion at compile time.
static QString zxingText(const std::string &s) { return QString::fromStdString(s); }
static QString zxingText(const std::wstring &s) { return QString::fromStdWString(s); }
#endif

namespace {
struct OcrResult { QString text; QString error; };
struct OcrBoxResult { QVector<OcrWord> words; QString error; };

// Runs on a worker thread. Recognizes and walks the result iterator at SYMBOL
// (glyph) granularity so the editor overlay can select individual letters,
// tracking the text-line index (for per-line underlines) and word boundaries
// (so a copied range keeps its spaces). No barcode short-circuit — a QR
// payload has no glyph geometry.
OcrBoxResult runOcrBoxes(QImage img, QString langs, std::shared_ptr<std::atomic_bool> cancelled)
{
    OcrBoxResult r;
    if (img.isNull()) {
        r.error = QObject::tr("No image to recognize");
        return r;
    }
    if (cancelled->load())
        return r;
    tesseract::TessBaseAPI api;
    bool ok = api.Init(nullptr, langs.toUtf8().constData()) == 0;
    if (!ok && qEnvironmentVariableIsEmpty("TESSDATA_PREFIX")) {
        static const char *const kTessData[] = {
            "/usr/share/tesseract/tessdata",
            "/usr/share/tessdata",
            "/usr/share/tesseract-ocr/5/tessdata",
            "/usr/share/tesseract-ocr/4.00/tessdata",
        };
        for (const char *dir : kTessData) {
            if (QDir(QString::fromLatin1(dir)).exists()
                && api.Init(dir, langs.toUtf8().constData()) == 0) {
                ok = true;
                break;
            }
        }
    }
    if (!ok) {
        r.error = QObject::tr("OCR language data for \"%1\" not found. "
                              "Install the Tesseract language packs").arg(langs);
        return r;
    }
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    api.SetImage(rgba.constBits(), rgba.width(), rgba.height(), 4,
                 static_cast<int>(rgba.bytesPerLine()));
    api.SetSourceResolution(96);
    using namespace tesseract;
    ETEXT_DESC monitor;
    monitor.cancel = [](void *that, int) { return static_cast<std::atomic_bool *>(that)->load(); };
    monitor.cancel_this = cancelled.get();
    if (api.Recognize(&monitor) != 0 || cancelled->load()) {
        api.End();
        return r;
    }
    tesseract::ResultIterator *it = api.GetIterator();
    const tesseract::PageIteratorLevel level = tesseract::RIL_SYMBOL;
    int line = -1;
    if (it) {
        do {
            if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE))
                ++line;
            // A new word that is NOT also the start of a line follows a space.
            const bool wordStart = it->IsAtBeginningOf(tesseract::RIL_WORD);
            const bool lineStart = it->IsAtBeginningOf(tesseract::RIL_TEXTLINE);
            char *w = it->GetUTF8Text(level);
            if (w) {
                const QString text = QString::fromUtf8(w);
                delete[] w;
                int x1, y1, x2, y2;
                if (!text.trimmed().isEmpty()
                    && it->BoundingBox(level, &x1, &y1, &x2, &y2)) {
                    OcrWord ow;
                    ow.rect = QRect(QPoint(x1, y1), QPoint(x2, y2));
                    ow.text = text;
                    ow.line = qMax(0, line);
                    ow.confidence = it->Confidence(level);
                    ow.spaceBefore = wordStart && !lineStart;
                    r.words.append(ow);
                }
            }
        } while (it->Next(level));
        delete it;
    }
    api.End();
    return r;
}

// Runs on a worker thread. A fresh TessBaseAPI per call keeps this reentrant.
// `cancelled` is flipped by ~OcrEngine so a job in flight aborts at its next
// checkpoint on quit instead of pinning the global thread pool (whose
// destructor waits) until tesseract finishes; nobody consumes the result then.
OcrResult runOcr(QImage img, QString langs, std::shared_ptr<std::atomic_bool> cancelled)
{
    OcrResult r;
    if (img.isNull()) {
        r.error = QObject::tr("No image to recognize");
        return r;
    }
    if (cancelled->load())
        return r;
#ifdef HAVE_ZXING
    // Barcode pass first: a QR (or other code) in the region means the user
    // wants its PAYLOAD — OCR-ing the code's pixels yields garbage. Grayscale
    // keeps the ImageView format portable across zxing-cpp versions.
    {
        const QImage gray = img.convertToFormat(QImage::Format_Grayscale8);
        ZXing::ImageView view(gray.constBits(), gray.width(), gray.height(),
                              ZXing::ImageFormat::Lum, int(gray.bytesPerLine()));
        ZXingOptions opts;
        opts.setTryHarder(true);
        opts.setTryRotate(true);
        const auto res = ZXing::ReadBarcode(view, opts);
        if (res.isValid()) {
            r.text = zxingText(res.text()).trimmed();
            if (!r.text.isEmpty())
                return r;
        }
    }
#endif
    if (cancelled->load())
        return r;
    tesseract::TessBaseAPI api;
    // Init returns non-zero when any requested language's data is missing.
    // The nullptr datapath uses the TESSDATA_PREFIX baked into libtesseract at
    // ITS build time — the AppImage bundles Ubuntu's build, whose baked path
    // does not exist on Fedora/Arch hosts. Retry against the known distro
    // tessdata locations before giving up (unless the user pinned one).
    bool ok = api.Init(nullptr, langs.toUtf8().constData()) == 0;
    if (!ok && qEnvironmentVariableIsEmpty("TESSDATA_PREFIX")) {
        static const char *const kTessData[] = {
            "/usr/share/tesseract/tessdata",          // Fedora
            "/usr/share/tessdata",                    // Arch
            "/usr/share/tesseract-ocr/5/tessdata",    // Debian/Ubuntu 24.04+
            "/usr/share/tesseract-ocr/4.00/tessdata", // Ubuntu 22.04
        };
        for (const char *dir : kTessData) {
            if (QDir(QString::fromLatin1(dir)).exists()
                && api.Init(dir, langs.toUtf8().constData()) == 0) {
                ok = true;
                break;
            }
        }
    }
    if (!ok) {
        r.error = QObject::tr("OCR language data for \"%1\" not found. "
                              "Install the Tesseract language packs").arg(langs);
        return r;
    }
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    api.SetImage(rgba.constBits(), rgba.width(), rgba.height(), 4,
                 static_cast<int>(rgba.bytesPerLine()));
    api.SetSourceResolution(96); // screenshots have no DPI metadata; silences a warning
    // Recognize with a cancel hook (polled at tesseract's progress ticks) so
    // the destructor can abort an in-flight recognition promptly.
    // ETEXT_DESC lives in ::tesseract on 5.x (Fedora) but in the GLOBAL
    // namespace on 4.x (Ubuntu 22.04 / the AppImage build). Unqualified
    // lookup with the namespace in scope resolves whichever exists.
    using namespace tesseract;
    ETEXT_DESC monitor;
    monitor.cancel = [](void *that, int) { return static_cast<std::atomic_bool *>(that)->load(); };
    monitor.cancel_this = cancelled.get();
    if (api.Recognize(&monitor) == 0 && !cancelled->load()) {
        char *out = api.GetUTF8Text();
        r.text = out ? QString::fromUtf8(out).trimmed() : QString();
        delete[] out;
    }
    api.End();
    return r;
}
} // namespace

OcrEngine::OcrEngine(QObject *parent) : QObject(parent) {}

OcrEngine::~OcrEngine()
{
    // Abort any in-flight worker (see m_cancelled in the header). The watcher
    // and callback die with us, so the discarded result is never consumed.
    m_cancelled->store(true);
}

QString OcrEngine::detectedLanguages()
{
    // Enumerate installed *.traineddata across the same tessdata dirs the Init
    // fallback loop uses, and join every real langpack into one spec. Tesseract
    // can't pick a language per region, so "auto" = load them all together;
    // osd (orientation/script) and equ (math) are not text languages. The first
    // directory that actually holds langpacks wins — mixing dirs would list a
    // language twice under different tessdata versions.
    // Mirror the loader's search scope (runOcr's Init fallback): when
    // TESSDATA_PREFIX is set, only its dir is consulted — advertising languages
    // from the distro dirs there would name packs Init then refuses to load.
    QStringList dirs;
    const QByteArray prefix = qgetenv("TESSDATA_PREFIX");
    if (!prefix.isEmpty()) {
        const QString p = QString::fromLocal8Bit(prefix);
        dirs << p << QDir(p).filePath(QStringLiteral("tessdata"));
    } else {
        dirs << QStringLiteral("/usr/share/tesseract/tessdata")
             << QStringLiteral("/usr/share/tessdata")
             << QStringLiteral("/usr/share/tesseract-ocr/5/tessdata")
             << QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata");
    }

    QStringList langs;
    QSet<QString> seen;
    for (const QString &d : std::as_const(dirs)) {
        QDir dir(d);
        if (!dir.exists())
            continue;
        const QStringList files =
            dir.entryList({QStringLiteral("*.traineddata")}, QDir::Files, QDir::Name);
        for (const QString &f : files) {
            const QString code = QFileInfo(f).completeBaseName();
            if (code.isEmpty() || code == QLatin1String("osd") || code == QLatin1String("equ"))
                continue;
            if (!seen.contains(code)) {
                seen.insert(code);
                langs << code;
            }
        }
        if (!langs.isEmpty())
            break;
    }
    // Tesseract weights the first language most; English is the safe primary.
    if (langs.removeAll(QStringLiteral("eng")) > 0)
        langs.prepend(QStringLiteral("eng"));
    return langs.join(QLatin1Char('+'));
}

void OcrEngine::recognize(const QImage &img, const QString &langs, Result cb)
{
    auto *fw = new QFutureWatcher<OcrResult>(this);
    connect(fw, &QFutureWatcher<OcrResult>::finished, this, [fw, cb]() {
        const OcrResult r = fw->result();
        fw->deleteLater();
        cb(r.text, r.error);
    });
    fw->setFuture(QtConcurrent::run(runOcr, img, langs, m_cancelled));
}

void OcrEngine::recognizeBoxes(const QImage &img, const QString &langs, BoxResult cb)
{
    auto *fw = new QFutureWatcher<OcrBoxResult>(this);
    connect(fw, &QFutureWatcher<OcrBoxResult>::finished, this, [fw, cb]() {
        const OcrBoxResult r = fw->result();
        fw->deleteLater();
        cb(r.words, r.error);
    });
    fw->setFuture(QtConcurrent::run(runOcrBoxes, img, langs, m_cancelled));
}

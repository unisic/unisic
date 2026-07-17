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

// The tessdata directories the loader consults, honouring TESSDATA_PREFIX (when
// set, ONLY its dir is valid — advertising langs from the distro dirs there
// would name packs Init then refuses to load).
QStringList tessdataDirs()
{
    const QByteArray prefix = qgetenv("TESSDATA_PREFIX");
    if (!prefix.isEmpty()) {
        const QString p = QString::fromLocal8Bit(prefix);
        return {p, QDir(p).filePath(QStringLiteral("tessdata"))};
    }
    return {QStringLiteral("/usr/share/tesseract/tessdata"),
            QStringLiteral("/usr/share/tessdata"),
            QStringLiteral("/usr/share/tesseract-ocr/5/tessdata"),
            QStringLiteral("/usr/share/tesseract-ocr/4.00/tessdata")};
}

// The first tessdata dir that actually holds <code>.traineddata, or "" if none.
QString tessdataDirWith(const QString &code)
{
    for (const QString &d : tessdataDirs())
        if (QFileInfo::exists(QDir(d).filePath(code + QStringLiteral(".traineddata"))))
            return d;
    return {};
}

// Init `api` with `langs`, trying the default resolution first and then each
// known tessdata dir (only when TESSDATA_PREFIX is unset — see tessdataDirs).
bool initTess(tesseract::TessBaseAPI &api, const QByteArray &langs)
{
    if (api.Init(nullptr, langs.constData()) == 0)
        return true;
    if (!qEnvironmentVariableIsEmpty("TESSDATA_PREFIX"))
        return false;
    for (const QString &d : tessdataDirs())
        if (QDir(d).exists() && api.Init(d.toUtf8().constData(), langs.constData()) == 0)
            return true;
    return false;
}

// The Tesseract langpack codes that write each non-Latin script OSD can name.
// Latin (and Latin-derived scripts: Fraktur, Vietnamese…) is handled by
// elimination — everything not in one of these sets.
const QHash<QString, QStringList> &scriptCodeTable()
{
    static const QHash<QString, QStringList> m = {
        {QStringLiteral("Arabic"), {"ara", "fas", "urd", "pus", "uig", "snd", "ckb"}},
        {QStringLiteral("Hebrew"), {"heb", "yid"}},
        {QStringLiteral("Han"), {"chi_sim", "chi_tra", "chi_sim_vert", "chi_tra_vert"}},
        {QStringLiteral("Japanese"), {"jpn", "jpn_vert"}},
        {QStringLiteral("Katakana"), {"jpn", "jpn_vert"}},
        {QStringLiteral("Hiragana"), {"jpn", "jpn_vert"}},
        {QStringLiteral("Hangul"), {"kor", "kor_vert"}},
        {QStringLiteral("Hangul_vert"), {"kor", "kor_vert"}},
        {QStringLiteral("Devanagari"), {"hin", "mar", "san", "nep"}},
        {QStringLiteral("Bengali"), {"ben", "asm"}},
        {QStringLiteral("Tamil"), {"tam"}},
        {QStringLiteral("Telugu"), {"tel"}},
        {QStringLiteral("Kannada"), {"kan"}},
        {QStringLiteral("Malayalam"), {"mal"}},
        {QStringLiteral("Gujarati"), {"guj"}},
        {QStringLiteral("Gurmukhi"), {"pan"}},
        {QStringLiteral("Thai"), {"tha"}},
        {QStringLiteral("Lao"), {"lao"}},
        {QStringLiteral("Khmer"), {"khm"}},
        {QStringLiteral("Myanmar"), {"mya"}},
        {QStringLiteral("Sinhala"), {"sin"}},
        {QStringLiteral("Tibetan"), {"bod"}},
        {QStringLiteral("Greek"), {"ell", "grc"}},
        {QStringLiteral("Cyrillic"),
         {"rus", "ukr", "bul", "srp", "mkd", "bel", "kaz", "kir", "mon", "tgk"}},
        {QStringLiteral("Georgian"), {"kat"}},
        {QStringLiteral("Armenian"), {"hye"}},
        {QStringLiteral("Ethiopic"), {"amh", "tir"}},
    };
    return m;
}

// Choose which installed langpacks to OCR with for a detected OSD script.
// Returns "" when nothing installed fits (caller keeps the full load-all spec).
QString langsForScript(const QString &script, const QStringList &available)
{
    const QSet<QString> avail(available.begin(), available.end());
    QStringList picked;
    const auto it = scriptCodeTable().constFind(script);
    if (it != scriptCodeTable().constEnd()) {
        for (const QString &c : *it)
            if (avail.contains(c))
                picked << c;
    } else {
        // Latin / unrecognised → every available code that is NOT a known
        // non-Latin one (covers eng, pol, deu, fra, spa, …).
        QSet<QString> nonLatin;
        for (const QStringList &codes : scriptCodeTable())
            for (const QString &c : codes)
                nonLatin.insert(c);
        for (const QString &c : available)
            if (!nonLatin.contains(c))
                picked << c;
    }
    if (picked.isEmpty())
        return {};
    // Latin digits / URLs turn up inside any script, and Tesseract weights the
    // first language most — put eng first when it is installed.
    picked.removeAll(QStringLiteral("eng"));
    if (avail.contains(QStringLiteral("eng")))
        picked.prepend(QStringLiteral("eng"));
    return picked.join(QLatin1Char('+'));
}

// Detect the image's script via osd.traineddata and narrow `availableSpec`
// (the "+"-joined load-all list) to the langpacks for that script. Returns ""
// on any failure — no OSD data, low confidence, nothing installed — so the
// caller falls back to load-all. Runs on the worker thread.
QString resolveScriptLangs(const QImage &img, const QString &availableSpec)
{
    const QString osdDir = tessdataDirWith(QStringLiteral("osd"));
    if (osdDir.isEmpty())
        return {}; // no OSD model → can't detect; load-all fallback
    tesseract::TessBaseAPI api;
    if (api.Init(osdDir.toUtf8().constData(), "osd") != 0)
        return {};
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    api.SetImage(rgba.constBits(), rgba.width(), rgba.height(), 4,
                 static_cast<int>(rgba.bytesPerLine()));
    api.SetSourceResolution(96);
    api.SetPageSegMode(tesseract::PSM_OSD_ONLY);
    int orientDeg = 0;
    float orientConf = 0.f, scriptConf = 0.f;
    const char *scriptName = nullptr;
    const bool ok = api.DetectOrientationScript(&orientDeg, &orientConf,
                                                &scriptName, &scriptConf);
    const QString script = (ok && scriptName) ? QString::fromLatin1(scriptName) : QString();
    api.End();
    if (script.isEmpty() || scriptConf <= 0.f)
        return {}; // no detection → load-all
    return OcrEngine::languagesForScript(script, availableSpec);
}

// Runs on a worker thread. Recognizes and walks the result iterator at SYMBOL
// (glyph) granularity so the editor overlay can select individual letters,
// tracking the text-line index (for per-line underlines) and word boundaries
// (so a copied range keeps its spaces). No barcode short-circuit — a QR
// payload has no glyph geometry.
OcrBoxResult runOcrBoxes(QImage img, QString langs, bool autoScript,
                         std::shared_ptr<std::atomic_bool> cancelled)
{
    OcrBoxResult r;
    if (img.isNull()) {
        r.error = QObject::tr("No image to recognize");
        return r;
    }
    if (cancelled->load())
        return r;
    // Auto-language: detect the script (OSD) and narrow to its langpacks — far
    // more accurate than loading every installed pack at once. Empty result
    // keeps `langs` (the full load-all spec) as the fallback.
    if (autoScript) {
        const QString picked = resolveScriptLangs(img, langs);
        if (!picked.isEmpty())
            langs = picked;
    }
    tesseract::TessBaseAPI api;
    const bool ok = initTess(api, langs.toUtf8());
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
OcrResult runOcr(QImage img, QString langs, bool autoScript,
                 std::shared_ptr<std::atomic_bool> cancelled)
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
    // Auto-language: narrow to the detected script's langpacks (see runOcrBoxes).
    if (autoScript) {
        const QString picked = resolveScriptLangs(img, langs);
        if (!picked.isEmpty())
            langs = picked;
    }
    tesseract::TessBaseAPI api;
    // initTess() resolves the datapath itself: the nullptr path uses the
    // TESSDATA_PREFIX baked into libtesseract at ITS build time — the AppImage
    // bundles Ubuntu's build, whose baked path does not exist on Fedora/Arch —
    // then retries the known distro tessdata dirs (unless the user pinned one).
    const bool ok = initTess(api, langs.toUtf8());
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
    QStringList langs;
    QSet<QString> seen;
    for (const QString &d : tessdataDirs()) {
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

bool OcrEngine::scriptDetectionAvailable()
{
    return !tessdataDirWith(QStringLiteral("osd")).isEmpty();
}

QString OcrEngine::languagesForScript(const QString &script, const QString &availableSpec)
{
    // OSD routinely mislabels Latin as Cyrillic or Greek (shared glyph shapes),
    // so narrowing on those would OCR a Latin page with the wrong pack AND drop
    // the user's real Latin language. Only narrow for VISUALLY DISTINCT scripts
    // (Arabic, Hebrew, Han, Japanese, Hangul, the Indic family, Thai, …); the
    // Latin/Cyrillic/Greek group keeps the full load-all set, which eng-first
    // already reads well. Distinct scripts don't false-positive on Latin.
    static const QSet<QString> confusable = {
        QStringLiteral("Latin"), QStringLiteral("Cyrillic"), QStringLiteral("Greek"),
        QStringLiteral("Fraktur"), QStringLiteral("Fraktur_latin")};
    if (confusable.contains(script))
        return {};
    return langsForScript(script, availableSpec.split(QLatin1Char('+'), Qt::SkipEmptyParts));
}

void OcrEngine::recognize(const QImage &img, const QString &langs, bool autoScript, Result cb)
{
    auto *fw = new QFutureWatcher<OcrResult>(this);
    connect(fw, &QFutureWatcher<OcrResult>::finished, this, [fw, cb]() {
        const OcrResult r = fw->result();
        fw->deleteLater();
        cb(r.text, r.error);
    });
    fw->setFuture(QtConcurrent::run(runOcr, img, langs, autoScript, m_cancelled));
}

void OcrEngine::recognizeBoxes(const QImage &img, const QString &langs, bool autoScript,
                               BoxResult cb)
{
    auto *fw = new QFutureWatcher<OcrBoxResult>(this);
    connect(fw, &QFutureWatcher<OcrBoxResult>::finished, this, [fw, cb]() {
        const OcrBoxResult r = fw->result();
        fw->deleteLater();
        cb(r.words, r.error);
    });
    fw->setFuture(QtConcurrent::run(runOcrBoxes, img, langs, autoScript, m_cancelled));
}

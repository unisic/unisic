#include "WindowsOcr.h"
#include <QImage>
#include <QRectF>
#include <QStringList>
#include <roapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>

// Windows.Media.Ocr through the C++/WinRT projection MSYS2 ships as the
// header-only `cppwinrt` package (mingw-w64's own headers carry no
// windows.media.ocr.h, so the raw ABI route would mean hand-declaring vtables).

namespace {
namespace wf = winrt::Windows::Foundation;
namespace wgi = winrt::Windows::Graphics::Imaging;
namespace wmo = winrt::Windows::Media::Ocr;
namespace wss = winrt::Windows::Storage::Streams;

// Joins the multithreaded apartment for one recognition and leaves it again.
// A pool thread already in it gets S_FALSE, which still counts and is still
// paired; one already in a single-threaded apartment (RPC_E_CHANGED_MODE) must
// not block on an async operation, so it fails and Tesseract runs instead.
struct Apartment {
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    Apartment() = default;
    Apartment(const Apartment &) = delete;
    Apartment &operator=(const Apartment &) = delete;
    ~Apartment()
    {
        if (SUCCEEDED(hr))
            RoUninitialize();
    }
};

QString fromHstring(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), qsizetype(s.size()));
}

QString hresultText(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}
} // namespace

bool WindowsOcr::built()
{
    return true;
}

bool WindowsOcr::usable()
{
    const Apartment apartment;
    if (FAILED(apartment.hr))
        return false;
    try {
        // The engine is a temporary, released before the apartment is left.
        return bool(wmo::OcrEngine::TryCreateFromUserProfileLanguages());
    } catch (const winrt::hresult_error &) {
        return false;
    } catch (const std::exception &) {
        return false;
    }
}

bool WindowsOcr::recognize(const QImage &src, const std::atomic_bool &cancelled,
                           QVector<OcrWord> *glyphs, QString *text, QString *why)
{
    const auto fail = [why](const QString &reason) {
        if (why)
            *why = reason;
        return false;
    };
    if (src.isNull())
        return fail(QStringLiteral("no image"));
    // Declared before every WinRT object, so it outlives (and releases after) them all.
    const Apartment apartment;
    if (FAILED(apartment.hr))
        return fail(QStringLiteral("RoInitialize failed: ") + hresultText(apartment.hr));
    try {
        // null when none of the user's profile languages has OCR installed.
        const wmo::OcrEngine engine = wmo::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine)
            return fail(QStringLiteral("no Windows OCR language is installed for the user's languages"));

        // Bgra8 premultiplied is QImage's ARGB32_Premultiplied on a little-endian
        // machine, rows tightly packed (4-byte pixels need no padding). The engine
        // refuses a side above MaxImageDimension, so a larger capture is scaled
        // down and the boxes are mapped back.
        QImage img = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const int maxSide = int(wmo::OcrEngine::MaxImageDimension());
        if (img.width() > maxSide || img.height() > maxSide)
            img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const qreal sx = qreal(src.width()) / img.width();
        const qreal sy = qreal(src.height()) / img.height();

        const auto bytes = std::uint32_t(img.sizeInBytes());
        wss::Buffer buffer(bytes);
        std::memcpy(buffer.data(), img.constBits(), bytes);
        buffer.Length(bytes);
        const wgi::SoftwareBitmap bitmap = wgi::SoftwareBitmap::CreateCopyFromBuffer(
            buffer, wgi::BitmapPixelFormat::Bgra8, img.width(), img.height(),
            wgi::BitmapAlphaMode::Premultiplied);

        // Polled rather than get(), so quitting mid-recognition cancels the
        // operation instead of holding the thread pool (and the exit) until it ends.
        // Not op.wait_for() in a loop: each call assigns op.Completed, which an
        // async operation accepts once, so the second poll of a run longer than
        // one interval threw 0x80000018 (E_ILLEGAL_DELEGATE_ASSIGNMENT). One
        // handler, shared state it owns a reference to, so it may fire late.
        struct Done {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
        };
        const auto state = std::make_shared<Done>();
        const auto op = engine.RecognizeAsync(bitmap);
        op.Completed([state](const wf::IAsyncOperation<wmo::OcrResult> &, wf::AsyncStatus) {
            const std::lock_guard<std::mutex> lock(state->m);
            state->done = true;
            state->cv.notify_all();
        });
        {
            std::unique_lock<std::mutex> lock(state->m);
            while (!state->cv.wait_for(lock, std::chrono::milliseconds(100), [&] { return state->done; })) {
                if (cancelled.load()) {
                    // Unlocked: the handler may run inside Cancel(). Cancel
                    // completes the operation promptly; it is still awaited
                    // so the apartment is not left with it running.
                    lock.unlock();
                    op.Cancel();
                    lock.lock();
                    state->cv.wait_for(lock, std::chrono::seconds(2), [&] { return state->done; });
                    return fail(QStringLiteral("cancelled"));
                }
            }
        }
        if (op.Status() == wf::AsyncStatus::Canceled)
            return fail(QStringLiteral("cancelled"));
        const wmo::OcrResult result = op.GetResults(); // throws the error of a failed run

        QStringList lines;
        int lineNo = 0;
        for (const wmo::OcrLine &line : result.Lines()) {
            lines.append(fromHstring(line.Text()));
            if (glyphs) {
                bool firstWord = true;
                for (const wmo::OcrWord &word : line.Words()) {
                    const wf::Rect r = word.BoundingRect();
                    const QRect box = QRectF(r.X * sx, r.Y * sy, r.Width * sx, r.Height * sy)
                                          .toAlignedRect();
                    appendWordGlyphs(*glyphs, box, fromHstring(word.Text()), lineNo, !firstWord);
                    firstWord = false;
                }
            }
            ++lineNo;
        }
        if (text)
            *text = lines.join(QLatin1Char('\n')).trimmed();
        return true;
    } catch (const winrt::hresult_error &e) {
        return fail(hresultText(HRESULT(e.code())) + QLatin1Char(' ') + fromHstring(e.message()));
    } catch (const std::exception &e) {
        return fail(QString::fromLocal8Bit(e.what()));
    }
}

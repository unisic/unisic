#pragma once
#include <QString>
#include <QStringList>
#include "WindowsOcr.h"

// Which text recognizer runs OCR. The id is a string, not a bool, so another
// engine can join the list without a settings migration.
//
// The default differs per platform the way HotkeyDefaults.h does it: Windows
// ships its own recognizer (Windows.Media.Ocr, no language data for us to
// bundle or install), Linux ships PP-OCRv6 tiny (PaddleOcr) with the package
// and keeps Tesseract for the scripts that model cannot read (Cyrillic,
// Korean, Arabic...).
#ifdef _WIN32
#define UNISIC_OCR_DEFAULT_ENGINE "windows"
#else
#define UNISIC_OCR_DEFAULT_ENGINE "paddle"
#endif

namespace OcrEngines {

// The engines this build can run, the platform default first. "paddle" is
// PaddleOcr, run by the ONNX Runtime BackgroundRemover loads: Linux only.
inline QStringList available()
{
    if (WindowsOcr::built())
        return {QStringLiteral("windows"), QStringLiteral("tesseract")};
    return {QStringLiteral("paddle"), QStringLiteral("tesseract")};
}

// The engine a stored id actually runs: an id this build cannot run (settings
// imported from Windows into Linux, a hand-edited config) means Tesseract.
inline QString resolve(const QString &id)
{
    return available().contains(id) ? id : QStringLiteral("tesseract");
}

// The engine that actually runs. Windows OCR needs an OCR language installed
// for one of the user's Windows languages; without one Tesseract runs, whether
// "windows" is the default or the user's pick, and Settings shows Tesseract
// with a hint saying why. The stored id is never rewritten, so installing a
// language later brings Windows OCR back with no change in Settings. The same
// goes for "paddle" without ONNX Runtime or its model files.
inline QString effective(const QString &id, bool windowsUsable, bool paddleUsable)
{
    const QString e = resolve(id);
    if ((e == QLatin1String("windows") && !windowsUsable) || (e == QLatin1String("paddle") && !paddleUsable))
        return QStringLiteral("tesseract");
    return e;
}

} // namespace OcrEngines

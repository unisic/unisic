#pragma once
#include <QString>
#include <QStringList>
#include "WindowsOcr.h"

// Which text recognizer runs OCR. The id is a string, not a bool, so another
// engine can join the list without a settings migration.
//
// The default differs per platform the way HotkeyDefaults.h does it: Windows
// ships its own recognizer (Windows.Media.Ocr, no language data for us to
// bundle or install), everywhere else Tesseract is the only engine there is.
#ifdef _WIN32
#define UNISIC_OCR_DEFAULT_ENGINE "windows"
#else
#define UNISIC_OCR_DEFAULT_ENGINE "tesseract"
#endif

namespace OcrEngines {

// The engines this build can run, the platform default first.
inline QStringList available()
{
    if (WindowsOcr::built())
        return {QStringLiteral("windows"), QStringLiteral("tesseract")};
    return {QStringLiteral("tesseract")};
}

// The engine a stored id actually runs: an id this build cannot run (settings
// imported from Windows into Linux, a hand-edited config) means Tesseract.
inline QString resolve(const QString &id)
{
    return available().contains(id) ? id : QStringLiteral("tesseract");
}

} // namespace OcrEngines

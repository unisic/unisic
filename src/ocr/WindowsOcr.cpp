#include "WindowsOcr.h"
#include <QImage>

// Not Windows: Tesseract is the only engine (WindowsOcr_win.cpp is the real one).

bool WindowsOcr::built()
{
    return false;
}

bool WindowsOcr::recognize(const QImage &, const std::atomic_bool &, QVector<OcrWord> *,
                           QString *, QString *why)
{
    if (why)
        *why = QStringLiteral("Windows OCR exists only on Windows");
    return false;
}

#include "diag/RuntimeTools.h"

// Windows 10 1803 and later ship curl.exe in System32, and the upload
// destinations shell out to it. Nothing else applies here: the clipboard and
// capture go through Qt and GDI, recording (ffmpeg, PipeWire) is off, and ZIP
// export has no zip.exe to call.
QList<RuntimeTools::Group> RuntimeTools::required()
{
    return {
        {QStringLiteral("curl"), {QStringLiteral("curl")}},
    };
}

bool RuntimeTools::needsVideoPlayback()
{
    return false;
}

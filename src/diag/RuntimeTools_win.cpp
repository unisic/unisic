#include "diag/RuntimeTools.h"

// deploy.sh bundles ffmpeg and ffprobe beside unisic.exe for GIF export and
// recording, and Windows 10 1803 and later ship curl.exe in System32, which
// the upload destinations shell out to. Nothing else applies here: the
// clipboard and capture go through Qt, DXGI and GDI, sounds through winmm, and
// ZIP export through the linked libarchive, so no other program is called.
QList<RuntimeTools::Group> RuntimeTools::required()
{
    return {
        {QStringLiteral("ffmpeg / ffprobe"), {QStringLiteral("ffmpeg"), QStringLiteral("ffprobe")}},
        {QStringLiteral("curl"), {QStringLiteral("curl")}},
    };
}

bool RuntimeTools::needsVideoPlayback()
{
    return false;
}

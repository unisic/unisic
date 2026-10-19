#include "diag/RuntimeTools.h"

QList<RuntimeTools::Group> RuntimeTools::required()
{
    return {
        {QStringLiteral("ffmpeg / ffprobe"), {QStringLiteral("ffmpeg"), QStringLiteral("ffprobe")}},
        {QStringLiteral("wl-clipboard"), {QStringLiteral("wl-copy")}},
        {QStringLiteral("curl"), {QStringLiteral("curl")}},
        {QStringLiteral("zip"), {QStringLiteral("zip")}},
        {QStringLiteral("grim"), {QStringLiteral("grim")}},
        {QStringLiteral("PipeWire tools"),
         {QStringLiteral("pw-record"), QStringLiteral("pw-dump"), QStringLiteral("pw-play")}},
    };
}

bool RuntimeTools::needsVideoPlayback()
{
    return true;
}

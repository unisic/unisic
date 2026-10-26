#include "notify/NativeSound.h"

#include <windows.h>
// WIN32_LEAN_AND_MEAN drops mmsystem.h from <windows.h>; PlaySoundW lives there.
#include <mmsystem.h>

QString NativeSound::playerName()
{
    return QStringLiteral("winmm");
}

bool NativeSound::playWav(const QString &wavPath)
{
    // SND_ASYNC returns at once and a later cue cuts this one off, which is
    // what a shutter fired twice should do. SND_NODEFAULT: a missing or
    // undecodable file stays silent instead of playing the Windows error ding.
    return PlaySoundW(reinterpret_cast<LPCWSTR>(wavPath.utf16()), nullptr,
                      SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

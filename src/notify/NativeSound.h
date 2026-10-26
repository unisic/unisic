#pragma once

#include <QString>

// A WAV player built into the OS, for platforms that ship none of the
// command-line players AppContext shells out to (pw-play, paplay, aplay).
// Linux has no such player: NativeSound.cpp says so and the shell-out path
// runs unchanged. NativeSound_win.cpp plays through winmm.
namespace NativeSound {

// Name the dependency report and the [cue] log show; empty where there is none.
QString playerName();

// Starts `wavPath` asynchronously at the sample's own level (like aplay, no
// volume control). False where there is no built-in player or it refused.
bool playWav(const QString &wavPath);

} // namespace NativeSound

#pragma once

#include <QString>
#include <QStringList>

// Better Blur (kwin-effects-forceblur, plugin id "forceblur", archived) and its
// maintained continuation Better Blur DX (kwin-effects-better-blur-dx, plugin
// id "better_blur_dx") are forks of KWin's blur effect that blur BEHIND every
// window their class list says to, whether the window asked for it or not.
// The scrolling-capture overlay and the record-region frame are full-screen
// transparent windows over the captured output, so a blurred one blurs the
// whole screen under it - on the display and in every frame the compositor
// streams from it: the capture comes out as one smooth blur (#121). Stock KWin
// only blurs on an explicit hint, which Unisic never sets. Both forks skip
// docks unless "Blur docks" is on, which is why those frames map as docks
// (layer-shell scope "dock"); this is what is left when that is not enough.
namespace KWinForceBlur {

// Why the effect blurs Unisic's frames, or an empty string when neither fork
// is loaded or it leaves them alone. `dock`: the frames map as layer-shell
// docks (any layer-shell session), not as X11 windows. One blocking D-Bus call
// to KWin (bounded) and a read of kwinrc; only meant for the moment such a
// frame is about to map.
QString blursThisApp(bool dock);

// The loaded fork's kwinrc group ("Effect-better-blur-dx" or
// "Effect-blurplus"), empty when neither is loaded.
QString loadedGroup();

// The kwinrc half: that group in the given kwinrc files (first file that has
// a key wins), matched against the names KWin knows the frame by. No D-Bus,
// so the unit test can feed it a file.
QString verdict(const QStringList &kwinrcFiles, const QString &group,
                const QStringList &names, bool dock);

} // namespace KWinForceBlur

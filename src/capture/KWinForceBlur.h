#pragma once

#include <QString>
#include <QStringList>

// Better Blur (kwin-effects-forceblur, plugin id "forceblur") is a fork of
// KWin's blur effect that blurs BEHIND every window its class list says to,
// whether the window asked for it or not. The scrolling-capture overlay and
// the record-region frame are full-screen transparent windows over the
// captured output, so with Unisic on that list the whole screen under them is
// blurred - on the display and in every frame the compositor streams from it:
// the capture comes out as one smooth blur (#121). Stock KWin only blurs on an
// explicit hint, which Unisic never sets, so this is the one effect that can.
namespace KWinForceBlur {

// Why the effect blurs Unisic's overlays, or an empty string when it is not
// loaded or leaves Unisic alone. One blocking D-Bus call to KWin (bounded) and
// a read of kwinrc; only meant for the moment such an overlay is about to map.
QString blursThisApp();

// Whether the effect is loaded in KWin at all (the first half of the above).
bool loaded();

// The second half, from the effect's [Effect-blurplus] group in the given
// kwinrc files (first file that has a key wins) and this app's id. No D-Bus,
// so the unit test can feed it a file.
QString verdict(const QStringList &kwinrcFiles, const QString &appId);

} // namespace KWinForceBlur

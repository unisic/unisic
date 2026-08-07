#pragma once

// Helpers that AppContext.cpp and its diagnostics half (SmokeTests.cpp) both
// need. Each one was a file-static in AppContext.cpp before the split; the only
// change is that it now has a declaration instead of being invisible outside
// that one file. Nothing belongs here that only the tests use - that stays a
// file-static in SmokeTests.cpp.

#include <QImage>
#include <QStringList>

class QMimeData;
class KWinScreencasting;

// How long the compositor gets to actually take our surface off screen after
// hide(). There is no Wayland event for "the frame without that window has been
// presented" - xdg_toplevel destruction is fire-and-forget - so this is a wait,
// not a handshake. Measured worst case among kwin_wayland, mutter, cosmic-comp
// and labwc was well under 100 ms; the rest is headroom, and it is only ever
// paid when a window was actually up.
inline constexpr int kSelfHideSettleMs = 180;

// The nine sound ids shipped in resources/sounds. The settings UI offers them,
// the self-test plays every one.
const QStringList &bundledSoundIds();

// A flat 320x200 fill in the Secondary token. Stands in for a real capture in
// every dev action that needs an image but not a compositor.
QImage devTestImage();

// Two words rendered large enough for Tesseract to box them. Feeds the OCR
// box self-test, which is the selectable-text overlay's data source.
QImage ocrBoxTestImage();

// zxing-cpp is already linked for QR decoding. Encoding a small preview is
// synchronous and bounded (360 squared pixels); unlike screenshot PNG work it
// never needs a worker or retains a full capture-sized buffer.
QImage qrPreviewImage(const QString &url);

// Builds the clipboard offer that Klipper actually records in its history.
// Ownership passes to KSystemClipboard::setMimeData.
QMimeData *makeForceImageMime(const QImage &img);

// One process-wide binding of the zkde_screencast global: the answer cannot
// change without reinstalling the desktop file and restarting anyway, and
// GifRecorder keeps its own instance for the actual recordings.
KWinScreencasting *kwinScreencastProbe();

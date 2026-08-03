#pragma once

#include <QByteArray>
#include <QString>

class QImage;

// The ONE place that turns a QImage into bytes in one of the four formats
// Unisic writes. Saving, uploading, the clipboard-file payload and every
// conversion path used to each carry their own copy of the same three rules -
// JPEG cannot hold alpha, GIF has to go through ffmpeg, PNG is the fallback
// when either refuses - and they drifted (the editor's overwrite path ignored
// the quality setting entirely and silently failed on .gif). One encoder means
// the answer cannot differ by which button was pressed.
namespace ImageEncode {

// `format` is the ASKED format; `Result::format` is what was actually produced
// and is what the file must be named after. They differ exactly when a rule
// above fired, which the caller has to be able to see - a .jpg holding PNG
// bytes is a file that opens nowhere.
struct Result
{
    QByteArray bytes;
    QString format; // "png" | "jpg" | "webp" | "gif"
    QString mime;
    // Why the produced format is not the asked one: "alpha" (JPEG cannot hold
    // the transparency this image has) or "gif" (no ffmpeg, or its encode
    // failed). Empty when the asked format was delivered. Deliberately a token
    // and not a sentence: the wording is a user-facing string and belongs in
    // AppContext with the rest of them, and this header has to stay linkable
    // into a test with no translations loaded.
    QString fallbackReason;

    bool ok() const { return !bytes.isEmpty(); }
};

// quality is the 1-100 image-quality setting; PNG ignores it.
//
// Cost: the returned bytes are a second copy of the image alongside the
// QImage itself (a 3840x2160 capture is 33 MB as pixels and roughly 5-10 MB as
// PNG), held only until the caller writes or uploads them.
//
// GIF additionally BLOCKS the calling thread: measured on a 3840x2160 frame,
// 0.12 s to hand ffmpeg a PNG on stdin plus 0.33 s for its palettegen/paletteuse
// pass, so roughly half a second. The upload path encodes on a pool thread and
// pays none of it; the SAVE path is deliberately still synchronous, because
// unpicking it would turn the whole after-capture pipeline (save, then history,
// notification, external action and upload, each reading the saved path) into
// continuations to hide a stall the user got by choosing GIF as their format.
// Revisit that trade if the number above stops being half a second.
Result encode(const QImage &img, const QString &format, int quality);

// True when the image actually contains transparent pixels (not merely an
// alpha-capable format). Sampled at <=256x256 points: a full scan of a 4K frame
// costs 8 megapixel reads on every save, and transparency comes in contiguous
// regions, so the sample finds it. Public because the save path asks BEFORE it
// picks a file name.
bool hasTransparency(const QImage &img);

} // namespace ImageEncode

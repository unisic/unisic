#pragma once

#include <QtGlobal>

// The recorder's quality knob in the two units it has to exist in: a percent,
// which is what the user sets and what the config stores, and an x264/VP9 CRF,
// which is what ffmpeg takes. The CRF is DERIVED at every call site and never
// stored, so the two can never drift apart in a config file.
//
// The scale is linear over the whole usable CRF range, 40 down to 0, because
// that is what makes the two directions exact inverses at every whole CRF step:
// 0% is CRF 40 (past which screen text turns to mush), 50% is CRF 20 (the
// default this replaced, so an untouched config keeps rendering exactly as it
// did) and 100% is CRF 0, mathematically lossless H.264 and a very large file.
// Nothing here quietly clamps to a "sensible" ceiling: a percent that meant
// something other than what it says would be worse than a big file that was
// asked for.
namespace UnisicVideo {

// CRF at 0% quality. 51 is x264's true maximum, but 41-51 is unusable for
// screen content, so spending scale on it would only make the useful half of
// the slider narrower.
constexpr int kCrfWorst = 40;

inline int crfFromPercent(int percent)
{
    const int p = qBound(0, percent, 100);
    return kCrfWorst - (p * kCrfWorst + 50) / 100;
}

inline int percentFromCrf(int crf)
{
    const int c = qBound(0, crf, kCrfWorst);
    return ((kCrfWorst - c) * 100 + kCrfWorst / 2) / kCrfWorst;
}

} // namespace UnisicVideo

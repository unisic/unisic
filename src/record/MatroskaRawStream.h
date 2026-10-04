#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

// The recorder's stdin framing for ffmpeg: a streamed Matroska file holding one
// raw-video track, every frame in its own Cluster with the timestamp the app
// sampled it at.
//
// Why not plain rawvideo: rawvideo carries no timestamps, so ffmpeg numbers the
// frames N/framerate and every sample the app had to drop (encoder backpressure)
// deleted 1/fps of time from the file - the video ran fast against the audio.
// -use_wallclock_as_timestamps stamped the moment ffmpeg READ a frame instead of
// the moment it was captured (seconds late behind a full pipe), rounded to the
// 1/framerate time base so a burst read at once collapsed onto one timestamp
// and was dropped, and used CLOCK_REALTIME where the pause intervals are
// monotonic. Here the app writes the capture time itself.
//
// Verified against ffmpeg 8.1.2 reading a pipe: the demuxer maps the ColourSpace
// FourCC of a V_UNCOMPRESSED track to bgra/bgr0/rgba/rgb0 with the channels in
// the right order, keeps 1 ms PTS exactly (a 1 ms burst survives intact), and
// DefaultDuration gives the stream its nominal frame rate. The muxer refuses to
// WRITE raw RGB to Matroska; reading it is fine.
namespace MatroskaRawStream {

namespace detail {

// Every size is written as an 8-byte vint (EBMLMaxSizeLength 8 below): a frame
// at 8K is far past what a shorter vint holds, and one fixed width keeps the
// cluster size arithmetic in frameHeader() a constant.
inline void putSize(QByteArray &out, quint64 size)
{
    out.append(char(0x01));
    for (int shift = 48; shift >= 0; shift -= 8)
        out.append(char((size >> shift) & 0xFF));
}

inline void putUInt(QByteArray &out, quint64 value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
        out.append(char((value >> shift) & 0xFF));
}

inline QByteArray element(const QByteArray &id, const QByteArray &payload)
{
    QByteArray out = id;
    putSize(out, quint64(payload.size()));
    out.append(payload);
    return out;
}

inline QByteArray uintElement(const QByteArray &id, quint64 value)
{
    QByteArray payload;
    putUInt(payload, value);
    return element(id, payload);
}

constexpr int kIdTimestamp = 0xE7;
constexpr int kIdSimpleBlock = 0xA3;

} // namespace detail

// ffmpeg's pix_fmt name to the FourCC its raw decoder looks up: "bgra" ->
// "BGRA", "bgr0" -> "BGR\0". That one rule covers all four orders the grabbers
// report; anything else reaches ffmpeg unchanged and fails there, loudly,
// instead of being guessed into the wrong channel order here.
inline QByteArray fourCC(const QString &pixFmt)
{
    QByteArray out = pixFmt.toLatin1().toUpper().left(4);
    out.replace('0', '\0');
    return out;
}

// The stream header, written once before the first frame. The Segment has the
// unknown size (all ones) so it can grow until the pipe closes.
inline QByteArray header(int width, int height, const QString &pixFmt, int fps)
{
    using namespace detail;
    QByteArray ebml;
    ebml += uintElement(QByteArray("\x42\x86", 2), 1);  // EBMLVersion
    ebml += uintElement(QByteArray("\x42\xF7", 2), 1);  // EBMLReadVersion
    ebml += uintElement(QByteArray("\x42\xF2", 2), 4);  // EBMLMaxIDLength
    ebml += uintElement(QByteArray("\x42\xF3", 2), 8);  // EBMLMaxSizeLength
    ebml += element(QByteArray("\x42\x82", 2), QByteArrayLiteral("matroska"));
    ebml += uintElement(QByteArray("\x42\x87", 2), 4);  // DocTypeVersion
    ebml += uintElement(QByteArray("\x42\x85", 2), 2);  // DocTypeReadVersion

    QByteArray info;
    info += uintElement(QByteArray("\x2A\xD7\xB1", 3), 1000000); // 1 ms ticks
    info += element(QByteArray("\x4D\x80", 2), QByteArrayLiteral("Unisic")); // MuxingApp
    info += element(QByteArray("\x57\x41", 2), QByteArrayLiteral("Unisic")); // WritingApp

    QByteArray video;
    video += uintElement(QByteArray("\xB0", 1), quint64(qMax(0, width)));
    video += uintElement(QByteArray("\xBA", 1), quint64(qMax(0, height)));
    video += element(QByteArray("\x2E\xB5\x24", 3), fourCC(pixFmt)); // ColourSpace

    QByteArray track;
    track += uintElement(QByteArray("\xD7", 1), 1);     // TrackNumber
    track += uintElement(QByteArray("\x73\xC5", 2), 1); // TrackUID
    track += uintElement(QByteArray("\x83", 1), 1);     // TrackType: video
    track += element(QByteArray("\x86", 1), QByteArrayLiteral("V_UNCOMPRESSED"));
    track += uintElement(QByteArray("\x23\xE3\x83", 3), // DefaultDuration, ns
                         quint64(1000000000) / quint64(qMax(1, fps)));
    track += element(QByteArray("\xE0", 1), video);

    QByteArray out = element(QByteArray("\x1A\x45\xDF\xA3", 4), ebml);
    out += QByteArray("\x18\x53\x80\x67\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 12); // Segment
    out += element(QByteArray("\x15\x49\xA9\x66", 4), info);
    out += element(QByteArray("\x16\x54\xAE\x6B", 4), element(QByteArray("\xAE", 1), track));
    return out;
}

// Everything that goes in front of one frame's pixels: a Cluster at ptsMs whose
// only child is a keyframe SimpleBlock for track 1. The pixels follow as a
// separate write so a 33 MB frame is never copied just to prepend 43 bytes.
inline QByteArray frameHeader(qint64 ptsMs, qsizetype frameBytes)
{
    using namespace detail;
    constexpr quint64 timestampElement = 1 + 8 + 8;
    constexpr quint64 blockHeader = 4; // track vint, int16 relative time, flags
    QByteArray out;
    out.reserve(4 + 8 + int(timestampElement) + 1 + 8 + int(blockHeader));
    out.append(QByteArray("\x1F\x43\xB6\x75", 4)); // Cluster
    putSize(out, timestampElement + 1 + 8 + blockHeader + quint64(frameBytes));
    out.append(char(kIdTimestamp));
    putSize(out, 8);
    putUInt(out, quint64(qMax<qint64>(0, ptsMs)));
    out.append(char(kIdSimpleBlock));
    putSize(out, blockHeader + quint64(frameBytes));
    out.append(char(0x81)); // track 1
    out.append(char(0x00));
    out.append(char(0x00)); // relative timestamp 0: the Cluster carries it
    out.append(char(0x80)); // keyframe
    return out;
}

} // namespace MatroskaRawStream

#pragma once
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

// The ffmpeg filter graph behind a clip export: the kept segments of input 0
// are cut out and joined, the video runs through `videoTail` (crop, scale,
// a hardware upload) and leaves as [v], each kept audio track as its own
// labelled output. Header-only and pure, so tests/ClipGraphTest.cpp covers
// it without ffmpeg or a window.
namespace ClipGraph {

struct Graph {
    QString filter;       // the -filter_complex value
    QStringList audioArgs; // -map/-filter/-metadata for the audio outputs
};

// segments: [start, end] in seconds on input 0's timeline, ascending.
// gains: one per source audio track, -1 = dropped (empty = no audio).
// mixTrack >= 0: that track is the recording's mix; it is not cut but
// rebuilt as amix of the kept stems and written first, titled `mixTitle`.
// titles: written back per kept stem, because filtered streams inherit no
// source metadata (empty entries are skipped).
inline Graph build(const QList<QPointF> &segments, const QString &videoTail,
                   const QList<double> &gains, int mixTrack,
                   const QStringList &titles, const QString &mixTitle)
{
    const auto num = [](double v) { return QString::number(v, 'f', 3); };
    QList<int> kept;
    for (int i = 0; i < gains.size(); ++i)
        if (gains.at(i) >= 0 && i != mixTrack)
            kept << i;
    const bool remix = mixTrack >= 0 && mixTrack < gains.size() && !kept.isEmpty();

    QStringList parts;
    QString concatIn;
    for (int s = 0; s < segments.size(); ++s) {
        const QString range = QStringLiteral("start=%1:end=%2")
                                  .arg(num(segments.at(s).x()), num(segments.at(s).y()));
        // The same input pad may feed several filters: ffmpeg duplicates it.
        parts << QStringLiteral("[0:v]trim=%1,setpts=PTS-STARTPTS[v%2]").arg(range).arg(s);
        concatIn += QStringLiteral("[v%1]").arg(s);
        for (int t : std::as_const(kept)) {
            parts << QStringLiteral("[0:a:%1]atrim=%2,asetpts=PTS-STARTPTS[a%3_%4]")
                         .arg(t).arg(range).arg(t).arg(s);
            concatIn += QStringLiteral("[a%1_%2]").arg(t).arg(s);
        }
    }
    QString concatOut = QStringLiteral("[vc]");
    for (int t : std::as_const(kept))
        concatOut += QStringLiteral("[c%1]").arg(t);
    parts << concatIn + QStringLiteral("concat=n=%1:v=1:a=%2").arg(segments.size()).arg(kept.size())
                 + concatOut;
    parts << (videoTail.isEmpty() ? QStringLiteral("[vc]null[v]")
                                  : QStringLiteral("[vc]%1[v]").arg(videoTail));

    Graph g;
    QString mixIn;
    for (int t : std::as_const(kept)) {
        const double gain = gains.at(t);
        const QString vol = qFuzzyCompare(gain, 1.0)
                                ? QStringLiteral("anull")
                                : QStringLiteral("volume=%1").arg(num(gain));
        if (remix) {
            parts << QStringLiteral("[c%1]%2,asplit=2[o%1][m%1]").arg(t).arg(vol);
            mixIn += QStringLiteral("[m%1]").arg(t);
        } else {
            parts << QStringLiteral("[c%1]%2[o%1]").arg(t).arg(vol);
        }
    }
    int out = 0;
    const auto title = [&g](int stream, const QString &name) {
        // Both forms, for the reason GifRecorder::finalAudioArgs gives.
        g.audioArgs << QStringLiteral("-metadata:s:a:%1").arg(stream)
                    << QStringLiteral("title=%1").arg(name)
                    << QStringLiteral("-metadata:s:a:%1").arg(stream)
                    << QStringLiteral("handler_name=%1").arg(name);
    };
    if (remix) {
        parts << mixIn + QStringLiteral("amix=inputs=%1:duration=longest:normalize=0[mix]")
                             .arg(kept.size());
        g.audioArgs << QStringLiteral("-map") << QStringLiteral("[mix]");
        title(out++, mixTitle);
    }
    for (int t : std::as_const(kept)) {
        g.audioArgs << QStringLiteral("-map") << QStringLiteral("[o%1]").arg(t);
        if (t < titles.size() && !titles.at(t).isEmpty())
            title(out, titles.at(t));
        ++out;
    }
    g.filter = parts.join(QLatin1Char(';'));
    return g;
}

// Even-sized crop (yuv420p needs it) and an optional downscale so the
// SHORTER side is `shortSide` px. Never upscales. srcW/srcH: the source
// size; crop w <= 0 means the full frame.
inline QString videoTail(int srcW, int srcH, int cropX, int cropY, int cropW, int cropH,
                         int shortSide)
{
    int w = srcW, h = srcH;
    QStringList chain;
    if (cropW > 0 && cropH > 0 && (cropW < srcW || cropH < srcH)) {
        w = cropW & ~1;
        h = cropH & ~1;
        chain << QStringLiteral("crop=%1:%2:%3:%4").arg(w).arg(h).arg(qMax(0, cropX)).arg(qMax(0, cropY));
    } else {
        chain << QStringLiteral("crop=trunc(iw/2)*2:trunc(ih/2)*2");
    }
    const int shortest = qMin(w, h);
    if (shortSide > 0 && shortest > shortSide) {
        const double f = double(shortSide) / shortest;
        const int sw = qMax(2, qRound(w * f / 2) * 2);
        const int sh = qMax(2, qRound(h * f / 2) * 2);
        chain << QStringLiteral("scale=%1:%2").arg(sw).arg(sh);
    }
    return chain.join(QLatin1Char(','));
}

} // namespace ClipGraph

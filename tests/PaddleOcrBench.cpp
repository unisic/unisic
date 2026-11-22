// Developer harness for the better OCR model, never shipped: runs PaddleOcr
// over pictures with the runtime BackgroundRemover loads, and writes the
// JSON the OCR benchmark's score.py reads (results/*.json).
//
//   unisic_paddle_ocr_bench <model dir> <tiny|small> <threads> <runs> <out.json> <image>...
//
// Every run creates and releases both sessions, as the app does, so the
// times include the model load. Peak memory is VmHWM at the end against VmRSS
// once the runtime is loaded and before the first run.
#include "editor/BackgroundRemover.h"
#include "ocr/PaddleOcr.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <algorithm>
#include <cstdio>

static qint64 procKb(const char *field)
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray &line : f.readAll().split('\n'))
        if (line.startsWith(field))
            return line.mid(qstrlen(field)).trimmed().split(' ').value(0).toLongLong();
    return -1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 7) {
        std::fprintf(stderr, "usage: %s <model dir> <tiny|small> <threads> <runs> <out.json> <image>...\n", argv[0]);
        return 2;
    }
    BackgroundRemover br;
    PaddleOcr::Setup s = br.ocrSetup();
    if (!s.api) {
        std::fprintf(stderr, "no ONNX Runtime: %s\n", qPrintable(br.runtimeInfo()));
        return 1;
    }
    s.det = a[1] + QStringLiteral("/PP-OCRv6_det_") + a[2] + QStringLiteral(".onnx");
    s.rec = a[1] + QStringLiteral("/PP-OCRv6_rec_") + a[2] + QStringLiteral(".onnx");
    const int threads = a[3].toInt();
    const int runs = std::max(1, a[4].toInt());
    const std::atomic_bool cancelled{false};
    const qint64 baseKb = procKb("VmRSS:");

    QJsonObject images;
    for (int i = 6; i < a.size(); ++i) {
        const QImage img(a[i]);
        QJsonArray times;
        QString text, why;
        std::vector<double> t;
        for (int r = 0; r < runs; ++r) {
            QVector<OcrWord> glyphs;
            QElapsedTimer timer;
            timer.start();
            if (!PaddleOcr::recognize(s, img, cancelled, &glyphs, &text, &why, threads)) {
                std::fprintf(stderr, "%s: %s\n", qPrintable(a[i]), qPrintable(why));
                return 1;
            }
            t.push_back(timer.nsecsElapsed() / 1e9);
            times.append(t.back());
        }
        std::sort(t.begin(), t.end());
        images.insert(QFileInfo(a[i]).fileName(),
                      QJsonObject{{"text", text}, {"times", times}, {"median_s", t[t.size() / 2]}});
    }
    const qint64 peakKb = procKb("VmHWM:");
    QJsonObject out{{"variant", QStringLiteral("cpp_") + a[2]}, {"threads", threads}, {"runs", runs},
                    {"rss_base_mb", baseKb / 1024.0}, {"rss_peak_mb", peakKb / 1024.0},
                    {"rss_extra_mb", (peakKb - baseKb) / 1024.0}, {"images", images}};
    QFile f(a[5]);
    if (!f.open(QIODevice::WriteOnly))
        return 1;
    f.write(QJsonDocument(out).toJson());
    std::printf("%s threads=%d base %.0f MB peak %.0f MB extra %.0f MB\n", qPrintable(a[2]), threads,
                baseKb / 1024.0, peakKb / 1024.0, (peakKb - baseKb) / 1024.0);
    return 0;
}

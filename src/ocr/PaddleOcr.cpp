#include "PaddleOcr.h"

#include <onnxruntime/onnxruntime_c_api.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace PaddleOcr {

namespace {

// RapidOCR 3.10 defaults (config.yaml), the settings the benchmark ran with.
constexpr int kMaxSide = 2000;       // Global.max_side_len: larger pictures are scaled down first
constexpr int kMinSide = 30;         // Global.min_side_len
constexpr int kMinHeight = 30;       // Global.min_height: shorter pictures get vertical padding
constexpr double kWideRatio = 8.0;   // Global.width_height_ratio: so do wider ones
// Det.limit_type max, limit_side_len 2048, not RapidOCR's stock min/736: the
// benchmark ran the detector at the screenshot's own resolution. Stock
// upscales the short side to 736, which more than doubled the detector's time
// and memory on the benchmark's pictures for the same CER, and grows a narrow
// strip without bound (100 x 2000 became 736 x 14720).
constexpr int kDetMaxSide = 2048;
constexpr float kThresh = 0.3f;      // Det.thresh
constexpr float kBoxThresh = 0.5f;   // Det.box_thresh
constexpr int kMaxCandidates = 1000; // Det.max_candidates
constexpr double kUnclip = 1.6;      // Det.unclip_ratio
constexpr int kMinBox = 3;           // DBPostProcess.min_size
constexpr int kRecHeight = 48;       // Rec.rec_img_shape [3, 48, 320]
constexpr int kRecWidth = 320;
constexpr int kRecBatch = 6;         // Rec.rec_batch_num
constexpr float kTextScore = 0.5f;   // Global.text_score

int round32(double v)
{
    // Python's round() is half-to-even, which is what nearbyint does by default.
    return std::max(32, int(std::nearbyint(v / 32.0)) * 32);
}

// cv2.resize with INTER_LINEAR: half-pixel centres, edge pixels clamped, no
// area averaging when shrinking. RGB888 in and out.
QImage resizeLinear(const QImage &src, int w, int h)
{
    if (src.width() == w && src.height() == h)
        return src;
    QImage dst(w, h, QImage::Format_RGB888);
    const int sw = src.width(), sh = src.height();
    auto axis = [](int n, int sn, std::vector<int> &idx, std::vector<float> &frac) {
        idx.resize(n);
        frac.resize(n);
        const double scale = double(sn) / n;
        for (int i = 0; i < n; ++i) {
            const double f = (i + 0.5) * scale - 0.5;
            int s = int(std::floor(f));
            float a = float(f - s);
            if (s < 0)
                s = 0, a = 0;
            if (s >= sn - 1)
                s = sn - 1, a = 0;
            idx[i] = s;
            frac[i] = a;
        }
    };
    std::vector<int> xi, yi;
    std::vector<float> xf, yf;
    axis(w, sw, xi, xf);
    axis(h, sh, yi, yf);
    for (int y = 0; y < h; ++y) {
        const uchar *r0 = src.constScanLine(yi[y]);
        const uchar *r1 = src.constScanLine(std::min(sh - 1, yi[y] + 1));
        const float b = yf[y];
        uchar *o = dst.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int x0 = xi[x] * 3, x1 = std::min(sw - 1, xi[x] + 1) * 3;
            const float a = xf[x];
            for (int c = 0; c < 3; ++c) {
                const float top = r0[x0 + c] + a * (r0[x1 + c] - r0[x0 + c]);
                const float bot = r1[x0 + c] + a * (r1[x1 + c] - r1[x0 + c]);
                o[x * 3 + c] = uchar(std::lround(top + b * (bot - top)));
            }
        }
    }
    return dst;
}

// The models were trained on OpenCV's BGR order: (v / 255 - 0.5) / 0.5 per
// channel, planar. Writes `img` into columns [0, img.width()) of a plane
// `stride` wide.
void toBgrPlanes(const QImage &img, float *out, int stride)
{
    const size_t plane = size_t(stride) * img.height();
    for (int y = 0; y < img.height(); ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < img.width(); ++x)
            for (int c = 0; c < 3; ++c)
                out[(2 - c) * plane + size_t(y) * stride + x] = line[x * 3 + c] / 127.5f - 1.0f;
    }
}

// np.rot90: a quarter turn counter-clockwise.
QImage rotateCcw(const QImage &src)
{
    QImage dst(src.height(), src.width(), QImage::Format_RGB888);
    for (int y = 0; y < dst.height(); ++y) {
        uchar *o = dst.scanLine(y);
        for (int x = 0; x < dst.width(); ++x)
            std::copy_n(src.constScanLine(x) + (src.width() - 1 - y) * 3, 3, o + x * 3);
    }
    return dst;
}

// One ORT output, released with the run.
struct Output {
    const OrtApi *api = nullptr;
    OrtValue *value = nullptr;
    const float *data = nullptr;
    std::vector<int64_t> shape;
    Output() = default;
    Output(const Output &) = delete;
    Output &operator=(const Output &) = delete;
    ~Output()
    {
        if (value)
            api->ReleaseValue(value);
    }
};

class Runner
{
public:
    Runner(const Setup &s, QString *why) : m_s(s), m_api(s.api), m_why(why) {}
    ~Runner()
    {
        if (m_det) m_api->ReleaseSession(m_det);
        if (m_rec) m_api->ReleaseSession(m_rec);
        if (m_mem) m_api->ReleaseMemoryInfo(m_mem);
    }
    Runner(const Runner &) = delete;
    Runner &operator=(const Runner &) = delete;

    bool load(int threads)
    {
        OrtSessionOptions *opts = nullptr;
        // No arena, as for background removal: its blocks would stay with the
        // process after the run.
        bool ok = ok_(m_api->CreateSessionOptions(&opts))
                  && ok_(m_api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL))
                  && ok_(m_api->DisableCpuMemArena(opts))
                  && (threads <= 0 || ok_(m_api->SetIntraOpNumThreads(opts, threads)))
                  && create(m_s.det, opts, &m_det) && create(m_s.rec, opts, &m_rec)
                  && ok_(m_api->CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &m_mem));
        if (opts)
            m_api->ReleaseSessionOptions(opts);
        return ok && loadDict();
    }

    bool run(bool det, std::vector<float> &input, const std::array<int64_t, 4> &shape, Output &out)
    {
        OrtValue *in = nullptr;
        if (!ok_(m_api->CreateTensorWithDataAsOrtValue(m_mem, input.data(), input.size() * sizeof(float),
                                                       shape.data(), shape.size(),
                                                       ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in)))
            return false;
        // Both PP-OCRv6 exports (Paddle2ONNX) name their tensors this way.
        const char *inName = "x";
        const char *outName = "fetch_name_0";
        out.api = m_api;
        const bool ran = ok_(m_api->Run(det ? m_det : m_rec, nullptr, &inName, &in, 1, &outName, 1, &out.value));
        m_api->ReleaseValue(in);
        if (!ran)
            return false;
        OrtTensorTypeAndShapeInfo *info = nullptr;
        size_t n = 0;
        void *data = nullptr;
        bool ok = ok_(m_api->GetTensorTypeAndShape(out.value, &info)) && ok_(m_api->GetDimensionsCount(info, &n));
        if (ok) {
            out.shape.resize(n);
            ok = ok_(m_api->GetDimensions(info, out.shape.data(), n));
        }
        if (info)
            m_api->ReleaseTensorTypeAndShapeInfo(info);
        if (!ok || !ok_(m_api->GetTensorMutableData(out.value, &data)))
            return false;
        out.data = static_cast<const float *>(data);
        return true;
    }

    const QStringList &dict() const { return m_dict; }

private:
    // From memory, as background removal does: CreateSession takes a wide
    // path on Windows, and this file compiles there too.
    bool create(const QString &path, const OrtSessionOptions *opts, OrtSession **out)
    {
        QFile file(path);
        uchar *bytes = file.open(QIODevice::ReadOnly) ? file.map(0, file.size()) : nullptr;
        if (!bytes) {
            *m_why = path + QStringLiteral(": ") + file.errorString();
            return false;
        }
        const bool ok = ok_(m_api->CreateSessionFromArray(m_s.env, bytes, size_t(file.size()), opts, out));
        file.unmap(bytes);
        return ok;
    }

    bool ok_(OrtStatus *st)
    {
        if (!st)
            return true;
        *m_why = QString::fromUtf8(m_api->GetErrorMessage(st));
        m_api->ReleaseStatus(st);
        return false;
    }

    // The recognizer carries its character list in the model's metadata.
    bool loadDict()
    {
        OrtModelMetadata *meta = nullptr;
        OrtAllocator *alloc = nullptr;
        char *value = nullptr;
        bool ok = ok_(m_api->GetAllocatorWithDefaultOptions(&alloc))
                  && ok_(m_api->SessionGetModelMetadata(m_rec, &meta))
                  && ok_(m_api->ModelMetadataLookupCustomMetadataMap(meta, alloc, "character", &value));
        if (value) {
            m_dict = QString::fromUtf8(value).split(QLatin1Char('\n'));
            if (OrtStatus *st = m_api->AllocatorFree(alloc, value))
                m_api->ReleaseStatus(st);
        }
        if (meta)
            m_api->ReleaseModelMetadata(meta);
        if (ok && m_dict.isEmpty()) {
            *m_why = QStringLiteral("the recognition model has no character list");
            ok = false;
        }
        return ok;
    }

    Setup m_s;
    const OrtApi *m_api;
    QString *m_why;
    OrtSession *m_det = nullptr;
    OrtSession *m_rec = nullptr;
    OrtMemoryInfo *m_mem = nullptr;
    QStringList m_dict;
};

struct Read {
    QRect box;      // the crop, image pixels
    float pad = 0;  // unclip margin in image pixels
    bool rotated = false;
    Line line;
    double steps = 0;
};

} // namespace

// Found from the binary, like the bundled tessdata (OcrEngine.cpp): an AppImage
// or tarball sets no variable. <bin>/share is the build tree, where CMake links
// resources/models so a dev build and the tests run the same files.
static QString bundledModel(const QString &file)
{
    const QString bin = QCoreApplication::applicationDirPath();
    for (const QString &dir : {bin + QStringLiteral("/../share/unisic/models/"), bin + QStringLiteral("/share/unisic/models/")}) {
        const QString path = QDir::cleanPath(dir + file);
        if (QFileInfo::exists(path))
            return path;
    }
    return {};
}

QString detPath()
{
    return bundledModel(QStringLiteral("PP-OCRv6_det_tiny.onnx"));
}

QString recPath()
{
    return bundledModel(QStringLiteral("PP-OCRv6_rec_tiny.onnx"));
}

// shortcut: axis-aligned boxes, not cv2.minAreaRect, so rotated text gets a loose crop; upgrade if tilted text (photos, rotated UI) must be read.
QVector<DetBox> boxesFromMap(const float *prob, int w, int h, int destW, int destH)
{
    // Threshold, then the 2x2 dilation cv2.dilate applies with its anchor at
    // (1, 1): a pixel is on when it or its left, upper or upper-left neighbour
    // is over the threshold.
    std::vector<uchar> on(size_t(w) * h);
    auto hot = [&](int x, int y) { return prob[size_t(y) * w + x] > kThresh; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            on[size_t(y) * w + x] = hot(x, y) || (x > 0 && hot(x - 1, y))
                                    || (y > 0 && hot(x, y - 1)) || (x > 0 && y > 0 && hot(x - 1, y - 1));

    // shortcut: each 8-connected region becomes its axis-aligned bounding box
    // where RapidOCR fits cv2.minAreaRect to the region's contour (and reads a
    // hole's contour as one more candidate). Screenshot text is horizontal, so
    // the two agree; upgrade to a rotated rectangle if tilted text (photos)
    // ever matters.
    QVector<DetBox> out;
    std::vector<int> stack;
    int candidates = 0;
    for (int sy = 0; sy < h && candidates < kMaxCandidates; ++sy) {
        for (int sx = 0; sx < w && candidates < kMaxCandidates; ++sx) {
            if (!on[size_t(sy) * w + sx])
                continue;
            ++candidates;
            int x0 = sx, x1 = sx, y0 = sy, y1 = sy;
            on[size_t(sy) * w + sx] = 0;
            stack.assign(1, sy * w + sx);
            while (!stack.empty()) {
                const int p = stack.back();
                stack.pop_back();
                const int px = p % w, py = p / w;
                x0 = std::min(x0, px), x1 = std::max(x1, px);
                y0 = std::min(y0, py), y1 = std::max(y1, py);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = px + dx, ny = py + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h && on[size_t(ny) * w + nx]) {
                            on[size_t(ny) * w + nx] = 0;
                            stack.push_back(ny * w + nx);
                        }
                    }
            }
            // minAreaRect measures between pixel centres.
            const int bw = x1 - x0, bh = y1 - y0;
            if (std::min(bw, bh) < kMinBox)
                continue;
            double sum = 0;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    sum += prob[size_t(y) * w + x];
            const float score = float(sum / (double(bw + 1) * (bh + 1)));
            if (score < kBoxThresh)
                continue;
            // Unclip: grow by area * ratio / perimeter (pyclipper, round joins,
            // integer output), whose bounding box is the new rectangle.
            const double d = double(bw) * bh * kUnclip / (2.0 * (bw + bh));
            const auto clipperRound = [](double v) { return int(v < 0 ? v - 0.5 : v + 0.5); };
            const int ux0 = clipperRound(x0 - d), ux1 = clipperRound(x1 + d);
            const int uy0 = clipperRound(y0 - d), uy1 = clipperRound(y1 + d);
            if (std::min(ux1 - ux0, uy1 - uy0) < kMinBox + 2)
                continue;
            // To the destination image (np.round, half to even), then
            // filter_det_res: clipped inside it, and at least 4 px each way.
            const auto scale = [](int v, int from, int to, int limit) {
                const double s = std::clamp(std::nearbyint(double(v) / from * to), 0.0, double(to));
                return std::min(int(s), limit);
            };
            DetBox b;
            b.x0 = scale(ux0, w, destW, destW - 1);
            b.x1 = scale(ux1, w, destW, destW - 1);
            b.y0 = scale(uy0, h, destH, destH - 1);
            b.y1 = scale(uy1, h, destH, destH - 1);
            if (b.x1 - b.x0 <= 3 || b.y1 - b.y0 <= 3)
                continue;
            b.score = score;
            b.pad = float(d * destH / h);
            out.append(b);
        }
    }
    return out;
}

Line ctcDecode(const float *probs, int steps, int classes, const QStringList &dict)
{
    Line l;
    double sum = 0;
    int prev = -1;
    for (int t = 0; t < steps; ++t) {
        const float *p = probs + size_t(t) * classes;
        const int best = int(std::max_element(p, p + classes) - p);
        // Drop blanks and repeats of the previous timestep.
        if (best != 0 && best != prev) {
            l.chars << (best <= dict.size() ? dict[best - 1] : QStringLiteral(" "));
            l.cols << t;
            sum += p[best];
        }
        prev = best;
    }
    l.score = l.cols.isEmpty() ? 0.f : float(sum / l.cols.size());
    return l;
}

void appendGlyphs(QVector<OcrWord> &out, const Line &line, const QRect &box, double steps,
                  int row, bool spaceBefore)
{
    const int n = line.chars.size();
    if (n == 0 || steps <= 0 || box.isEmpty())
        return;
    const auto toX = [&](double step) {
        return box.left() + int(std::lround(std::clamp(step, 0.0, steps) / steps * box.width()));
    };
    // A char is read around its timestep's centre; its edges sit halfway to
    // the next char's. Word ends reach out by half the word's pitch.
    bool space = spaceBefore;
    for (int i = 0; i < n;) {
        if (line.chars[i] == QLatin1String(" ")) {
            space = true;
            ++i;
            continue;
        }
        int j = i;
        while (j < n && line.chars[j] != QLatin1String(" "))
            ++j;
        const double pitch = j - i > 1 ? double(line.cols[j - 1] - line.cols[i]) / (j - i - 1) : 2.0;
        for (int k = i; k < j; ++k) {
            const double c = line.cols[k] + 0.5;
            const double left = k == i ? c - pitch / 2 : (line.cols[k - 1] + 0.5 + c) / 2;
            const double right = k == j - 1 ? c + pitch / 2 : (c + line.cols[k + 1] + 0.5) / 2;
            OcrWord g;
            const int x0 = toX(left), x1 = toX(right);
            g.rect = QRect(x0, box.top(), std::max(1, x1 - x0), box.height());
            g.text = line.chars[k];
            g.line = row;
            g.confidence = 100.f * line.score;
            g.spaceBefore = space && k == i;
            out.append(g);
        }
        space = false;
        i = j;
    }
}

QVector<QVector<int>> groupRows(const QVector<QRect> &boxes)
{
    // The order the benchmark's rows_text() used: by centre, height, left.
    QVector<int> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    const auto yc = [&](int i) { return boxes[i].top() + boxes[i].height() / 2.0; };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (yc(a) != yc(b))
            return yc(a) < yc(b);
        if (boxes[a].height() != boxes[b].height())
            return boxes[a].height() < boxes[b].height();
        return boxes[a].left() < boxes[b].left();
    });
    QVector<QVector<int>> rows;
    QVector<int> heads;
    for (int i : order) {
        if (!rows.isEmpty()) {
            const int h = heads.last();
            if (std::abs(yc(i) - yc(h)) < 0.5 * std::max(boxes[i].height(), boxes[h].height())) {
                rows.last().append(i);
                continue;
            }
        }
        rows.append(QVector<int>{i});
        heads.append(i);
    }
    for (QVector<int> &r : rows)
        std::stable_sort(r.begin(), r.end(), [&](int a, int b) { return boxes[a].left() < boxes[b].left(); });
    return rows;
}

bool recognize(const Setup &s, const QImage &img, const std::atomic_bool &cancelled,
               QVector<OcrWord> *glyphs, QString *text, QString *why, int threads)
{
    if (!s.api || !s.env) {
        *why = QStringLiteral("ONNX Runtime is not loaded");
        return false;
    }
    const QImage src = img.convertToFormat(QImage::Format_RGB888);
    const int W = src.width(), H = src.height();
    if (W < 1 || H < 1) {
        *why = QStringLiteral("empty image");
        return false;
    }

    // Global pre-processing: long side down to 2000, short side up to 30,
    // both rounded to multiples of 32; then black bands above and below a
    // picture that is short or wider than 8:1.
    QImage pre = src;
    if (std::max(W, H) > kMaxSide) {
        const double r = double(kMaxSide) / std::max(W, H);
        pre = resizeLinear(pre, round32(int(W * r)), round32(int(H * r)));
    }
    if (std::min(pre.width(), pre.height()) < kMinSide) {
        const double r = double(kMinSide) / std::min(pre.width(), pre.height());
        pre = resizeLinear(pre, round32(int(pre.width() * r)), round32(int(pre.height() * r)));
    }
    const double rw = double(W) / pre.width(), rh = double(H) / pre.height();
    int padTop = 0;
    if (pre.height() <= kMinHeight || double(pre.width()) / pre.height() > kWideRatio) {
        const int newH = std::max(int(pre.width() / kWideRatio), kMinHeight) * 2;
        padTop = std::abs(newH - pre.height()) / 2;
        QImage padded(pre.width(), pre.height() + 2 * padTop, QImage::Format_RGB888);
        padded.fill(Qt::black);
        for (int y = 0; y < pre.height(); ++y)
            std::copy_n(pre.constScanLine(y), pre.width() * 3, padded.scanLine(y + padTop));
        pre = padded;
    }

    // Detection input: at the picture's own size (long side at most 2048,
    // which the 2000 px cap above already ensures), multiples of 32.
    const double dr = std::max(pre.width(), pre.height()) > kDetMaxSide
                          ? double(kDetMaxSide) / std::max(pre.width(), pre.height()) : 1.0;
    const int dw = round32(int(pre.width() * dr)), dh = round32(int(pre.height() * dr));

    Runner runner(s, why);
    if (!runner.load(threads) || cancelled.load())
        return false;

    QVector<DetBox> boxes;
    {
        const QImage det = resizeLinear(pre, dw, dh);
        std::vector<float> input(size_t(3) * dw * dh);
        toBgrPlanes(det, input.data(), dw);
        Output map;
        if (!runner.run(true, input, {1, 3, dh, dw}, map))
            return false;
        if (map.shape.size() != 4 || map.shape[2] != dh || map.shape[3] != dw) {
            *why = QStringLiteral("unexpected detection output");
            return false;
        }
        boxes = boxesFromMap(map.data, dw, dh, pre.width(), pre.height());
    }
    if (cancelled.load())
        return false;

    // Back to the original picture, where the crops come from at full
    // resolution (RapidOCR crops the scaled copy and scales the crops back).
    QVector<Read> reads;
    for (const DetBox &b : std::as_const(boxes)) {
        const auto mx = [&](double x) { return std::clamp(int(std::lround(x * rw)), 0, W); };
        const auto my = [&](double y) { return std::clamp(int(std::lround((y - padTop) * rh)), 0, H); };
        const QRect r(QPoint(mx(b.x0), my(b.y0)), QPoint(mx(b.x1) - 1, my(b.y1) - 1));
        if (r.width() < 1 || r.height() < 1)
            continue;
        Read rd;
        rd.box = r;
        rd.pad = float(b.pad * rh);
        reads.append(rd);
    }

    // Recognition: crops sorted by aspect ratio, six at a time, each scaled to
    // 48 px high and padded (with zeros, i.e. mid-grey) to the widest of its batch.
    std::vector<QImage> crops(reads.size());
    std::vector<double> ratio(reads.size());
    for (int i = 0; i < reads.size(); ++i) {
        crops[i] = src.copy(reads[i].box);
        // get_rotate_crop_image: a crop 1.5 times taller than wide is read as
        // vertical text.
        if (crops[i].height() >= 1.5 * crops[i].width()) {
            crops[i] = rotateCcw(crops[i]);
            reads[i].rotated = true;
        }
        ratio[i] = double(crops[i].width()) / crops[i].height();
    }
    std::vector<int> order(reads.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return ratio[a] < ratio[b]; });
    for (size_t beg = 0; beg < order.size(); beg += kRecBatch) {
        if (cancelled.load())
            return false;
        const size_t end = std::min(order.size(), beg + kRecBatch);
        double maxRatio = double(kRecWidth) / kRecHeight;
        for (size_t k = beg; k < end; ++k)
            maxRatio = std::max(maxRatio, ratio[order[k]]);
        const int iw = int(kRecHeight * maxRatio);
        const int n = int(end - beg);
        std::vector<float> input(size_t(n) * 3 * kRecHeight * iw, 0.f);
        for (int k = 0; k < n; ++k) {
            const int i = order[beg + k];
            const int rwid = std::min(iw, int(std::ceil(kRecHeight * ratio[i])));
            toBgrPlanes(resizeLinear(crops[i], rwid, kRecHeight),
                        input.data() + size_t(k) * 3 * kRecHeight * iw, iw);
        }
        Output out;
        if (!runner.run(false, input, {n, 3, kRecHeight, iw}, out))
            return false;
        if (out.shape.size() != 3 || out.shape[0] != n
            || out.shape[2] != runner.dict().size() + 2) {
            *why = QStringLiteral("unexpected recognition output");
            return false;
        }
        const int T = int(out.shape[1]), C = int(out.shape[2]);
        for (int k = 0; k < n; ++k) {
            const int i = order[beg + k];
            reads[i].line = ctcDecode(out.data + size_t(k) * T * C, T, C, runner.dict());
            reads[i].steps = T * ratio[i] / maxRatio;
        }
    }

    // Lines read with too little confidence, or nothing at all, are dropped.
    QVector<Read> kept;
    for (const Read &r : std::as_const(reads))
        if (r.line.score >= kTextScore && !r.line.text().trimmed().isEmpty())
            kept.append(r);
    QVector<QRect> rects;
    for (const Read &r : std::as_const(kept))
        rects.append(r.box);
    const QVector<QVector<int>> rows = groupRows(rects);

    if (text) {
        QStringList lines;
        for (const QVector<int> &row : rows) {
            QStringList parts;
            for (int i : row)
                parts << kept[i].line.text();
            lines << parts.join(QLatin1Char(' '));
        }
        *text = lines.join(QLatin1Char('\n')).trimmed();
    }
    if (glyphs) {
        glyphs->clear();
        for (int ri = 0; ri < rows.size(); ++ri) {
            for (int k = 0; k < rows[ri].size(); ++k) {
                const Read &r = kept[rows[ri][k]];
                // The unclip grew the crop past the letters by about half its
                // margin (DB shrinks text by 0.84 * area / perimeter when it
                // trains and the unclip grows by 1.6), so glyph boxes keep
                // the inner part and adjacent lines do not overlap.
                const int inset = std::min(int(r.pad / 2), (r.box.height() - 1) / 2);
                if (r.rotated) {
                    // Read bottom to top: one box per glyph stacked upwards.
                    QVector<OcrWord> flat;
                    appendGlyphs(flat, r.line, QRect(0, 0, r.box.height(), r.box.width()), r.steps, ri, k > 0);
                    for (OcrWord &g : flat) {
                        g.rect = QRect(r.box.left(), r.box.bottom() + 1 - (g.rect.left() + g.rect.width()),
                                       r.box.width(), g.rect.width());
                        glyphs->append(g);
                    }
                } else {
                    appendGlyphs(*glyphs, r.line, r.box.adjusted(0, inset, 0, -inset), r.steps, ri, k > 0);
                }
            }
        }
    }
    return true;
}

} // namespace PaddleOcr

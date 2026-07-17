#include "PipeWireGrabber.h"
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/param/param.h>     // enum spa_param (SPA_PARAM_Meta)
// enum spa_param_meta (SPA_PARAM_META_type / _size) lives in buffers.h. Some
// distro SPA packages (older/reduced libpipewire-0.3-dev on the CI runners)
// ship spa/param/video/* but not spa/param/buffers.h, so guard the include and
// fall back to the ABI-stable ordinals — the enum has only ever lived in
// buffers.h, so param.h can never define them and this can't clash.
#if __has_include(<spa/param/buffers.h>)
#  include <spa/param/buffers.h>
#else
enum { SPA_PARAM_META_type = 1, SPA_PARAM_META_size = 2 };
#endif
#include <spa/buffer/meta.h>     // spa_meta_{header,cursor,bitmap}
#include <spa/pod/builder.h>
#include <QDebug>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

// Cap on undrained cursor samples: ~200k is ~1h at 60fps of one sample/frame,
// a safety valve if a consumer forgets to drain — we drop the oldest quarter
// and warn once rather than growing without bound on the PipeWire thread.
static constexpr int kMaxCursorSamples = 200000;

// spa_meta sizes we ask the compositor to reserve for the cursor bitmap. The
// default covers a 64x64 RGBA cursor; the RANGE lets big themes (up to 512x512)
// still attach their bitmap instead of dropping it.
static constexpr int kCursorMetaSizeMin =
    int(sizeof(struct spa_meta_cursor));
static constexpr int kCursorMetaSizeDefault =
    int(sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + 64 * 64 * 4);
static constexpr int kCursorMetaSizeMax =
    int(sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + 512 * 512 * 4);

// Decode a spa_meta_bitmap into a deep-copied QImage (owns its pixels). Returns
// a null image for formats we do not handle or a malformed/empty bitmap. Cursor
// bitmaps are tiny, so the mapping below is chosen for a little-endian host
// (x86/arm64): BGRA/BGRx -> the word-ordered ARGB32*/RGB32 whose in-memory bytes
// are B,G,R,(A/x); RGBA/RGBx -> the byte-ordered RGBA/RGBX8888.
//
// The alpha is PREMULTIPLIED: both Wayland's ARGB8888 buffers and the XCursor
// files the themes ship store it that way, and the compositor hands the cursor
// through unchanged. Mapping it to a straight-alpha format instead makes every
// later convertToFormat() premultiply it a SECOND time, which eats the
// antialiased outline and is exactly what made the recorded pointer look soft.
static QImage cursorBitmapToImage(const struct spa_meta_bitmap *bm)
{
    if (!bm || bm->format == 0 || bm->offset < sizeof(struct spa_meta_bitmap))
        return {};
    const int w = int(bm->size.width);
    const int h = int(bm->size.height);
    if (w <= 0 || h <= 0 || w > 512 || h > 512)
        return {};
    const int stride = bm->stride;
    if (stride < w * 4)
        return {};
    QImage::Format fmt;
    switch (bm->format) {
    case SPA_VIDEO_FORMAT_BGRA: fmt = QImage::Format_ARGB32_Premultiplied; break;  // mem: B,G,R,A
    case SPA_VIDEO_FORMAT_BGRx: fmt = QImage::Format_RGB32; break;       // mem: B,G,R,x (opaque)
    case SPA_VIDEO_FORMAT_RGBA: fmt = QImage::Format_RGBA8888_Premultiplied; break; // mem: R,G,B,A
    case SPA_VIDEO_FORMAT_RGBx: fmt = QImage::Format_RGBX8888; break;    // mem: R,G,B,x (opaque)
    default: return {};
    }
    const auto *pixels = SPA_PTROFF(bm, bm->offset, const uint8_t);
    // The QImage ctor only wraps `pixels`; copy() detaches into owned storage so
    // the image outlives this PipeWire buffer.
    return QImage(pixels, w, h, stride, fmt).copy();
}

struct GrabberEvents {
    pw_stream_events events = {};
    spa_hook hook = {};
    PipeWireGrabber *self = nullptr;
    spa_video_info format = {};
    bool haveFormat = false;
};

static void on_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
    auto *ev = static_cast<GrabberEvents *>(data);
    ev->self->onParamChanged(id, param);
}

static void on_process(void *data)
{
    auto *ev = static_cast<GrabberEvents *>(data);
    ev->self->onProcess();
}

static void on_state_changed(void *data, enum pw_stream_state, enum pw_stream_state state,
                             const char *error)
{
    auto *ev = static_cast<GrabberEvents *>(data);
    if (state == PW_STREAM_STATE_ERROR)
        emit ev->self->streamError(QString::fromUtf8(error ? error : "PipeWire stream error"));
}

PipeWireGrabber::PipeWireGrabber(QObject *parent) : QObject(parent)
{
    pw_init(nullptr, nullptr);
}

PipeWireGrabber::~PipeWireGrabber()
{
    stop();
    // pw_init() is process-global. Balance every recording grabber's
    // initialization so repeated start/stop cycles do not retain PipeWire
    // global resources for the rest of the tray application's lifetime.
    pw_deinit();
}

bool PipeWireGrabber::start(int pipewireFd, uint nodeId, int maxFps, bool wantCursorMeta)
{
    m_wantCursorMeta = wantCursorMeta;
    m_loop = pw_thread_loop_new("unisic-pipewire", nullptr);
    if (!m_loop) {
        close(pipewireFd);
        return false;
    }

    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        close(pipewireFd);
        stop();
        return false;
    }
    if (pw_thread_loop_start(m_loop) != 0) {
        qWarning() << "pw_thread_loop_start failed";
        close(pipewireFd);
        stop();
        return false;
    }

    pw_thread_loop_lock(m_loop);
    const int dupFd = fcntl(pipewireFd, F_DUPFD_CLOEXEC, 3);
    if (dupFd < 0) {
        pw_thread_loop_unlock(m_loop);
        close(pipewireFd);
        stop();
        return false;
    }
    m_core = pw_context_connect_fd(m_context, dupFd, nullptr, 0);
    if (!m_core) {
        close(dupFd);
        pw_thread_loop_unlock(m_loop);
        close(pipewireFd);
        stop();
        qWarning() << "pw_context_connect_fd failed";
        return false;
    }

    auto *ev = new GrabberEvents;
    m_listener = ev;
    ev->self = this;
    ev->events.version = PW_VERSION_STREAM_EVENTS;
    ev->events.param_changed = on_param_changed;
    ev->events.process = on_process;
    ev->events.state_changed = on_state_changed;

    m_stream = pw_stream_new(m_core, "unisic-capture",
                             pw_properties_new(PW_KEY_MEDIA_TYPE, "Video",
                                               PW_KEY_MEDIA_CATEGORY, "Capture",
                                               PW_KEY_MEDIA_ROLE, "Screen", nullptr));
    if (!m_stream) {
        pw_thread_loop_unlock(m_loop);
        close(pipewireFd);
        stop();
        return false;
    }
    pw_stream_add_listener(m_stream, &ev->hook, &ev->events, ev);

    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_rectangle defSize = SPA_RECTANGLE(1920, 1080);
    const spa_rectangle minSize = SPA_RECTANGLE(1, 1);
    const spa_rectangle maxSize = SPA_RECTANGLE(16384, 16384);
    // Cap negotiated delivery at the sampling rate: KWin/Mutter honor the max
    // and throttle, so onProcess isn't run at monitor refresh for frames the
    // fixed-FPS sampler would only discard.
    const int cappedFps = qBound(1, maxFps, 240);
    const spa_fraction defRate = SPA_FRACTION(cappedFps, 1);
    const spa_fraction minRate = SPA_FRACTION(0, 1);
    const spa_fraction maxRate = SPA_FRACTION(cappedFps, 1);
    const spa_pod *params[1];
    params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5,
            SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
            SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate)));

    int res = pw_stream_connect(m_stream, PW_DIRECTION_INPUT, nodeId,
                                static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                                             PW_STREAM_FLAG_MAP_BUFFERS),
                                params, 1);
    pw_thread_loop_unlock(m_loop);
    close(pipewireFd);

    if (res < 0) {
        qWarning() << "pw_stream_connect failed:" << res;
        stop();
        return false;
    }
    return true;
}

void PipeWireGrabber::stop()
{
    if (!m_loop)
        return;
    pw_thread_loop_lock(m_loop);
    if (m_stream) {
        pw_stream_disconnect(m_stream);
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    if (m_core) {
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }
    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_stop(m_loop);
    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }
    pw_thread_loop_destroy(m_loop);
    m_loop = nullptr;
    delete static_cast<GrabberEvents *>(m_listener);
    m_listener = nullptr;
}

void PipeWireGrabber::onParamChanged(uint32_t id, const void *param)
{
    if (id != SPA_PARAM_Format || !param)
        return;
    auto *ev = static_cast<GrabberEvents *>(m_listener);
    if (spa_format_parse(static_cast<const spa_pod *>(param),
                         &ev->format.media_type, &ev->format.media_subtype) < 0)
        return;
    if (ev->format.media_type != SPA_MEDIA_TYPE_video ||
        ev->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;
    if (spa_format_video_raw_parse(static_cast<const spa_pod *>(param),
                                   &ev->format.info.raw) < 0)
        return;

    ev->haveFormat = true;
    m_format = ev->format.info.raw.format;
    const QSize size(int(ev->format.info.raw.size.width), int(ev->format.info.raw.size.height));
    m_size = size;

    // Format is where we also announce the per-buffer metadata we want attached.
    // Only when cursor capture was requested: with it off this is a no-op and
    // the stream negotiates exactly as before. SPA_PARAM_Meta is a different
    // param id than Format/Buffers, so this adds meta without disturbing them.
    if (m_wantCursorMeta && m_stream) {
        uint8_t buf[512];
        spa_pod_builder mb = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        const spa_pod *metaParams[2];
        metaParams[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&mb,
            SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
            SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
            SPA_PARAM_META_size, SPA_POD_Int(int(sizeof(struct spa_meta_header)))));
        metaParams[1] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&mb,
            SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
            SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Cursor),
            SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(
                kCursorMetaSizeDefault, kCursorMetaSizeMin, kCursorMetaSizeMax)));
        // Called on the PipeWire thread from the param_changed callback — the
        // supported place to update params, no extra loop lock needed.
        pw_stream_update_params(m_stream, metaParams, 2);
    }

    // Queued: we're on the PipeWire thread.
    QMetaObject::invokeMethod(this, [this, size] { emit formatReady(size); }, Qt::QueuedConnection);
}

void PipeWireGrabber::onProcess()
{
    if (!m_stream)
        return;
    pw_buffer *b = pw_stream_dequeue_buffer(m_stream);
    if (!b)
        return;

    spa_buffer *buf = b->buffer;

    // Cursor metadata (opt-in). Handled BEFORE and independently of frame
    // validity: a corrupted/undersized video frame can still carry good cursor
    // meta, and the early-out below requeues the buffer.
    if (m_wantCursorMeta) {
        // Presentation time for this buffer. Fallback chain: header pts (when
        // the compositor set one) -> the stream clock -> the monotonic clock
        // read directly. All three are in the CLOCK_MONOTONIC domain.
        qint64 ptsNs = 0;
        if (auto *hdr = static_cast<spa_meta_header *>(
                spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(struct spa_meta_header))))
            if (hdr->pts != 0)
                ptsNs = hdr->pts;
        if (ptsNs == 0) {
            struct pw_time t = {};
            if (pw_stream_get_time_n(m_stream, &t, sizeof(t)) == 0 && t.now > 0)
                ptsNs = t.now;
        }
        if (ptsNs == 0) {
            struct timespec ts = {};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ptsNs = qint64(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
        }

        if (auto *cur = static_cast<spa_meta_cursor *>(
                spa_buffer_find_meta_data(buf, SPA_META_Cursor, sizeof(struct spa_meta_cursor)))) {
            // id==0 means "no new cursor data" per spa: cursor state is
            // unavailable this frame, so record a not-visible sample carrying
            // the last known position (so the consumer's interpolation holds
            // rather than jumping to the origin). Some compositors also signal
            // "hidden" by parking the pointer at an absurd coordinate instead of
            // dropping the meta, so a position outside [-16384, 16384] is
            // likewise treated as not-visible.
            const bool haveShape = cur->id != 0;
            const bool inRange = cur->position.x >= -16384 && cur->position.x <= 16384 &&
                                 cur->position.y >= -16384 && cur->position.y <= 16384;
            const bool visible = haveShape && inRange;
            CursorSample s;
            s.tMonoNs = ptsNs;
            s.shapeId = haveShape ? int(cur->id) : 0;
            s.visible = visible;
            if (visible) {
                m_lastCursorX = cur->position.x;
                m_lastCursorY = cur->position.y;
            }
            s.x = m_lastCursorX;
            s.y = m_lastCursorY;

            // A non-zero bitmap_offset means a NEW bitmap is attached this frame:
            // the compositor sets it precisely when the pointer shape changes
            // (arrow → hand → I-beam …). Decode on EVERY such frame — do not gate
            // on "id not seen before". KWin reuses one cursor id and swaps the
            // bitmap in place, so an id-seen check froze the pointer on the first
            // shape (the arrow) for the whole recording. The consumer re-keys by
            // id, so re-emitting a known id just refreshes its bitmap.
            if (haveShape && cur->bitmap_offset != 0) {
                const auto *bm = SPA_PTROFF(cur, cur->bitmap_offset, const struct spa_meta_bitmap);
                QImage img = cursorBitmapToImage(bm);
                if (!img.isNull())
                    // Direct emit off the PipeWire thread; QImage/QPoint/int are
                    // metatypes, so a queued/auto connection marshals safely.
                    emit cursorShapeChanged(s.shapeId, img,
                                            QPoint(cur->hotspot.x, cur->hotspot.y));
            }

            QMutexLocker lock(&m_mutex);
            if (m_cursorSamples.size() >= kMaxCursorSamples) {
                // Consumer never drained: drop the oldest quarter in one shot
                // (amortized O(1) vs. per-append shifting) and warn once.
                m_cursorSamples.remove(0, kMaxCursorSamples / 4);
                if (!m_cursorOverflowWarned) {
                    qWarning() << "PipeWireGrabber: cursor sample buffer overflow, dropping oldest";
                    m_cursorOverflowWarned = true;
                }
            }
            m_cursorSamples.append(s);
        }
    }

    if (buf->n_datas > 0 && buf->datas[0].data && buf->datas[0].chunk && m_size.isValid()) {
        const int w = m_size.width();
        const int h = m_size.height();
        const int rowBytes = w * 4;
        const spa_data &data = buf->datas[0];
        const spa_chunk *chunk = data.chunk;
        const int stride = chunk->stride > 0 ? chunk->stride : rowBytes;
        const uint32_t offset = chunk->offset;
        const qsizetype frameBytes = qsizetype(h - 1) * stride + rowBytes;
        const qsizetype required = qsizetype(offset) + frameBytes;
        if (chunk->size == 0
            || (chunk->flags & SPA_CHUNK_FLAG_CORRUPTED)
            || stride < rowBytes
            || (data.maxsize > 0 && required > qsizetype(data.maxsize))
            || frameBytes > qsizetype(chunk->size)) {
            // Zero-size / corrupted / undersized buffer: requeue and keep the
            // previous frame (the sampler already sample-and-holds).
            pw_stream_queue_buffer(m_stream, b);
            return;
        }
        const auto *src = static_cast<const uint8_t *>(data.data) + offset;

        // Copy into a back buffer WITHOUT the lock — a 4K row copy takes
        // milliseconds and must never block the GUI thread's latestFrame(); the
        // pool is touched only on this (PipeWire) thread. Rotation pool: with
        // the consumer pinning the published frame (sample-and-hold) plus
        // m_latest, one of three buffers is always detached, so steady-state
        // full-screen capture stops reallocating a frame-sized block per frame.
        // Pick the first detached, right-sized slot; else reuse any detached
        // slot (reallocating). Never data() a shared buffer — that would memcpy
        // the FULL stale frame just to detach; a fresh slot skips that copy.
        const qsizetype need = qsizetype(rowBytes) * h;
        QByteArray *back = nullptr;
        for (QByteArray &slot : m_pool) {
            if (slot.size() == need && slot.isDetached()) {
                back = &slot;
                break;
            }
        }
        if (!back) {
            for (QByteArray &slot : m_pool) {
                if (slot.isDetached()) { back = &slot; break; }
            }
            if (!back)
                back = &m_pool[0];
            *back = QByteArray(need, Qt::Uninitialized);
        }
        // Keep the native byte order — the encoder's -pix_fmt (pixelFormat())
        // matches it, so no swizzle/alpha-fill is needed; the row memcpy also
        // drops any padding stride down to a tightly packed frame.
        char *dst = back->data();
        for (int y = 0; y < h; ++y) {
            memcpy(dst, src + qsizetype(y) * stride, rowBytes);
            dst += rowBytes;
        }
        {
            QMutexLocker lock(&m_mutex);
            m_latest = *back;
            ++m_seq;
        }
        m_haveFrame.store(true, std::memory_order_release);
    }
    pw_stream_queue_buffer(m_stream, b);
}

bool PipeWireGrabber::latestFrame(QByteArray &out, quint64 *seq)
{
    if (!m_haveFrame.load(std::memory_order_acquire))
        return false;
    QMutexLocker lock(&m_mutex);
    // Cheap shared reference: the front buffer is only ever *replaced* by the
    // swap in onProcess(), never written in place, so no deep copy is needed.
    out = m_latest;
    if (seq)
        *seq = m_seq;
    return true;
}

QVector<CursorSample> PipeWireGrabber::takeCursorSamples()
{
    QMutexLocker lock(&m_mutex);
    QVector<CursorSample> out;
    out.swap(m_cursorSamples);
    return out;
}

QString PipeWireGrabber::pixelFormat() const
{
    // Native SPA byte order -> ffmpeg rawvideo pix_fmt (onProcess no longer
    // swizzles): the "x" formats map to the *0 variants that ignore the padding.
    switch (m_format) {
    case SPA_VIDEO_FORMAT_BGRA: return QStringLiteral("bgra");
    case SPA_VIDEO_FORMAT_RGBx: return QStringLiteral("rgb0");
    case SPA_VIDEO_FORMAT_RGBA: return QStringLiteral("rgba");
    case SPA_VIDEO_FORMAT_BGRx:
    default:                    return QStringLiteral("bgr0");
    }
}

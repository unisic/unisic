#include "PipeWireGrabber.h"
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/pod/builder.h>
#include <QDebug>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

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
}

bool PipeWireGrabber::start(int pipewireFd, uint nodeId)
{
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
    const spa_fraction defRate = SPA_FRACTION(30, 1);
    const spa_fraction minRate = SPA_FRACTION(0, 1);
    const spa_fraction maxRate = SPA_FRACTION(240, 1);
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
        if (stride < rowBytes
            || (data.maxsize > 0 && required > qsizetype(data.maxsize))
            || (chunk->size > 0 && frameBytes > qsizetype(chunk->size))) {
            pw_stream_queue_buffer(m_stream, b);
            return;
        }
        const auto *src = static_cast<const uint8_t *>(data.data) + offset;

        QMutexLocker lock(&m_mutex);
        m_latest.resize(qsizetype(rowBytes) * h);
        char *dst = m_latest.data();
        const bool swapRB = (m_format == SPA_VIDEO_FORMAT_RGBx || m_format == SPA_VIDEO_FORMAT_RGBA);
        const bool hasAlpha = (m_format == SPA_VIDEO_FORMAT_BGRA || m_format == SPA_VIDEO_FORMAT_RGBA);
        for (int y = 0; y < h; ++y) {
            const uint8_t *row = src + qsizetype(y) * stride;
            if (!swapRB) {
                memcpy(dst, row, rowBytes);
                if (!hasAlpha) {
                    for (int x = 0; x < w; ++x)
                        dst[x * 4 + 3] = char(0xff);
                }
            } else {
                for (int x = 0; x < w; ++x) { // RGBx -> BGRx
                    dst[x * 4 + 0] = char(row[x * 4 + 2]);
                    dst[x * 4 + 1] = char(row[x * 4 + 1]);
                    dst[x * 4 + 2] = char(row[x * 4 + 0]);
                    dst[x * 4 + 3] = hasAlpha ? char(row[x * 4 + 3]) : char(0xff);
                }
            }
            dst += rowBytes;
        }
        m_haveFrame.store(true, std::memory_order_release);
    }
    pw_stream_queue_buffer(m_stream, b);
}

bool PipeWireGrabber::latestFrame(QByteArray &out)
{
    if (!m_haveFrame.load(std::memory_order_acquire))
        return false;
    QMutexLocker lock(&m_mutex);
    out = m_latest; // implicit share; detached on next writer resize? m_latest is overwritten in-place,
    out.detach();   // so force a deep copy while we hold the lock.
    return true;
}

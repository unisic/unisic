#include "KWinScreenShot2.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <unistd.h>
#include <fcntl.h>

bool KWinScreenShot2::isAvailable()
{
    auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(QStringLiteral("org.kde.KWin"));
}

struct PipeImageResult {
    QImage image;
    QString error;
};

static PipeImageResult readImageFromPipe(int fd, const QVariantMap &meta)
{
    PipeImageResult r;
    const uint width = meta.value(QStringLiteral("width")).toUInt();
    const uint height = meta.value(QStringLiteral("height")).toUInt();
    const uint stride = meta.value(QStringLiteral("stride")).toUInt();
    const uint format = meta.value(QStringLiteral("format")).toUInt();
    const qreal scale = meta.value(QStringLiteral("scale"), 1.0).toReal();

    if (!width || !height || !stride || format == QImage::Format_Invalid) {
        r.error = QStringLiteral("KWin returned invalid image metadata");
        close(fd);
        return r;
    }

    QImage img(width, height, static_cast<QImage::Format>(format));
    if (img.isNull()) {
        r.error = QStringLiteral("Failed to allocate image buffer");
        close(fd);
        return r;
    }

    const qsizetype rowBytes = qMin<qsizetype>(stride, img.bytesPerLine());
    QByteArray row(stride, Qt::Uninitialized);
    for (uint y = 0; y < height; ++y) {
        qsizetype got = 0;
        while (got < qsizetype(stride)) {
            ssize_t n = read(fd, row.data() + got, stride - got);
            if (n <= 0) {
                if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                r.error = QStringLiteral("Short read from KWin screenshot pipe");
                close(fd);
                return r;
            }
            got += n;
        }
        memcpy(img.scanLine(y), row.constData(), rowBytes);
    }
    close(fd);
    img.setDevicePixelRatio(scale > 0 ? scale : 1.0);
    r.image = img;
    return r;
}

void KWinScreenShot2::call(const QString &method, const QVariantList &args, bool includeCursor, Callback cb)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        cb({}, QStringLiteral("pipe2() failed"));
        return;
    }

    QVariantMap options{
        {QStringLiteral("include-cursor"), includeCursor},
        {QStringLiteral("include-decoration"), true},
        {QStringLiteral("include-shadow"), false},
        {QStringLiteral("native-resolution"), true},
    };

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/ScreenShot2"),
        QStringLiteral("org.kde.KWin.ScreenShot2"),
        method);
    QVariantList fullArgs = args;
    fullArgs << options << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    msg.setArguments(fullArgs);

    QDBusPendingCall call = QDBusConnection::sessionBus().asyncCall(msg, 30000);
    close(fds[1]); // KWin holds its own dup of the write end now (sent with the message)
    int readFd = fds[0];

    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, readFd, cb](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            close(readFd);
            cb({}, reply.error().name() + QStringLiteral(": ") + reply.error().message());
            return;
        }
        const QVariantMap meta = reply.value();
        auto *fw = new QFutureWatcher<PipeImageResult>(this);
        connect(fw, &QFutureWatcher<PipeImageResult>::finished, this, [fw, cb]() {
            PipeImageResult r = fw->result();
            fw->deleteLater();
            cb(r.image, r.error);
        });
        fw->setFuture(QtConcurrent::run(readImageFromPipe, readFd, meta));
    });
}

void KWinScreenShot2::captureWorkspace(bool includeCursor, Callback cb)
{
    call(QStringLiteral("CaptureWorkspace"), {}, includeCursor, std::move(cb));
}

void KWinScreenShot2::captureScreen(const QString &screenName, bool includeCursor, Callback cb)
{
    call(QStringLiteral("CaptureScreen"), {screenName}, includeCursor, std::move(cb));
}

void KWinScreenShot2::captureActiveWindow(bool includeCursor, Callback cb)
{
    call(QStringLiteral("CaptureActiveWindow"), {}, includeCursor, std::move(cb));
}

void KWinScreenShot2::captureArea(int x, int y, int w, int h, bool includeCursor, Callback cb)
{
    call(QStringLiteral("CaptureArea"), {x, y, uint(w), uint(h)}, includeCursor, std::move(cb));
}

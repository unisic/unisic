#include "GrimScreenshot.h"
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QFutureWatcher>

bool GrimScreenshot::isAvailable()
{
    return !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty()
           && !QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty();
}

void GrimScreenshot::captureWorkspace(bool includeCursor, Callback cb)
{
    auto *proc = new QProcess(this);
    QStringList args{QStringLiteral("-t"), QStringLiteral("png")};
    if (includeCursor)
        args << QStringLiteral("-c");
    args << QStringLiteral("-"); // PNG to stdout

    connect(proc, &QProcess::finished, this,
            [this, proc, cb](int code, QProcess::ExitStatus) {
        const QByteArray png = proc->readAllStandardOutput();
        const QString errOut = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (code != 0 || png.isEmpty()) {
            cb({}, errOut.isEmpty()
                       ? QStringLiteral("grim exited with code %1").arg(code)
                       : QStringLiteral("grim: %1").arg(errOut));
            return;
        }
        // Multi-monitor 4K PNGs take a while to decode — keep it off the GUI
        // thread (same pattern as GnomeScreenshot).
        auto *fw = new QFutureWatcher<QImage>(this);
        connect(fw, &QFutureWatcher<QImage>::finished, this, [fw, cb]() {
            const QImage img = fw->result();
            fw->deleteLater();
            if (img.isNull())
                cb({}, QStringLiteral("grim produced no readable image"));
            else
                cb(img, {});
        });
        fw->setFuture(QtConcurrent::run([png]() {
            QImage img;
            img.loadFromData(png, "PNG");
            return img;
        }));
    });
    // FailedToStart never emits finished() — without this the callback is
    // simply lost and the capture flow hangs.
    connect(proc, &QProcess::errorOccurred, this,
            [proc, cb](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        cb({}, QStringLiteral("could not run grim"));
    });
    proc->start(QStringLiteral("grim"), args);
}

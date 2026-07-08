#include "GrimScreenshot.h"
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>

bool GrimScreenshot::isAvailable()
{
    return !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty()
           && !QStandardPaths::findExecutable(QStringLiteral("grim")).isEmpty();
}

void GrimScreenshot::captureWorkspace(bool includeCursor, Callback cb)
{
    // -l 1: fastest PNG compression — encode time dominates the capture and
    // the image is decoded and discarded right away.
    QStringList args{QStringLiteral("-t"), QStringLiteral("png"), QStringLiteral("-l"), QStringLiteral("1")};
    if (includeCursor)
        args << QStringLiteral("-c");
    args << QStringLiteral("-"); // PNG to stdout
    run(args, std::move(cb));
}

void GrimScreenshot::captureOutput(const QString &outputName, bool includeCursor, Callback cb)
{
    QStringList args{QStringLiteral("-t"), QStringLiteral("png"), QStringLiteral("-l"), QStringLiteral("1"),
                     QStringLiteral("-o"), outputName};
    if (includeCursor)
        args << QStringLiteral("-c");
    args << QStringLiteral("-");
    run(args, std::move(cb));
}

void GrimScreenshot::run(const QStringList &args, Callback cb)
{
    auto *proc = new QProcess(this);

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
    // Watchdog, same 30 s discipline as every other backend: grim blocked on a
    // screencopy frame that never comes (locked session, DPMS-off output)
    // would otherwise never fire finished() — the lost callback permanently
    // wedges every capture entry point (m_captureInFlight / overlay active()).
    // kill() makes finished(code, CrashExit) deliver the error instead.
    QTimer::singleShot(30000, proc, [proc] { proc->kill(); });
}

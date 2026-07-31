#include "ExternalActionRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

bool ExternalActionRunner::expandCommand(const QString &command,
                                         const QString &inputPath,
                                         QString *program,
                                         QStringList *arguments,
                                         QString *outputPath,
                                         QString *error)
{
    if (error)
        error->clear();
    QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty()) {
        // Every message this function and run() produce is shown to the user
        // as-is, inside "External action failed: %1". They say what happened
        // and what to do about it, in words that assume nothing about shells,
        // exit codes or processes.
        if (error) *error = tr("no command is set.");
        return false;
    }
    const QFileInfo input(inputPath);
    const QString suffix = input.suffix().isEmpty() ? QStringLiteral("png") : input.suffix();
    const QString prefix = input.absolutePath() + QLatin1Char('/')
                           + input.completeBaseName() + QStringLiteral("-action");
    QString out = prefix + QLatin1Char('.') + suffix;
    for (int copy = 2; QFileInfo::exists(out); ++copy)
        out = prefix + QLatin1Char('-') + QString::number(copy) + QLatin1Char('.') + suffix;
    QString exe = parts.takeFirst();
    const QString found = QFileInfo(exe).isAbsolute()
                              ? (QFileInfo(exe).isExecutable() ? exe : QString())
                              : QStandardPaths::findExecutable(exe);
    if (found.isEmpty()) {
        if (error)
            *error = tr("the program \"%1\" was not found. Check the spelling, "
                        "or install it first.").arg(exe);
        return false;
    }
    for (QString &arg : parts) {
        arg.replace(QLatin1String("$input"), inputPath);
        arg.replace(QLatin1String("$output"), out);
    }
    *program = found;
    *arguments = parts;
    *outputPath = out;
    return true;
}

void ExternalActionRunner::run(const QString &command, const QString &inputPath,
                               bool removeInput, Callback callback, int timeoutMs)
{
    if (m_running >= kMaxConcurrent) {
        if (removeInput)
            QFile::remove(inputPath);
        callback({}, tr("%1 of them are already running, so this one was skipped. "
                        "Wait for those to finish.").arg(kMaxConcurrent));
        return;
    }
    QString program, outputPath, error;
    QStringList arguments;
    if (!expandCommand(command, inputPath, &program, &arguments, &outputPath, &error)) {
        if (removeInput)
            QFile::remove(inputPath);
        callback({}, error);
        return;
    }
    auto *process = new QProcess(this);
    const auto completed = std::make_shared<bool>(false);
    // Share ONE callback between both handlers. Moving it into only the finished
    // lambda (and copying the moved-from function into errorOccurred) left an
    // empty std::function on the FailedToStart path - invoking it threw
    // std::bad_function_call and crashed the app on the exact "report failure"
    // path. The `completed` flag still guarantees it fires exactly once.
    const auto cb = std::make_shared<Callback>(std::move(callback));

    // The single exit door. Success, non-zero exit, a failed start and the
    // timeout all leave through it, so the counter drops exactly once and no
    // path can forget the scratch input or a half-written output.
    const auto finish = [this, process, inputPath, removeInput, outputPath, cb, completed]
                        (const QString &out, const QString &err) {
        if (*completed)
            return;
        *completed = true;
        --m_running;
        if (removeInput)
            QFile::remove(inputPath);
        // expandCommand picked a name that did not exist yet, so anything
        // sitting there after a failed run is this run's own leftover and
        // never a file the user already had.
        if (out.isEmpty() && QFileInfo::exists(outputPath))
            QFile::remove(outputPath);
        process->deleteLater();
        (*cb)(out, err);
    };

    process->setProgram(program);
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, process,
            [process, outputPath, finish](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            const QString diagnostic = QString::fromUtf8(process->readAll()).trimmed().left(1000);
            finish({}, diagnostic.isEmpty()
                           ? tr("it stopped with error code %1.").arg(code)
                           : diagnostic);
            return;
        }
        finish(QFileInfo::exists(outputPath) ? outputPath : QString(), {});
    });
    connect(process, &QProcess::errorOccurred, process,
            [process, finish](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        finish({}, tr("Unisic could not start it (%1).").arg(process->errorString()));
    });

    ++m_running;
    if (timeoutMs > 0) {
        // Parented to the process so it dies with it, and armed before start()
        // so a synchronous start failure simply finds the run already closed.
        auto *guard = new QTimer(process);
        guard->setSingleShot(true);
        connect(guard, &QTimer::timeout, process, [process, finish, timeoutMs] {
            if (process->state() != QProcess::NotRunning)
                process->kill();
            finish({}, tr("it did not respond for %1 s, so Unisic stopped it.")
                           .arg(timeoutMs / 1000));
        });
        guard->start(timeoutMs);
    }
    process->start();
}

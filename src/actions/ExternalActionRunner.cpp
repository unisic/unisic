#include "ExternalActionRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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
        if (error) *error = QStringLiteral("action command is empty");
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
        if (error) *error = QStringLiteral("program not found: %1").arg(exe);
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
                               bool removeInput, Callback callback)
{
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
    // empty std::function on the FailedToStart path — invoking it threw
    // std::bad_function_call and crashed the app on the exact "report failure"
    // path. The `completed` flag still guarantees it fires exactly once.
    const auto cb = std::make_shared<Callback>(std::move(callback));
    process->setProgram(program);
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, process,
            [process, inputPath, removeInput, outputPath, cb, completed](int code, QProcess::ExitStatus status) {
        if (*completed)
            return;
        *completed = true;
        const QString diagnostic = QString::fromUtf8(process->readAll()).trimmed().left(1000);
        if (removeInput)
            QFile::remove(inputPath);
        process->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            (*cb)({}, diagnostic.isEmpty()
                          ? QStringLiteral("action exited with code %1").arg(code)
                          : diagnostic);
            return;
        }
        (*cb)(QFileInfo::exists(outputPath) ? outputPath : QString(), {});
    });
    connect(process, &QProcess::errorOccurred, process,
            [process, inputPath, removeInput, cb, completed](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (*completed)
            return;
        *completed = true;
        if (removeInput)
            QFile::remove(inputPath);
        const QString error = process->errorString();
        process->deleteLater();
        (*cb)({}, error);
    });
    process->start();
}

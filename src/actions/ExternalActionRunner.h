#pragma once

#include <QObject>
#include <functional>

class ExternalActionRunner final : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &outputPath, const QString &error)>;
    explicit ExternalActionRunner(QObject *parent = nullptr) : QObject(parent) {}

    // Generous on purpose: the point of an after-capture action is real work
    // (oxipng -o 4 on a 4K frame is tens of seconds), so the ceiling only has
    // to catch a program that hangs, never one that is merely slow.
    static constexpr int kDefaultTimeoutMs = 120000;
    // What the settings row may set the ceiling to. Below the floor the guard
    // would fire on ordinary work; above the roof it stops being a guard, and
    // "no ceiling at all" is the bug this class exists to fix, so it is not on
    // offer. The self-tests bypass both by passing timeoutMs explicitly - they
    // need a hang caught in a second, not in a minute.
    static constexpr int kMinTimeoutSec = 10;
    static constexpr int kMaxTimeoutSec = 3600;
    // Backpressure at the only boundary that can pile up: hold the shutter and
    // every capture queues one more child process. Rejecting the overflow keeps
    // the count bounded and tells the user why, which an unbounded queue never
    // could once it started swallowing memory.
    static constexpr int kMaxConcurrent = 4;

    void run(const QString &command, const QString &inputPath, bool removeInput,
             Callback callback, int timeoutMs = kDefaultTimeoutMs);

    static bool expandCommand(const QString &command, const QString &inputPath,
                              QString *program, QStringList *arguments,
                              QString *outputPath, QString *error = nullptr);

private:
    // Touched only from the GUI thread (QProcess and QTimer both deliver their
    // signals there), so no synchronisation is needed or wanted.
    int m_running = 0;
};

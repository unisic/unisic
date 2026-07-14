#pragma once

#include <QObject>
#include <functional>

class ExternalActionRunner final : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &outputPath, const QString &error)>;
    explicit ExternalActionRunner(QObject *parent = nullptr) : QObject(parent) {}

    void run(const QString &command, const QString &inputPath, bool removeInput,
             Callback callback);
    static bool expandCommand(const QString &command, const QString &inputPath,
                              QString *program, QStringList *arguments,
                              QString *outputPath, QString *error = nullptr);
};

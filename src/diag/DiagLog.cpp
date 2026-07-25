#include "DiagLog.h"

#include "CrashHandler.h"
#include "DiagRedact.h"

#include <array>
#include <mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

#include <fcntl.h>
#include <unistd.h>

namespace {

// Sized to hold a whole cold start (hotkey bind status, portal probes, theme
// scan), a full capture-edit-upload cycle, an ffmpeg failure dump, or one F8
// smoke run with context to spare. At the 1000-char line cap the ring settles
// around 50-100 KB and is hard-bounded at 500 KB, which is cheap enough that
// there is deliberately NO on/off switch: a logger the user turned off is
// never on when it is finally needed.
constexpr int kRingLines = 500;
constexpr int kMaxLineChars = 1000;
// On overflow the CURRENT file is truncated and continues, so the crash tail
// always lands and the previous run's file is never collateral damage.
constexpr qint64 kMaxFileBytes = 2 * 1024 * 1024;

std::mutex g_mutex;
std::array<QByteArray, kRingLines> g_ring;
int g_head = 0;
int g_count = 0;
QByteArray g_lastLine;
int g_repeat = 0;

QtMessageHandler g_previousHandler = nullptr;
DiagLog::Role g_role = DiagLog::Role::App;
QString g_helperTag;
int g_fd = -1;
QString g_path;
qint64 g_written = 0;
bool g_installed = false;

const char *levelOf(QtMsgType t)
{
    switch (t) {
    case QtDebugMsg:    return "D";
    case QtInfoMsg:     return "I";
    case QtWarningMsg:  return "W";
    case QtCriticalMsg: return "C";
    case QtFatalMsg:    return "F";
    }
    return "?";
}

// Caller holds g_mutex.
void writeToFileLocked(const QByteArray &line)
{
    if (g_fd < 0)
        return;
    if (g_written + line.size() > kMaxFileBytes) {
        static const char note[] = "\n=== log size cap reached, truncating and continuing ===\n";
        ::ftruncate(g_fd, 0);
        ::lseek(g_fd, 0, SEEK_SET);
        g_written = 0;
        (void)!::write(g_fd, note, sizeof(note) - 1);
        g_written += qint64(sizeof(note) - 1);
    }
    const ssize_t w = ::write(g_fd, line.constData(), size_t(line.size()));
    if (w > 0)
        g_written += w;
}

// Caller holds g_mutex. `line` is already redacted and newline-free.
void storeLocked(const QByteArray &stamped)
{
    g_ring[g_head] = stamped;
    g_head = (g_head + 1) % kRingLines;
    if (g_count < kRingLines)
        ++g_count;
    writeToFileLocked(stamped + '\n');
}

void record(const char *level, const QString &tag, const QString &text)
{
    // One physical message can be many lines (ffmpeg dumps, D-Bus errors);
    // each becomes its own ring entry so the tail is never one giant blob.
    const QString redacted = DiagRedact::redact(text);
    const QString stampPrefix =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));

    std::lock_guard<std::mutex> lock(g_mutex);
    const QStringList parts = redacted.split(QLatin1Char('\n'));
    for (const QString &part : parts) {
        QString body = part;
        if (body.size() > kMaxLineChars)
            body = body.left(kMaxLineChars) + QStringLiteral(" ...[truncated]");
        QByteArray line = stampPrefix.toUtf8() + ' ' + level;
        if (!tag.isEmpty())
            line += " [" + tag.toUtf8() + ']';
        line += ' ' + body.toUtf8();

        // Consecutive duplicates collapse: a repeating portal timeout used to
        // be able to push an entire startup out of a 500-line ring by itself.
        QByteArray key = line.mid(13); // drop the timestamp
        if (key == g_lastLine) {
            ++g_repeat;
            continue;
        }
        if (g_repeat > 0) {
            storeLocked(stampPrefix.toUtf8() + " . (previous line repeated "
                        + QByteArray::number(g_repeat) + " more times)");
            g_repeat = 0;
        }
        g_lastLine = key;
        storeLocked(line);
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (g_role == DiagLog::Role::App) {
        record(levelOf(type), QString(), msg);
    } else {
        // A helper keeps no file: it tags the line and writes it to fd 2, which
        // the parent is already reading and folding into the one merged log.
        const QByteArray out = '[' + g_helperTag.toUtf8() + "] " + levelOf(type)
                               + ' ' + DiagRedact::redact(msg).toUtf8() + '\n';
        (void)!::write(2, out.constData(), size_t(out.size()));
    }
    if (g_previousHandler)
        g_previousHandler(type, ctx, msg);
}

QString logDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/logs");
}

QString crashFilePath()
{
    const QString suffix = g_role == DiagLog::Role::NotifHelper
                               ? QStringLiteral("-notif-helper")
                           : g_role == DiagLog::Role::RecordBorderHelper
                               ? QStringLiteral("-border-helper")
                               : QString();
    return logDir() + QStringLiteral("/last-crash") + suffix + QStringLiteral(".log");
}

DiagLog::PreviousRun g_previous;
bool g_previousComputed = false;

} // namespace

namespace DiagLog {

void install(int argc, char *argv[])
{
    if (g_installed)
        return;
    g_installed = true;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--notification-helper") == 0)
            g_role = Role::NotifHelper;
        else if (qstrcmp(argv[i], "--record-border-helper") == 0)
            g_role = Role::RecordBorderHelper;
    }
    if (g_role == Role::NotifHelper)
        g_helperTag = QStringLiteral("notif-helper");
    else if (g_role == Role::RecordBorderHelper)
        g_helperTag = QStringLiteral("border-helper");

    g_previousHandler = qInstallMessageHandler(messageHandler);
    CrashHandler::install();
}

Role role() { return g_role; }

QString openLogFile()
{
    if (g_role != Role::App || qgetenv("UNISIC_LOG") == "0")
        return QString();

    const QString dir = logDir();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner);

    const QString path = dir + QStringLiteral("/unisic.log");
    const QString prev = path + QStringLiteral(".1");
    // Rotation is startup-only and keeps exactly the two runs that matter for
    // a crash report: this one and the one before it. Two renames and an open,
    // no directory scan, on a path that must stay fast.
    QFile::remove(prev);
    QFile::rename(path, prev);

    const int fd = ::open(path.toLocal8Bit().constData(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return QString(); // stay in RAM; nothing else changes

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_fd = fd;
        g_path = path;
        g_written = 0;
        const QByteArray header =
            "=== unisic " + QCoreApplication::applicationVersion().toUtf8()
            + " app=" + QCoreApplication::applicationName().toUtf8()
            + " pid=" + QByteArray::number(QCoreApplication::applicationPid())
            + " started " + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
            + " ===\n";
        writeToFileLocked(header);
        // Everything qDebug'd between install() and here (QPA, QML engine,
        // early portal probes) was buffered in the ring; flush it in order.
        for (int i = 0; i < g_count; ++i) {
            const int idx = (g_head - g_count + i + kRingLines) % kRingLines;
            writeToFileLocked(g_ring[idx] + '\n');
        }
    }

    CrashHandler::setLogFd(fd);
    CrashHandler::setCrashFilePath(crashFilePath().toUtf8().constData());
    CrashHandler::setHeader(
        (QStringLiteral("unisic ") + QCoreApplication::applicationVersion()
         + QStringLiteral(" app=") + QCoreApplication::applicationName()
         + QStringLiteral(" pid=") + QString::number(QCoreApplication::applicationPid()))
            .toUtf8()
            .constData());
    return path;
}

void markCleanExit()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_fd < 0)
        return;
    const QByteArray marker = "=== clean exit "
                              + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
                              + " ===\n";
    writeToFileLocked(marker);
    ::fsync(g_fd);
}

QString logFilePath()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

QString logDirPath() { return logDir(); }

qint64 logFileSize()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_written;
}

int bufferedLineCount()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_count;
}

QString recentLines(int maxLines)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const int n = qMin(maxLines, g_count);
    QStringList out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const int idx = (g_head - n + i + kRingLines) % kRingLines;
        out << QString::fromUtf8(g_ring[idx]);
    }
    return out.join(QLatin1Char('\n'));
}

void appendRaw(const QString &tag, const QString &line)
{
    record("H", tag, line);
}

void appendChildOutput(const QString &tag, const QByteArray &bytes)
{
    const QString text = QString::fromUtf8(bytes);
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &l : lines)
        appendRaw(tag, l.trimmed());
}

const PreviousRun &previousRun()
{
    if (g_previousComputed)
        return g_previous;
    g_previousComputed = true;

    // A crash file is authoritative: the handler wrote it, so we know both the
    // signal and the frames. It is consumed here and left in place for the
    // user to attach; the latch keys on its content, not its existence.
    QFile crash(crashFilePath());
    if (crash.exists() && crash.open(QIODevice::ReadOnly | QIODevice::Text)) {
        g_previous.report = QString::fromUtf8(crash.readAll());
        crash.close();
        g_previous.outcome = PreviousRun::Crashed;
        g_previous.when = QFileInfo(crashFilePath()).lastModified();
        for (const QString &l : g_previous.report.split(QLatin1Char('\n'))) {
            if (l.startsWith(QLatin1String("signal: "))) {
                g_previous.signalName = l.mid(8).trimmed();
                break;
            }
        }
        return g_previous;
    }

    // No crash file: the previous log's last lines say whether it ended
    // cleanly. Absent marker with a present file means the process went away
    // without running aboutToQuit - a SIGKILL, an OOM kill or a power cut.
    QFile prev(logDir() + QStringLiteral("/unisic.log.1"));
    if (prev.exists() && prev.open(QIODevice::ReadOnly)) {
        const qint64 size = prev.size();
        prev.seek(qMax(qint64(0), size - 512));
        const QString tail = QString::fromUtf8(prev.readAll());
        prev.close();
        g_previous.outcome = tail.contains(QLatin1String("=== clean exit"))
                                 ? PreviousRun::Clean
                                 : PreviousRun::Killed;
        g_previous.when = QFileInfo(prev.fileName()).lastModified();
    }
    return g_previous;
}

QString previousRunKey()
{
    const PreviousRun &p = previousRun();
    if (p.outcome != PreviousRun::Crashed)
        return QString();
    // Content-keyed: the same crash never nags twice, a different one is shown.
    return QString::number(qHash(p.report), 16);
}

} // namespace DiagLog

#pragma once
#include <QDateTime>
#include <QString>

// The runtime log a user can actually paste into an issue.
//
// Qt logs to journald on this platform, not to stderr, so until now a crash or
// a misbehaving capture left the reporter with nothing to attach: the existing
// "Copy diagnostics" button reports versions, capabilities and build flags, but
// nothing about what the app was DOING. This keeps the last kRingLines
// messages in memory and, once the process owns the session, mirrors them to a
// file that survives the crash that ended the run.
//
// Two-phase install on purpose:
//   install()     first statement of main(), before QApplication and before the
//                 helper dispatch, so QPA and QML engine warnings are caught.
//                 Touches no files.
//   openLogFile() only after this process has WON the single-instance
//                 handshake. A forwarding `unisic --region`, or a
//                 `--export-settings` batch run, must never rotate the running
//                 instance's log out from under it.
//
// Everything is redacted through DiagRedact on write, so nothing unredacted is
// ever held in memory either. Nothing is ever uploaded; the user pastes it.
namespace DiagLog {

enum class Role { App, NotifHelper, RecordBorderHelper };

// PHASE 1. Installs the message handler (CHAINING to the previous one, so
// journald/stderr behaviour is unchanged) and arms CrashHandler.
void install(int argc, char *argv[]);
Role role();

// PHASE 2. mkpath, rotate (.log -> .log.1), open a fresh 0600 O_CLOEXEC file,
// write the header, flush everything buffered since install(), and hand the fd
// to the crash handler. No-op for helper roles and when UNISIC_LOG=0. Returns
// the path, or "" if the file could not be opened - in which case the ring
// still works and nothing else changes.
QString openLogFile();

// Which code path asked the app to quit, recorded in the clean-exit marker.
// Call it right before QCoreApplication::quit(). An EMPTY reason in a finished
// log means nothing inside the app asked: the process was ended from outside
// (session manager, systemd stopping the unit, a kill), which is the one thing
// a "clean exit" marker alone could never distinguish from a normal quit.
void setQuitReason(const QString &reason);

// Written from aboutToQuit. Its ABSENCE in the previous run's file is what
// lets the next start tell a crash (or an OOM kill, or a power cut) from a
// normal exit.
void markCleanExit();

QString logFilePath();
QString logDirPath();
qint64 logFileSize();
int bufferedLineCount();

// Newest last, already redacted.
QString recentLines(int maxLines = 500);

// Helper subprocess stderr, folded into this process's log so there is ONE
// ordered file. stdout is a protocol on both helpers, so this is stderr only.
void appendChildOutput(const QString &tag, const QByteArray &bytes);
// A line from the app itself that must not go through qDebug (used by the
// child forwarder and the dev harness, so neither re-enters the handler).
void appendRaw(const QString &tag, const QString &line);

// Helper roles deliberately keep NO file and no crash file of their own: they
// tag every line and write it to fd 2, and the parent folds that into the one
// merged, ordered log through appendChildOutput(). Two processes writing two
// logs would make the interleaving unrecoverable.

struct PreviousRun {
    enum Outcome { Unknown, Clean, Crashed, Killed };
    Outcome outcome = Unknown;
    QString signalName;   // empty unless outcome == Crashed
    QDateTime when;
    QString report;       // the whole crash block, already redacted
};
// Computed once, lazily, off the startup path: reads the tail of the previous
// log for the clean-exit marker and picks up a crash file if one is there.
const PreviousRun &previousRun();
// Latch: the crash notice is shown once per crash, keyed on the report itself
// so a second crash is reported again but the same one never nags.
QString previousRunKey();

} // namespace DiagLog

// AppContext's desktop-integration half on Windows (DesktopShell.cpp on
// Linux): login autostart through the per-user Run key, "show in folder"
// through Explorer, and ZIP export written by the linked libarchive, since
// Windows ships no zip.exe and System32's tar.exe crashes on non-ASCII names.
#include "AppContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrentRun>

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <memory>

void AppContext::showInFileManager(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        openDirectory(path.isEmpty() ? QString() : QFileInfo(path).absolutePath());
        return;
    }
    // explorer /select opens the folder with the file selected, the job
    // FileManager1.ShowItems does on Linux. Detached: the child hands the
    // request to the running shell and exits, so nothing waits on it.
    // Explorer parses its own command line, so the switch goes through as one
    // verbatim native argument rather than through Qt's quoting.
    QProcess explorer;
    explorer.setProgram(QStringLiteral("explorer.exe"));
    explorer.setNativeArguments(
        QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    if (!explorer.startDetached())
        openDirectory(QFileInfo(path).absolutePath());
}

QString AppContext::showInFolderCheck() const
{
    if (QStandardPaths::findExecutable(QStringLiteral("explorer")).isEmpty())
        return QStringLiteral("SKIP (no explorer.exe - falls back to opening the folder)");
    return QStringLiteral("PASS (explorer /select)");
}

// ------------------------------------------------------------------- ZIP

QString AppContext::zipUnavailableReason() const
{
    return QString(); // libarchive is linked in, nothing to install
}

namespace {

struct ZipResult {
    QString path;        // the file the failure is about, empty for libarchive's own
    QString error;       // empty on success
    bool shrank = false; // a member got shorter while it was being read
};

struct ArchiveFree {
    void operator()(struct archive *a) const { archive_write_free(a); }
};
struct EntryFree {
    void operator()(struct archive_entry *e) const { archive_entry_free(e); }
};

// libarchive's sink: each block it emits goes straight into the QSaveFile.
la_ssize_t zipSink(struct archive *a, void *client, const void *buf, size_t len)
{
    auto *out = static_cast<QSaveFile *>(client);
    const qint64 n = out->write(static_cast<const char *>(buf), qint64(len));
    if (n != qint64(len)) {
        archive_set_error(a, EIO, "%s", out->errorString().toLocal8Bit().constData());
        return -1;
    }
    return la_ssize_t(n);
}

// Compressed already: deflating them again costs CPU for next to no gain,
// and a long recording is most of an export's size.
bool storedAsIs(const QString &name)
{
    static const QStringList suffixes{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("avif"),
        QStringLiteral("mp4"), QStringLiteral("webm"), QStringLiteral("mkv"),
        QStringLiteral("zip")};
    return suffixes.contains(QFileInfo(name).suffix(), Qt::CaseInsensitive);
}

// Runs on a pool thread and owns everything it touches. Memory is bounded by
// the one read buffer, libarchive's 10 KiB output block and zlib's deflate
// state (about 256 KiB), whatever the member sizes. The archive appears at
// `dest` only on QSaveFile::commit(), so a failure leaves no partial file and
// an existing archive of that name intact.
ZipResult writeZipArchive(const QList<QPair<QString, QString>> &members, const QString &dest)
{
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly))
        return {dest, out.errorString()};
    // Declared after `out`, so it is freed first, while its sink still exists.
    std::unique_ptr<struct archive, ArchiveFree> a(archive_write_new());
    if (!a)
        return {QString(), QStringLiteral("libarchive: out of memory")};
    const auto fail = [&a](const QString &path) {
        const char *e = archive_error_string(a.get());
        ZipResult r{path, e ? QString::fromLocal8Bit(e)
                            : QStringLiteral("libarchive error %1").arg(archive_errno(a.get()))};
        archive_write_fail(a.get()); // free() then skips the trailer it would write
        return r;
    };

    if (archive_write_set_format_zip(a.get()) != ARCHIVE_OK
        // Names as UTF-8 with the zip "language encoding" flag set, so Explorer
        // and 7-Zip read a Polish or Cyrillic file name back as it was written.
        || archive_write_set_format_option(a.get(), "zip", "hdrcharset", "UTF-8") != ARCHIVE_OK
        // A zip is not a tape: no zero padding up to a whole 10 KiB block.
        || archive_write_set_bytes_in_last_block(a.get(), 1) != ARCHIVE_OK
        || archive_write_open2(a.get(), &out, nullptr, zipSink, nullptr, nullptr) != ARCHIVE_OK)
        return fail(QString());

    QByteArray buf(256 * 1024, Qt::Uninitialized);
    for (const auto &m : members) {
        QFile in(m.second);
        if (!in.open(QIODevice::ReadOnly)) {
            archive_write_fail(a.get());
            return {m.second, in.errorString()};
        }
        const int setCompression = storedAsIs(m.first)
                                       ? archive_write_zip_set_compression_store(a.get())
                                       : archive_write_zip_set_compression_deflate(a.get());
        std::unique_ptr<struct archive_entry, EntryFree> entry(archive_entry_new());
        if (setCompression != ARCHIVE_OK || !entry)
            return fail(QString());
        archive_entry_set_pathname_utf8(entry.get(), m.first.toUtf8().constData());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), in.size());
        archive_entry_set_mtime(entry.get(),
                                in.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch(), 0);
        if (archive_write_header(a.get(), entry.get()) != ARCHIVE_OK)
            return fail(m.second);
        for (qint64 left = in.size(); left > 0;) {
            const qint64 n = in.read(buf.data(), qMin<qint64>(buf.size(), left));
            if (n <= 0) {
                archive_write_fail(a.get());
                ZipResult r{m.second, in.errorString()};
                r.shrank = n == 0;
                return r;
            }
            if (archive_write_data(a.get(), buf.constData(), size_t(n)) != la_ssize_t(n))
                return fail(m.second);
            left -= n;
        }
        if (archive_write_finish_entry(a.get()) != ARCHIVE_OK)
            return fail(m.second);
    }
    if (archive_write_close(a.get()) != ARCHIVE_OK)
        return fail(QString());
    if (!out.commit())
        return {dest, out.errorString()};
    return {};
}

} // namespace

void AppContext::writeZip(const QList<QPair<QString, QString>> &members, const QString &dest,
                          std::function<void(bool, const QString &)> done)
{
    auto *watcher = new QFutureWatcher<ZipResult>(qApp);
    QPointer<AppContext> self(this); // created, read and destroyed on the GUI thread
    connect(watcher, &QFutureWatcher<ZipResult>::finished, qApp, [watcher, self, done] {
        const ZipResult r = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        if (r.error.isEmpty() && !r.shrank) {
            done(true, QString());
            return;
        }
        const QString file = QDir::toNativeSeparators(r.path);
        QString why = r.error;
        if (r.shrank)
            why = tr("%1 changed while it was being archived").arg(file);
        else if (!file.isEmpty())
            why = QStringLiteral("%1: %2").arg(file, r.error);
        done(false, tr("Export failed: %1").arg(why));
    });
    watcher->setFuture(QtConcurrent::run(writeZipArchive, members, dest));
}

// -------------------------------------------------------------- autostart

// Per-user autostart is a value under HKCU's Run key, named after the desktop
// id so the dev build (app.unisic.UnisicDev) keeps its own entry and never
// overwrites the stable one, as the XDG file is on Linux.
static const QString &runKey()
{
    static const QString key =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    return key;
}

// The Startup switches in Task Manager and Settings leave Run alone and file
// their "off" under StartupApproved\Run instead, where Windows reads it before
// starting a Run value.
static const QString &startupApprovedKey()
{
    static const QString key = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run");
    return key;
}

static QString autostartCommand()
{
    // The command must name a STABLE exe. A staged update runs from the
    // updates folder, so the launcher that started it (UNISIC_BOOTSTRAP_EXE,
    // set by StagedUpdate_win.cpp) is the one to start at login, as $APPIMAGE
    // is on Linux. --tray-only makes the login launch start hidden in the tray.
    QString exe = qEnvironmentVariable("UNISIC_BOOTSTRAP_EXE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
    return QLatin1Char('"') + QDir::toNativeSeparators(exe) + QStringLiteral("\" --tray-only");
}

static bool writeRunValue()
{
    QSettings run(runKey(), QSettings::NativeFormat);
    run.setValue(QGuiApplication::desktopFileName(), autostartCommand());
    run.sync();
    return run.status() == QSettings::NoError;
}

bool AppContext::autostartEnabled() const
{
    return QSettings(runKey(), QSettings::NativeFormat).contains(QGuiApplication::desktopFileName());
}

void AppContext::refreshAutostartIfStale()
{
    // Self-heal a stale command (the portable folder moved, a reinstall to
    // another path) - otherwise the toggle reads "on" while login autostart
    // silently launches nothing.
    QSettings run(runKey(), QSettings::NativeFormat);
    const QString name = QGuiApplication::desktopFileName();
    if (run.contains(name) && run.value(name).toString() != autostartCommand())
        writeRunValue();
}

void AppContext::setAutostartEnabled(bool on)
{
    if (on == autostartEnabled())
        return;
    const QString name = QGuiApplication::desktopFileName();
    // Unisic's own switch is the one that counts: a Task Manager "off" left
    // over from earlier would keep a fresh Run value from ever starting.
    QSettings(startupApprovedKey(), QSettings::NativeFormat).remove(name);
    const QString where = runKey() + QLatin1Char('\\') + name;
    if (!on) {
        QSettings run(runKey(), QSettings::NativeFormat);
        run.remove(name);
        run.sync();
        if (run.status() != QSettings::NoError)
            showToast(tr("Could not disable autostart: cannot remove %1").arg(where), true);
        emit autostartEnabledChanged(); // reflect the real (post-remove) state
        return;
    }
    if (!writeRunValue()) {
        showToast(tr("Could not enable autostart: cannot write %1").arg(where), true);
        return; // state unchanged; the switch snaps back on the next read
    }
    emit autostartEnabledChanged();
}

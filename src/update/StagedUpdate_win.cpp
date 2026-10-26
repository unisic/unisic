// Windows half of the staged update (the Linux one is StagedUpdate.cpp).
//
// A portable install is a folder the user unpacked anywhere, and Windows will
// not let a running exe or a DLL it loaded be overwritten. So an update never
// touches that folder: the release zip is unpacked into
// <AppData>/updates/<version>/ and a "current" pointer names its unisic.exe.
// The installed exe stays a launcher - execStagedUpdate() below starts the
// newest staged copy and waits for it - until the user unpacks a newer zip
// over it by hand, at which point cleanStaleStage() drops the staging area.
#include "StagedUpdate.h"
#include "UpdateChecker.h"
#include "VersionCompare.h"
#include "diag/DiagLog.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cstdlib>
#include <string>
#include <windows.h>

namespace {

// How long a relaunch waits for the instance it replaces. That instance owns
// the single-instance pipe until it is gone, and a fresh process that still
// finds the pipe hands "show" to the exiting old version and quits itself.
// A blocked handle wait (no polling), bounded so a wedged old instance cannot
// keep the new one from ever starting.
constexpr DWORD kRestartWaitMs = 30000;

void waitForReplacedInstance()
{
    const QByteArray pid = qgetenv("UNISIC_RESTART_WAIT_PID");
    if (pid.isEmpty())
        return;
    // Consumed here, so a stage started next does not wait a second time.
    SetEnvironmentVariableW(L"UNISIC_RESTART_WAIT_PID", nullptr);
    bool ok = false;
    const DWORD id = pid.toULong(&ok);
    if (!ok || id == 0)
        return;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, id);
    if (!h)
        return; // already gone
    WaitForSingleObject(h, kRestartWaitMs);
    CloseHandle(h);
}

// Everything after argv[0] of this process's own command line, verbatim, so
// the arguments reach the staged copy with their quoting intact. argv[0] ends
// at its closing quote when it starts with one, else at the first blank: the
// rule CreateProcess itself parses the program name by.
const wchar_t *argumentTail(const wchar_t *cmd)
{
    if (*cmd == L'"') {
        ++cmd;
        while (*cmd && *cmd != L'"')
            ++cmd;
        if (*cmd == L'"')
            ++cmd;
    } else {
        while (*cmd && *cmd != L' ' && *cmd != L'\t')
            ++cmd;
    }
    return cmd;
}

std::wstring moduleFileName()
{
    // MAX_PATH is not a limit on a long-path system: grow until it fits, up
    // to the 32767-character ceiling of a Windows path.
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        if (buf.size() >= 32768)
            return {};
        buf.resize(buf.size() * 2);
    }
}

// The launcher only mirrors the staged copy, which handles Ctrl+C and console
// close itself: the default handler would end the launcher at once and hand
// the console back to the shell while the app is still shutting down.
BOOL WINAPI followChild(DWORD)
{
    return TRUE;
}

// System32\tar.exe (in Windows since 10 1803) is bsdtar: it reads zip as well
// as tar, refuses entries that climb out of the target with "..", and needs
// nothing from the bundle it is replacing. Resolved from the system directory,
// never from PATH, so a tar.exe from MSYS2 or Git cannot stand in for it.
QString systemTar()
{
    wchar_t dir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    return QString::fromWCharArray(dir, int(n)) + QStringLiteral("\\tar.exe");
}

} // namespace

// Runs before QApplication. On Linux the staged copy replaces this process
// (execv); Windows has no exec, so this one starts it, waits for it and exits
// with its exit code. That keeps what exec gave for free: a script or shell
// waiting on unisic.exe waits for the real run and sees its exit code, and
// --output style CLI runs keep their stdout/stderr. The waiting launcher costs
// one blocked thread and, once it trims its working set, well under a megabyte
// resident; nothing per frame.
void execStagedUpdate(int, char *[])
{
    // First, before any early return: a restart must outlive its predecessor
    // even when there is nothing staged to redirect to.
    waitForReplacedInstance();
    if (qEnvironmentVariableIsSet("UNISIC_STAGED"))
        return;
#ifdef UNISIC_DEV_BUILD
    const QString appName = QStringLiteral("unisic-dev");
#else
    const QString appName = QStringLiteral("unisic");
#endif
    // QStandardPaths composes <AppData>/<org>/<app> from these two names,
    // which main() only sets on the QApplication later. Set them for this one
    // lookup and put back what was there, so this is the directory
    // UpdateChecker::stagingDir() resolves to and nothing else changes.
    const QString oldOrg = QCoreApplication::organizationName();
    const QString oldApp = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("Unisic"));
    QCoreApplication::setApplicationName(appName);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/updates");
    QCoreApplication::setOrganizationName(oldOrg);
    QCoreApplication::setApplicationName(oldApp);

    QFile f(base + QStringLiteral("/current"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    f.close();
    const QString ver = lines.value(0).trimmed();
    const QString exe = lines.value(1).trimmed();
    // Only a strictly NEWER stage wins; a stale pointer (the user unpacked a
    // newer zip by hand) is ignored here and cleaned up by UpdateChecker.
    if (exe.isEmpty() || !UpdateVersion::isNewer(ver, QStringLiteral(UNISIC_VERSION))
        || !QFileInfo(exe).isExecutable())
        return;
    const std::wstring self = moduleFileName();
    if (self.empty())
        return;

    // Set in the process environment block, which is what CreateProcess hands
    // the child; the copy Qt reads in this process stays as it was, so a
    // failed start below leaves this run exactly as if nothing was staged.
    SetEnvironmentVariableW(L"UNISIC_STAGED", L"1");
    // Restarts go through this exe again so they land on the newest stage.
    SetEnvironmentVariableW(L"UNISIC_BOOTSTRAP_EXE", self.c_str());
    const std::wstring app = QDir::toNativeSeparators(exe).toStdWString();
    std::wstring cmd = L"\"" + app + L"\"" + argumentTail(GetCommandLineW());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                        &si, &pi)) {
        // Deleted stage, blocked by policy...: fall through to the installed
        // version rather than dying.
        SetEnvironmentVariableW(L"UNISIC_STAGED", nullptr);
        SetEnvironmentVariableW(L"UNISIC_BOOTSTRAP_EXE", nullptr);
        return;
    }
    CloseHandle(pi.hThread);
    SetConsoleCtrlHandler(followChild, TRUE);
    // Loading unisic.exe mapped every Qt DLL it imports; this process will
    // not touch those pages again, so hand them back for the whole wait.
    SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    std::exit(int(code));
}

bool UpdateChecker::isInstallAsset(const QString &name)
{
    // The portable zip windows.yml builds, never the AppImage: that is a
    // Linux ELF file with nothing in it Windows can run. The ".sha256" file
    // beside the zip must not match. The "-experimental" suffix the preview
    // port ships under is optional, so the release that drops it is still
    // found by every copy installed before then.
    static const QRegularExpression assetRe(
        QStringLiteral("^Unisic-.*-windows-x86_64(-experimental)?\\.zip$"));
    return assetRe.match(name).hasMatch();
}

QString UpdateChecker::noInstallAssetStatus()
{
    return tr("This release has no Windows build - it can't be installed in place");
}

void UpdateChecker::setUnpackCommand(QProcess *proc, const QString &pkg)
{
    // An empty program (no system directory) fails to start, which the
    // caller reports as a failed unpack.
    proc->setProgram(systemTar());
    proc->setArguments({QStringLiteral("-xf"), QDir::toNativeSeparators(pkg)});
}

QString UpdateChecker::stagedExe(const QString &verDir)
{
    // The zip carries the bundle deploy.sh stages: bin/ and share/ at its root.
    return verDir + QStringLiteral("/bin/unisic.exe");
}

bool UpdateChecker::replaceFile(const QString &from, const QString &to)
{
    // std::rename refuses an existing target on Windows, so the pointer could
    // never be flipped a second time. MoveFileEx with REPLACE_EXISTING is one
    // rename over the target on the same volume; WRITE_THROUGH returns only
    // once that is on disk.
    return MoveFileExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(from).utf16()),
                       reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(to).utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
           != 0;
}

QString UpdateChecker::writeTestPackage(const QString &dir)
{
    // Stands in for the release zip: one empty stored entry at the path the
    // staged exe lives at. The three zip records are written by hand rather
    // than with tar -c, so the dry run tests the unpack, not tar against
    // itself.
    const QByteArray name = "bin/unisic.exe";
    QByteArray zip;
    const auto u16 = [&zip](quint16 v) {
        zip.append(char(v & 0xff));
        zip.append(char(v >> 8));
    };
    const auto u32 = [&u16](quint32 v) {
        u16(quint16(v & 0xffff));
        u16(quint16(v >> 16));
    };
    // Local file header: version 1.0, stored, dated 1980-01-01, size and crc 0.
    u32(0x04034b50);
    u16(10); u16(0); u16(0); u16(0); u16(0x21);
    u32(0); u32(0); u32(0);
    u16(quint16(name.size())); u16(0);
    zip.append(name);
    const quint32 cdOffset = quint32(zip.size());
    // Central directory entry for it, pointing back at offset 0.
    u32(0x02014b50);
    u16(20); u16(10); u16(0); u16(0); u16(0); u16(0x21);
    u32(0); u32(0); u32(0);
    u16(quint16(name.size())); u16(0); u16(0); u16(0); u16(0);
    u32(0); u32(0);
    zip.append(name);
    const quint32 cdSize = quint32(zip.size()) - cdOffset;
    // End of central directory: one entry on one disk.
    u32(0x06054b50);
    u16(0); u16(0); u16(1); u16(1);
    u32(cdSize); u32(cdOffset);
    u16(0);

    const QString pkg = dir + QStringLiteral("/test.zip");
    QFile f(pkg);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(zip) != zip.size()
        || !f.flush())
        return {};
    f.close();
    return pkg;
}

void UpdateChecker::restartNow(bool trayOnly)
{
    // Always relaunch through the BOOTSTRAP exe so its execStagedUpdate() lands
    // on the newest staged version (a staged copy relaunching itself would pin
    // its own, now-old, version).
    QString exe = qEnvironmentVariable("UNISIC_BOOTSTRAP_EXE");
    if (exe.isEmpty())
        exe = QCoreApplication::applicationFilePath();
    if (exe.isEmpty())
        return;
    // The child must NOT inherit the staged-copy markers, or the fresh
    // bootstrap would skip its redirect and run the old installed version.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("UNISIC_STAGED"));
    env.remove(QStringLiteral("UNISIC_BOOTSTRAP_EXE"));
    // In place of Linux's 1 s sleep: the new process waits on this one's
    // handle, which is signalled the moment this process has exited and its
    // single-instance pipe is gone - no guessed delay either way.
    env.insert(QStringLiteral("UNISIC_RESTART_WAIT_PID"),
               QString::number(QCoreApplication::applicationPid()));
    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments(trayOnly ? QStringList{QStringLiteral("--tray-only")} : QStringList{});
    proc.setProcessEnvironment(env);
    if (!proc.startDetached()) {
        // Staying up beats vanishing: the tray still offers "Restart to update".
        qWarning() << "Update restart aborted - could not spawn the replacement process";
        return;
    }
    DiagLog::setQuitReason(QStringLiteral("update restart"));
    QCoreApplication::quit();
}

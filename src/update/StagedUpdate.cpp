// Linux half of the staged update: the bootstrap main() calls before
// QApplication, plus UpdateChecker's platform members. StagedUpdate_win.cpp is
// the Windows sibling (cmake/UnisicWindows.cmake swaps it in).
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
#include <QVarLengthArray>

#include <cstdio>
#include <unistd.h>

// Package installs (deb/rpm/arch) can't replace /usr/bin/unisic without root,
// so UpdateChecker stages a newer version under the user's data dir and this
// bootstrap execs it - the packaged binary keeps working as a launcher until
// the distro package catches up. The staged copy runs with UNISIC_STAGED=1 so
// it never re-redirects; AppImage never stages. Path composition
// must stay in sync with UpdateChecker::stagingDir() (org "Unisic" + app name
// under XDG data), computed by hand because this runs before QApplication.
void execStagedUpdate(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet("UNISIC_STAGED")
        || !qEnvironmentVariable("APPIMAGE").isEmpty()
        // A flatpak never stages: the bundle is read-only, it is updated by
        // the flatpak client, and exec'ing a downloaded binary inside the
        // sandbox is exactly what the store forbids.
        || qEnvironmentVariableIsSet("FLATPAK_ID"))
        return;
#ifdef UNISIC_DEV_BUILD
    const QString appName = QStringLiteral("unisic-dev");
#else
    const QString appName = QStringLiteral("unisic");
#endif
    QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (dataHome.isEmpty())
        dataHome = QDir::homePath() + QStringLiteral("/.local/share");
    QFile f(dataHome + QStringLiteral("/Unisic/") + appName + QStringLiteral("/updates/current"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    f.close();
    const QString ver = lines.value(0).trimmed();
    const QString appRun = lines.value(1).trimmed();
    // Only a strictly NEWER stage wins; a stale pointer (package caught up)
    // is ignored here and garbage-collected by UpdateChecker at startup.
    if (appRun.isEmpty() || !UpdateVersion::isNewer(ver, QStringLiteral(UNISIC_VERSION))
        || !QFileInfo(appRun).isExecutable())
        return;
    qputenv("UNISIC_STAGED", "1");
    // Restarts go through this binary again so they land on the newest stage.
    qputenv("UNISIC_BOOTSTRAP_EXE",
            QFile::encodeName(QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget()));
    QList<QByteArray> args;
    args.append(QFile::encodeName(appRun));
    for (int i = 1; i < argc; ++i)
        args.append(QByteArray(argv[i]));
    QVarLengthArray<char *, 16> execArgv;
    for (QByteArray &a : args)
        execArgv.append(a.data());
    execArgv.append(nullptr);
    ::execv(execArgv[0], execArgv.data());
    // exec failed (deleted stage, wrong arch...) - fall through to the
    // packaged version rather than dying.
}

bool UpdateChecker::isInstallAsset(const QString &name)
{
    // The release's AppImage. ".zsync" must not match - it satisfies the same
    // prefix.
    static const QRegularExpression assetRe(QStringLiteral("^Unisic-.*x86_64\\.AppImage$"));
    return assetRe.match(name).hasMatch();
}

QString UpdateChecker::noInstallAssetStatus()
{
    return tr("This release has no AppImage - it can't be installed in place");
}

void UpdateChecker::setUnpackCommand(QProcess *proc, const QString &pkg)
{
    // --appimage-extract works without FUSE (a package install can't assume
    // libfuse2) and costs the extraction once - every later start execs the
    // unpacked AppRun directly.
    proc->setProgram(pkg);
    proc->setArguments({QStringLiteral("--appimage-extract")});
}

QString UpdateChecker::stagedExe(const QString &verDir)
{
    return verDir + QStringLiteral("/squashfs-root/AppRun");
}

bool UpdateChecker::replaceFile(const QString &from, const QString &to)
{
    // rename(2) replaces an existing target atomically: at no instant is
    // there no file at `to`.
    return std::rename(QFile::encodeName(from).constData(),
                       QFile::encodeName(to).constData()) == 0;
}

QString UpdateChecker::writeTestPackage(const QString &dir)
{
    // Stands in for an AppImage: answers --appimage-extract the way the real
    // runtime does, with an executable squashfs-root/AppRun in the working
    // directory.
    const QString pkg = dir + QStringLiteral("/test.AppImage");
    const QByteArray body = "#!/bin/sh\n"
                            "[ \"$1\" = --appimage-extract ] || exit 2\n"
                            "mkdir -p squashfs-root && : > squashfs-root/AppRun"
                            " && chmod 755 squashfs-root/AppRun\n";
    QFile f(pkg);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(body) != body.size()
        || !f.flush())
        return {};
    f.close();
    QFile::setPermissions(pkg, QFile::permissions(pkg) | QFileDevice::ExeOwner);
    return pkg;
}

void UpdateChecker::restartNow(bool trayOnly)
{
    QString exe = appImageTarget();
    if (exe.isEmpty()) {
        // Package install: always relaunch through the BOOTSTRAP binary so the
        // exec in main() lands on the newest staged version (a staged copy
        // relaunching itself would pin its own, now-old, version).
        exe = qEnvironmentVariable("UNISIC_BOOTSTRAP_EXE");
        if (exe.isEmpty())
            exe = QCoreApplication::applicationFilePath();
    }
    if (exe.isEmpty())
        return;
    // The 1 s shim lets this instance's single-instance socket disappear
    // first - a fresh process that still finds it would hand off "show" to
    // the exiting old version and quit itself.
    const QString shim = trayOnly ? QStringLiteral("sleep 1; exec \"$0\" --tray-only")
                                  : QStringLiteral("sleep 1; exec \"$0\"");
    // The child must NOT inherit the staged-copy markers, or the fresh
    // bootstrap would skip its redirect and run the old packaged version.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("UNISIC_STAGED"));
    env.remove(QStringLiteral("UNISIC_BOOTSTRAP_EXE"));
    const auto spawn = [&env](const QString &program, const QStringList &arguments) {
        QProcess proc;
        proc.setProgram(program);
        proc.setArguments(arguments);
        proc.setProcessEnvironment(env);
        return proc.startDetached();
    };

    // startDetached does NOT escape the cgroup, and every desktop launch on a
    // systemd session runs the app inside a unit (app-app.unisic.Unisic@*.service
    // for the autostart entry and for a menu click alike). Units default to
    // KillMode=control-group, so the moment this process quits systemd SIGTERMs
    // everything left in the cgroup - including the sleeping shim, which is
    // killed a second before it can exec. Measured: the replacement never
    // started, and the app simply disappeared from the tray until launched by
    // hand. systemd-run puts the relaunch in a unit of its own, outside the
    // dying one. Fall back to the bare shim where there is no systemd-run
    // (non-systemd session): there the old behaviour was already correct.
    bool started = false;
    const QString systemdRun = QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
    if (!systemdRun.isEmpty())
        started = spawn(systemdRun, {QStringLiteral("--user"), QStringLiteral("--collect"),
                                     QStringLiteral("--quiet"), QStringLiteral("--"),
                                     QStringLiteral("/bin/sh"), QStringLiteral("-c"), shim, exe});
    if (!started)
        started = spawn(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), shim, exe});
    if (!started) {
        // Staying up beats vanishing: the tray still offers "Restart to update".
        qWarning() << "Update restart aborted - could not spawn the replacement process";
        return;
    }
    DiagLog::setQuitReason(QStringLiteral("update restart"));
    QCoreApplication::quit();
}

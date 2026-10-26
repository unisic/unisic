// AppContext's desktop-integration half on Linux: login autostart, "show in
// folder" and the ZIP writer behind exportFilesToZip. Same class, separate
// translation unit, so DesktopShell_win.cpp can replace exactly these members
// on Windows without an #ifdef in AppContext.cpp.
#include "AppContext.h"
#include "Settings.h"
#include "capture/PortalRequest.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

void AppContext::showInFileManager(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        openDirectory(path.isEmpty() ? QString() : QFileInfo(path).absolutePath());
        return;
    }
    // ShowItems opens the folder with the file selected (Dolphin, Nautilus,
    // Nemo, Thunar all serve it). Async: the service is D-Bus-activatable, so
    // a cold start could stall a blocking call on the GUI thread.
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("ShowItems"));
    msg << QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}
        << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    const QString dir = QFileInfo(path).absolutePath();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, dir](QDBusPendingCallWatcher *w) {
        if (w->isError())
            openDirectory(dir);
        w->deleteLater();
    });
}

// showInFileManager's select-the-file path needs a FileManager1 host on the
// bus; without one the action still works via the plain open-folder fallback.
QString AppContext::showInFolderCheck() const
{
    auto *bus = QDBusConnection::sessionBus().interface();
    const QString svc = QStringLiteral("org.freedesktop.FileManager1");
    if (bus->isServiceRegistered(svc))
        return QStringLiteral("PASS (FileManager1 service live)");
    const QDBusReply<QStringList> act = bus->activatableServiceNames();
    if (act.isValid() && act.value().contains(svc))
        return QStringLiteral("PASS (FileManager1 activatable)");
    return QStringLiteral("SKIP (no FileManager1 host - falls back to opening the folder)");
}

// ------------------------------------------------------------------- ZIP

QString AppContext::zipUnavailableReason() const
{
    // No zip library is linked on Linux (see the header note): Info-ZIP it is.
    if (QStandardPaths::findExecutable(QStringLiteral("zip")).isEmpty())
        return tr("The “zip” program is not installed - install it and try again.");
    return QString();
}

void AppContext::writeZip(const QList<QPair<QString, QString>> &members, const QString &dest,
                          std::function<void(bool, const QString &)> done)
{
    const QString zipBin = QStandardPaths::findExecutable(QStringLiteral("zip"));
    QFile::remove(dest); // zip *appends* to an existing archive; we want a fresh one

    // Stage the members as symlinks under their archive names in a temp dir;
    // default zip (no -y) follows each link and stores the real content under
    // the staged name.
    auto *staging = new QTemporaryDir;
    if (!staging->isValid()) {
        const QString e = staging->errorString();
        delete staging;
        done(false, tr("Could not create a temporary folder: %1").arg(e));
        return;
    }
    // "./" prefix so a staged name starting with '-' is a path, not a zip flag
    // (Info-ZIP parses leading-dash argv anywhere as options); -j still stores
    // just the basename, so the archive entries are unchanged.
    QStringList zipArgs{QStringLiteral("-j"), QStringLiteral("-q"), dest};
    for (const auto &m : members) {
        QFile::link(m.second, staging->filePath(m.first));
        zipArgs << (QStringLiteral("./") + m.first);
    }

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(staging->path());
    connect(proc, &QProcess::finished, this,
            [this, proc, staging, dest, done](int code, QProcess::ExitStatus st) {
                const QString errOut = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                delete staging; // removes the staged symlinks
                if (st == QProcess::NormalExit && code == 0 && QFileInfo::exists(dest))
                    done(true, QString());
                else
                    done(false, tr("Export failed: %1")
                                    .arg(errOut.isEmpty() ? tr("zip exited with code %1").arg(code)
                                                          : errOut));
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, staging, done](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart)
                    return; // a crash after start still emits finished(), which cleans up
                proc->deleteLater();
                delete staging;
                done(false, tr("Could not run the “zip” program."));
            });
    proc->start(zipBin, zipArgs);
}

// -------------------------------------------------------------- autostart

static QString autostartFilePath()
{
    // XDG autostart: $XDG_CONFIG_HOME/autostart (ConfigLocation == ~/.config).
    // Keyed on the desktop id, so the dev build (app.unisic.UnisicDev) keeps
    // its own autostart entry and never overwrites the stable one.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/autostart/") + QGuiApplication::desktopFileName()
           + QStringLiteral(".desktop");
}

// Sandboxed autostart goes through the Background portal, not through a file:
// $XDG_CONFIG_HOME inside the sandbox is ~/.var/app/<id>/config, which the
// host session never reads, so a .desktop written there would leave the switch
// saying "on" while nothing starts at login. The portal writes the host-side
// entry itself (and, on some backends, asks the user first).
static bool sandboxed()
{
    return qEnvironmentVariableIsSet("FLATPAK_ID");
}

// The "Exec=..." line for the .desktop.
static QByteArray autostartExecLine()
{
    // Exec must point at a STABLE path. For an AppImage that is $APPIMAGE (the
    // outer file), not applicationFilePath() (the transient FUSE mount, gone on
    // the next run). --tray-only makes the login launch start hidden in the tray.
    QString execPath = qEnvironmentVariable("APPIMAGE");
    if (execPath.isEmpty())
        execPath = QCoreApplication::applicationFilePath();
    // Desktop Entry spec: backslash and quote need escaping inside a quoted arg;
    // '%' is a field-code introducer and must be doubled.
    execPath.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
            .replace(QLatin1Char('"'), QLatin1String("\\\""))
            .replace(QLatin1Char('%'), QLatin1String("%%"));
    return "Exec=\"" + execPath.toUtf8() + "\" --tray-only\n";
}

// (Re)write the autostart .desktop, false on I/O error.
static bool writeAutostartFile()
{
    const QString path = autostartFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write("[Desktop Entry]\n"
            "Type=Application\n"
            "Name=" + QGuiApplication::applicationDisplayName().toUtf8() + "\n"
            "Comment=Screenshots, annotations, uploads and GIF recording\n"
            + autostartExecLine() +
            "Icon=" + QGuiApplication::desktopFileName().toUtf8() + "\n"
            "Terminal=false\n"
            "Categories=Utility;Graphics;\n"
            "X-GNOME-Autostart-enabled=true\n");
    f.close();
    return true;
}

bool AppContext::autostartEnabled() const
{
    // The portal exposes no getter, so the last answer it gave is the state.
    // It is the ANSWER that is stored, never the request: a backend that
    // refuses (or a user who says no) leaves this false and the switch snaps
    // back, which is the honest reading.
    if (sandboxed())
        return m_settings && m_settings->portalAutostartGranted();
    return QFile::exists(autostartFilePath());
}

void AppContext::refreshAutostartIfStale()
{
    // Nothing to keep fresh in a sandbox: the entry lives host-side and is the
    // portal's to write, and the exec path inside /app never moves.
    if (sandboxed())
        return;
    // Pre-rename installs wrote org.unisic.Unisic.desktop; migrate it or the
    // old entry keeps autostarting alongside (and ignores the toggle).
    const QString legacy = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                           + QStringLiteral("/autostart/org.unisic.Unisic.desktop");
    if (QFile::remove(legacy) && !QFile::exists(autostartFilePath()))
        writeAutostartFile();

    // Self-heal a stale Exec (binary rebuilt to a new path / AppImage moved),
    // mirroring ensureDesktopFile() - otherwise the toggle reads "on" while
    // login autostart silently launches nothing.
    const QString path = autostartFilePath();
    if (!QFile::exists(path))
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray data = f.readAll();
    f.close();
    if (!data.contains(autostartExecLine()))
        writeAutostartFile();
}

void AppContext::requestPortalAutostart(bool on)
{
    const QString token = PortalRequest::nextToken();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Background"),
        QStringLiteral("RequestBackground"));
    QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("autostart"), on},
        // Without this the portal grants "may run in the background" but writes
        // no autostart entry; with it the entry runs the same tray-only launch
        // the native autostart file does. `unisic` resolves inside the sandbox.
        {QStringLiteral("commandline"), QStringList{QStringLiteral("unisic"),
                                                    QStringLiteral("--tray-only")}},
        {QStringLiteral("reason"), tr("Unisic starts hidden in the tray so its capture "
                                      "shortcuts work right after you log in.")},
    };
    msg << QString() << options; // parent_window: empty (no exported handle on Wayland)

    PortalRequest::send(msg, token, [this, on](uint code, const QVariantMap &results) {
        if (code != 0) {
            if (code != 1) // 1 = the user cancelled; not worth a toast
                showToast(tr("The desktop refused the autostart request"), true);
            emit autostartEnabledChanged(); // snap the switch back to reality
            return;
        }
        const bool granted = results.value(QStringLiteral("autostart"), false).toBool();
        if (m_settings)
            m_settings->setPortalAutostartGranted(granted);
        if (on && !granted)
            showToast(tr("Autostart was not granted"), true);
        emit autostartEnabledChanged();
    }, this, 0); // a portal that asks the user must be allowed to wait for them
}

void AppContext::setAutostartEnabled(bool on)
{
    if (on == autostartEnabled())
        return;
    if (sandboxed()) {
        requestPortalAutostart(on);
        return;
    }
    const QString path = autostartFilePath();
    if (!on) {
        if (!QFile::remove(path) && QFile::exists(path))
            showToast(tr("Could not disable autostart: cannot remove %1").arg(path), true);
        emit autostartEnabledChanged(); // reflect the real (post-remove) state
        return;
    }
    if (!writeAutostartFile()) {
        showToast(tr("Could not enable autostart: cannot write %1").arg(path), true);
        return; // state unchanged; the switch snaps back on the next read
    }
    emit autostartEnabledChanged();
}

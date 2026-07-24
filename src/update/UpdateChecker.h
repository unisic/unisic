#pragma once
#include <QObject>
#include <QUrl>
#include <functional>
#include <qqmlregistration.h>

class Settings;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Polls the GitHub releases feed for a newer version and installs it with no
// interaction. Two self-update paths:
//  - AppImage: download + atomic swap over $APPIMAGE.
//  - Package installs (deb/rpm/arch): /usr/bin is root-owned, so the release's
//    AppImage is downloaded and extracted ONCE (--appimage-extract, no FUSE)
//    into <AppData>/updates/<version>/; a "current" pointer file makes the
//    bootstrap in main() exec that copy on the next start. The package binary
//    becomes a bootstrap until the distro package catches up, then the staging
//    area is cleaned. No root, no package manager involved.
// Owned by AppContext, reached from QML as App.updater.
class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppContext")

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY stateChanged)
    // The new file is in place; only a restart is left.
    Q_PROPERTY(bool restartPending READ restartPending NOTIFY stateChanged)
    // "appimage" | "system" — which update affordance the UI shows.
    Q_PROPERTY(QString installKind READ installKind CONSTANT)
    // Running from an AppImage whose file (and directory) we can replace.
    Q_PROPERTY(bool canSelfUpdate READ canSelfUpdate CONSTANT)
    // A native package install under /usr — can't self-update in place, but CAN
    // be updated now by running install.sh in a spawned terminal (sudo there).
    // The "Install now" button / prompt only appears when this is true.
    Q_PROPERTY(bool canInstallViaScript READ canInstallViaScript CONSTANT)

public:
    // Callback payload shared by the smoke test and the dev button.
    struct Result {
        bool ok = false;
        bool updateAvailable = false;
        QString latestVersion;
        QString error;
    };

    explicit UpdateChecker(Settings *settings, QObject *parent = nullptr);

    bool busy() const { return m_checking || m_downloading; }
    bool updateAvailable() const { return m_available; }
    QString latestVersion() const { return m_latest; }
    QString statusText() const { return m_status; }
    bool downloading() const { return m_downloading; }
    qreal downloadProgress() const { return m_progress; }
    bool restartPending() const { return m_restartPending; }
    QString installKind() const;
    bool canSelfUpdate() const;
    bool canInstallViaScript() const;

    // Automatic policy: first check shortly after startup, then daily. No-op
    // (and stops the timer) on dev builds and while the autoCheckUpdates
    // setting is off; re-evaluated live when the setting changes.
    void startAutoCheck();

    Q_INVOKABLE void checkNow() { check(true); }
    Q_INVOKABLE void downloadAndInstall();
    // trayOnly relaunches hidden — used by the idle auto-restart so an update
    // applied while the app sits in the tray doesn't pop a window.
    Q_INVOKABLE void restartNow(bool trayOnly = false);

    // Native-package "Install now": fetch the canonical install.sh from the repo
    // to the cache, then open a terminal running it as `--self-update native`,
    // where its sudo prompt can ask for the password. Fire-and-forget — the
    // native install completes in that terminal, outside the app; emits
    // installerLaunched(ok, detail) once the terminal has (or hasn't) started.
    Q_INVOKABLE void installViaScript();
    // Dev/smoke dry-run: fetch + validate install.sh and detect a terminal, but
    // never launch anything. done(ok, humanDetail).
    void verifyInstallerReady(std::function<void(bool ok, const QString &detail)> done);

    // manual=true keeps failures visible in statusText and never toasts;
    // an automatic check additionally emits updateFound (once per version).
    void check(bool manual, std::function<void(const Result &)> done = {});
    // Dev harness: force the update-available state (fake version) so the
    // toast/tray/settings-card paths can be exercised without a newer release.
    void simulateAvailable(const QString &version);

signals:
    void stateChanged();
    // updateAvailable flipped — the tray menu rebuilds its entry on this
    // (never on stateChanged: download progress would rebuild it per chunk).
    void availabilityChanged();
    // Automatic-check discovery worth a toast; at most once per version
    // (persisted as updates/lastNotifiedVersion, a plain non-exported key).
    void updateFound(const QString &version);
    // The new version is swapped in place; a restart activates it.
    void installed(const QString &version);
    // The install.sh-in-a-terminal path was (ok=true) or wasn't (ok=false)
    // started; detail is a short human string (terminal name, or the failure).
    void installerLaunched(bool ok, const QString &detail);

private:
    QUrl feedUrl() const;
    // Canonicalized $APPIMAGE ("" when not an AppImage): launchers point the
    // variable at a symlink — the real file is what must be replaced.
    QString appImageTarget() const;
    // <AppData>/updates — staged versions for package installs. The path
    // composition must stay in sync with execStagedUpdate() in main.cpp.
    QString stagingDir() const;
    void applyAutoPolicy();
    void handleCheckReply(QNetworkReply *reply, bool manual,
                          const std::function<void(const Result &)> &done);
    void setAvailable(bool available);
    // Package-install second half: extract the downloaded AppImage into the
    // staging dir and flip the "current" pointer the bootstrap reads.
    void extractStagedAppImage(const QString &pkg);
    // Remove stage dirs other than keepVersion (never the one we run from).
    void pruneStages(const QString &keepVersion);
    // Startup: drop stages the package version has caught up with.
    void cleanStaleStage();
    QNetworkAccessManager *nam();

    // --- native "Install now" via install.sh ---
    // The canonical raw install.sh URL (env UNISIC_INSTALLER_URL overrides it for
    // dev/tests, e.g. a file:// path).
    QUrl installerScriptUrl() const;
    QString installerCacheDir() const;
    // Download install.sh to cacheFile; done(ok, error). Validates it looks like
    // the real installer (a marker line) before reporting ok.
    void fetchInstallerScript(const QString &cacheFile,
                              std::function<void(bool ok, const QString &error)> done);
    // First installed terminal emulator ("" if none), preferring $TERMINAL then
    // a KDE-first candidate list.
    static QString detectTerminal();
    // argv prefix that makes `exe` run a single script path as its command.
    static QStringList terminalArgv(const QString &exe, const QString &scriptPath);
    // Write a small wrapper that runs the installer + pauses, then spawn a
    // terminal on it. Sets m_status and emits installerLaunched.
    void launchInstallerTerminal(const QString &installerPath);

    bool m_installerBusy = false; // a fetch/launch is in flight

    Settings *m_settings;
    QNetworkAccessManager *m_nam = nullptr; // lazy; parented so quit aborts transfers
    QTimer *m_periodic = nullptr;           // 24 h repeating automatic check
    bool m_checking = false;
    bool m_downloading = false;
    bool m_available = false;
    bool m_restartPending = false;
    qreal m_progress = 0.0;
    QString m_latest;
    QString m_status;
    QString m_assetName; // matching AppImage asset of the latest release
    QString m_assetUrl;
    qint64 m_assetSize = 0;
};

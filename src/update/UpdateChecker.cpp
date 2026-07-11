#include "UpdateChecker.h"
#include "VersionCompare.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTime>
#include <QTimer>

#include <cstdio>
#include <memory>

static const char *kFeedUrl = "https://api.github.com/repos/unisic/unisic/releases/latest";
static const char *kReleasesPage = "https://github.com/unisic/unisic/releases";

UpdateChecker::UpdateChecker(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QNetworkAccessManager *UpdateChecker::nam()
{
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    return m_nam;
}

QUrl UpdateChecker::feedUrl() const
{
    // Env override lets tests/dev point the whole flow at a file:// feed.
    const QString env = qEnvironmentVariable("UNISIC_UPDATE_FEED_URL");
    return env.isEmpty() ? QUrl(QLatin1String(kFeedUrl)) : QUrl(env);
}

QString UpdateChecker::installKind() const
{
    if (qEnvironmentVariableIsSet("FLATPAK_ID"))
        return QStringLiteral("flatpak");
    if (!qEnvironmentVariable("APPIMAGE").isEmpty())
        return QStringLiteral("appimage");
    return QStringLiteral("system");
}

QString UpdateChecker::appImageTarget() const
{
    const QString env = qEnvironmentVariable("APPIMAGE");
    if (env.isEmpty())
        return {};
    const QString canon = QFileInfo(env).canonicalFilePath();
    return canon.isEmpty() ? env : canon;
}

bool UpdateChecker::canSelfUpdate() const
{
    if (installKind() != QLatin1String("appimage"))
        return false;
    const QFileInfo fi(appImageTarget());
    // The directory must be writable too: the download lands next to the
    // target (same filesystem) so the final rename is atomic.
    return fi.exists() && fi.isWritable() && QFileInfo(fi.absolutePath()).isWritable();
}

void UpdateChecker::startAutoCheck()
{
    connect(m_settings, &Settings::autoCheckUpdatesChanged,
            this, &UpdateChecker::applyAutoPolicy, Qt::UniqueConnection);
    applyAutoPolicy();
}

void UpdateChecker::applyAutoPolicy()
{
    const bool devBuild = QLatin1String(UNISIC_BUILD) == QLatin1String("dev");
    const bool eligible = !devBuild && m_settings->autoCheckUpdates();
    if (!eligible) {
        if (m_periodic)
            m_periodic->stop();
        qInfo() << "Automatic update checks off:"
                << (devBuild ? "dev build" : "disabled in settings");
        return;
    }
    const bool wasActive = m_periodic && m_periodic->isActive();
    if (!m_periodic) {
        m_periodic = new QTimer(this);
        m_periodic->setInterval(24 * 3600 * 1000);
        connect(m_periodic, &QTimer::timeout, this, [this] { check(false); });
    }
    m_periodic->start();
    // First check shortly after startup (or after re-enabling the setting) —
    // late enough that a resulting toast has a UI to appear in. The guard
    // re-reads the policy at fire time in case it flipped meanwhile.
    if (!wasActive) {
        QTimer::singleShot(10000, this, [this] {
            if (m_periodic && m_periodic->isActive())
                check(false);
        });
    }
}

void UpdateChecker::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availabilityChanged();
}

void UpdateChecker::check(bool manual, std::function<void(const Result &)> done)
{
    if (busy()) {
        if (done)
            done(Result{false, false, {}, tr("a check is already running")});
        return;
    }
    m_checking = true;
    m_status = tr("Checking for updates…");
    emit stateChanged();

    QNetworkRequest req(feedUrl());
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Unisic/" UNISIC_VERSION " (screenshot tool)"));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    QNetworkReply *reply = nam()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual, done] {
        reply->deleteLater();
        handleCheckReply(reply, manual, done);
    });
}

void UpdateChecker::handleCheckReply(QNetworkReply *reply, bool manual,
                                     const std::function<void(const Result &)> &done)
{
    m_checking = false;
    Result res;
    const QString at = QLocale().toString(QTime::currentTime(), QLocale::ShortFormat);
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // No release published yet (or only prereleases): nothing to offer, not
    // an error worth surfacing.
    if (http == 404) {
        res.ok = true;
        setAvailable(false);
        m_status = tr("Checked at %1 — up to date").arg(at);
        emit stateChanged();
        if (done)
            done(res);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        res.error = (http == 403 || http == 429)
                        ? tr("GitHub rate limit reached — try again later")
                        : reply->errorString();
        // Automatic checks fail silently (log + status only); a manual check
        // has the user looking at the status line already.
        qWarning() << "Update check failed:" << reply->errorString();
        m_status = tr("Update check failed: %1").arg(res.error);
        emit stateChanged();
        if (done)
            done(res);
        return;
    }

    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = obj.value(QLatin1String("tag_name")).toString();
    if (tag.isEmpty()) {
        res.error = tr("malformed release feed");
        qWarning() << "Update check: no tag_name in reply from" << reply->url();
        m_status = tr("Update check failed: %1").arg(res.error);
        emit stateChanged();
        if (done)
            done(res);
        return;
    }

    QString version = tag;
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version.remove(0, 1);
    m_latest = version;
    m_releaseUrl = obj.value(QLatin1String("html_url")).toString();
    if (m_releaseUrl.isEmpty())
        m_releaseUrl = QLatin1String(kReleasesPage);

    // The release's AppImage asset (self-update download). ".zsync" must not
    // match — it satisfies the same prefix.
    m_assetName.clear();
    m_assetUrl.clear();
    m_assetSize = 0;
    static const QRegularExpression assetRe(QStringLiteral("^Unisic-.*x86_64\\.AppImage$"));
    const QJsonArray assets = obj.value(QLatin1String("assets")).toArray();
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QLatin1String("name")).toString();
        if (assetRe.match(name).hasMatch()) {
            m_assetName = name;
            m_assetUrl = a.value(QLatin1String("browser_download_url")).toString();
            m_assetSize = static_cast<qint64>(a.value(QLatin1String("size")).toDouble());
            break;
        }
    }

    const bool newer = UpdateVersion::isNewer(tag, QStringLiteral(UNISIC_VERSION));
    res.ok = true;
    res.updateAvailable = newer;
    res.latestVersion = version;
    setAvailable(newer);
    m_status = newer ? tr("Checked at %1 — version %2 is available").arg(at, version)
                     : tr("Checked at %1 — up to date").arg(at);
    emit stateChanged();

    if (newer && !manual) {
        // Toast a given version once, ever — a tray-dwelling app re-checks
        // daily and on every launch, and repeat toasts read as nagging.
        const QString key = QStringLiteral("updates/lastNotifiedVersion");
        if (m_settings->raw()->value(key).toString() != version) {
            m_settings->raw()->setValue(key, version);
            m_settings->raw()->sync();
            emit updateFound(version);
        }
    }
    if (done)
        done(res);
    // Fully automatic pipeline: a found update on a self-updatable install
    // downloads and swaps in place with no interaction — only the restart is
    // left to the user (killing a recording in progress is not an option).
    // A failed download retries on the next (daily/startup) check.
    if (newer && !m_restartPending && canSelfUpdate())
        downloadAndInstall();
}

void UpdateChecker::downloadAndInstall()
{
    if (busy() || !m_available || m_restartPending)
        return;
    if (!canSelfUpdate() || m_assetUrl.isEmpty()) {
        m_status = m_assetUrl.isEmpty()
                       ? tr("This release has no AppImage — use the download page")
                       : tr("This install can't update itself — use the download page");
        emit stateChanged();
        return;
    }

    const QString target = appImageTarget();
    const QString part = QFileInfo(target).absolutePath() + QLatin1Char('/')
                         + m_assetName + QStringLiteral(".part");
    auto file = std::make_shared<QFile>(part);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_status = tr("Update failed: cannot write %1").arg(part);
        emit stateChanged();
        return;
    }

    m_downloading = true;
    m_progress = 0.0;
    m_status = tr("Downloading version %1…").arg(m_latest);
    emit stateChanged();

    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Unisic/" UNISIC_VERSION " (screenshot tool)"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(600000);
    QNetworkReply *reply = nam()->get(req);
    // Stream to disk — the asset is ~100 MB, never readAll() it in one piece.
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] {
        if (file->isOpen())
            file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 expected = total > 0 ? total : m_assetSize;
        m_progress = expected > 0 ? qreal(got) / qreal(expected) : 0.0;
        emit stateChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, part, target] {
        reply->deleteLater();
        const auto fail = [this, file, part](const QString &why) {
            file->close();
            QFile::remove(part);
            m_downloading = false;
            m_status = tr("Update failed: %1").arg(why);
            emit stateChanged();
        };
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->errorString());
            return;
        }
        file->write(reply->readAll()); // tail bytes after the last readyRead
        if (!file->flush()) {
            fail(tr("cannot write the file"));
            return;
        }
        file->close();
        // Exact-size check against the API's asset size: a truncated file
        // must never be renamed over a working app.
        if (m_assetSize > 0 && QFileInfo(part).size() != m_assetSize) {
            fail(tr("download looks truncated"));
            return;
        }
        // Keep the user's mode, guarantee the exec bits.
        QFile::setPermissions(part, QFile::permissions(target) | QFileDevice::ExeOwner
                                        | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        // Atomic overwrite: at no instant is there no app at $APPIMAGE (a
        // remove-then-rename would leave exactly that on a crash between the
        // two). The running instance is unaffected — its mount holds the old
        // inode.
        if (std::rename(QFile::encodeName(part).constData(),
                        QFile::encodeName(target).constData()) != 0) {
            fail(tr("cannot replace %1").arg(target));
            return;
        }
        m_downloading = false;
        m_progress = 1.0;
        m_restartPending = true;
        m_status = tr("Update installed — restart to run version %1").arg(m_latest);
        emit stateChanged();
        emit installed(m_latest);
    });
}

void UpdateChecker::restartNow()
{
    const QString target = appImageTarget();
    if (target.isEmpty())
        return;
    // The 1 s shim lets this instance's single-instance socket disappear
    // first — a fresh process that still finds it would hand off "show" to
    // the exiting old version and quit itself.
    QProcess::startDetached(QStringLiteral("/bin/sh"),
                            {QStringLiteral("-c"), QStringLiteral("sleep 1; exec \"$0\""), target});
    QCoreApplication::quit();
}

void UpdateChecker::simulateAvailable(const QString &version)
{
    m_latest = version;
    m_restartPending = false;
    if (m_releaseUrl.isEmpty())
        m_releaseUrl = QLatin1String(kReleasesPage);
    m_status = tr("Simulated: version %1 is available").arg(version);
    setAvailable(true);
    emit stateChanged();
    emit updateFound(version); // deliberately skips the once-per-version gate
}

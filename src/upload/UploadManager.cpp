#include "UploadManager.h"
#include "Settings.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QMimeDatabase>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <memory>

// Imgur's anonymous image endpoint needs an "Authorization: Client-ID <id>"
// header, and the ID is deliberately NOT compiled in: it identifies the
// *application*, so one shipped ID would put every user's uploads on one
// per-app daily cap (~1250) and leave whoever registered it answerable to
// Imgur for what strangers upload. Each user registers their own at
// https://api.imgur.com/oauth2/addclient ("Anonymous usage without user
// authorisation") and pastes it into the destination (Destinations → Imgur).
// Anonymous uploads are not tied to the ID owner's gallery.
// Up to 0.7 the builtin shipped with a placeholder ID baked into the
// Authorization header, which made every upload fail — the stored copy is
// repaired in ensureBuiltins().
static const char kImgurPlaceholderId[] = "REPLACE_WITH_YOUR_IMGUR_CLIENT_ID";
static const char kImgurHost[] = "api.imgur.com";
static const char kVgyMeHost[] = "vgy.me";
#if 0 // GitHub Pages: parked, see docs/dev/architecture.md
static const char kGitHubApiHost[] = "api.github.com";
// The preset ships with this in place of owner/name: it keeps the request URL
// recognisable as GitHub, and gitHubSetup() reads it back as "no repository".
static const char kGitHubPlaceholderRepo[] = "OWNER/REPO";
// The tail of a GitHub Pages urlPath: the public address plus the name the
// API confirms it committed.
static const char kGitHubNameToken[] = "/$json:content.name$";
#endif

// The two squares of the checkerboard testDestination() uploads: the mandatory
// palette's Secondary (#2E236C) and Accent (#C8ACD6), i.e. the `secondary` and
// `accent` tokens of the unisic palette in Theme.qml, so the test image is
// recognisably Unisic in a server's file listing. UploadManager is plain C++
// with no access to the QML Theme singleton, so the two values are mirrored by
// hand HERE and nowhere else - if the palette changes, this is the only place
// outside Theme.qml to follow it.
static constexpr QRgb kTestTileSecondary = 0xff2e236cu;
static constexpr QRgb kTestTileAccent = 0xffc8acd6u;

// Stall (INACTIVITY) timeouts for the http path: they restart on every byte
// transferred, so a slow but progressing transfer of any size is unaffected;
// only a server that accepts the connection and then goes quiet hits them.
//   Capture: a real upload can be a hundreds-of-MB recording on a hotel Wi-Fi
//   and nobody is staring at it, so it gets the long grace period. This is also
//   the manager-wide default set in the constructor.
//   Test: a few hundred bytes with the server editor's sheet stuck on
//   "Testing…" until an answer arrives, so it gives up in half the time.
static constexpr int kCaptureStallMs = 120000;
static constexpr int kTestStallMs = 60000;

// Absolute ceilings on a whole curl transfer - NOT inactivity timeouts (the
// watchdog in curlUpload explains why curl needs one on top of --speed-time).
// A real upload may legitimately run for a long time; a test moves a few
// hundred bytes, so its entire life is capped at the minute a stalled test
// would get.
static constexpr int kCaptureCurlCeilingMs = 30 * 60 * 1000;
static constexpr int kTestCurlCeilingMs = 60000;

bool UploadManager::isImgur(const QJsonObject &dest)
{
    return QUrl(dest.value(QStringLiteral("requestUrl")).toString()).host()
           == QLatin1String(kImgurHost);
}

// The Client-ID out of a destination's Authorization header ("Client-ID <id>"),
// empty when unset or still the shipped placeholder.
QString UploadManager::imgurClientId(const QJsonObject &dest)
{
    const QString auth = dest.value(QStringLiteral("headers")).toObject()
                             .value(QStringLiteral("Authorization")).toString().trimmed();
    if (!auth.startsWith(QLatin1String("Client-ID "), Qt::CaseInsensitive))
        return {};
    const QString id = auth.mid(10).trimmed();
    return id == QLatin1String(kImgurPlaceholderId) ? QString() : id;
}

bool UploadManager::isVgyMe(const QJsonObject &dest)
{
    const QString host = QUrl(dest.value(QStringLiteral("requestUrl")).toString()).host().toLower();
    return host == QLatin1String(kVgyMeHost) || host.endsWith(QLatin1String(".vgy.me"));
}

QString UploadManager::vgyMeUserKey(const QJsonObject &dest)
{
    const QJsonObject args = dest.value(QStringLiteral("arguments")).toObject();
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.key().compare(QLatin1String("userkey"), Qt::CaseInsensitive) == 0)
            return it.value().toString().trimmed();
    }
    return {};
}

#if 0 // GitHub Pages: parked, see docs/dev/architecture.md
bool UploadManager::isGitHub(const QJsonObject &dest)
{
    const QUrl url(dest.value(QStringLiteral("requestUrl")).toString());
    return url.host().compare(QLatin1String(kGitHubApiHost), Qt::CaseInsensitive) == 0
           && url.path().contains(QLatin1String("/contents/"));
}

// "owner/name" out of whatever was pasted: the repository's github.com page,
// a clone URL, a trailing slash or .git. Empty when there is no owner/name.
static QString gitHubRepoName(QString repo)
{
    static const QRegularExpression site(
        QStringLiteral("^(?:https?://)?(?:www\\.)?github\\.com[/:]"),
        QRegularExpression::CaseInsensitiveOption);
    repo = repo.trimmed();
    repo.remove(site);
    if (repo.endsWith(QLatin1String(".git"), Qt::CaseInsensitive))
        repo.chop(4);
    const QStringList parts = repo.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};
    return parts.at(0).trimmed() + QLatin1Char('/') + parts.at(1).trimmed();
}

// A folder path the way the Contents API wants it: no slash at either end.
static QString gitHubFolder(QString folder)
{
    folder = folder.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList parts = folder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.join(QLatin1Char('/'));
}

QString UploadManager::gitHubPagesUrl(const QString &repoIn, const QString &folderIn)
{
    const QString repo = gitHubRepoName(repoIn);
    if (repo.isEmpty())
        return {};
    const QString owner = repo.section(QLatin1Char('/'), 0, 0).toLower();
    const QString name = repo.section(QLatin1Char('/'), 1, 1);
    QString url = QStringLiteral("https://") + owner + QStringLiteral(".github.io");
    if (name.compare(owner + QLatin1String(".github.io"), Qt::CaseInsensitive) != 0)
        url += QLatin1Char('/') + name;
    const QString folder = gitHubFolder(folderIn);
    if (!folder.isEmpty())
        url += QLatin1Char('/') + folder;
    return url;
}

QJsonObject UploadManager::gitHubDestination(const GitHubSetup &s)
{
    QString repo = gitHubRepoName(s.repo);
    if (repo.isEmpty())
        repo = QLatin1String(kGitHubPlaceholderRepo);
    const QString folder = gitHubFolder(s.folder);
    const QString branch = s.branch.trimmed().isEmpty() ? QStringLiteral("main") : s.branch.trimmed();
    QString publicUrl = s.publicUrl.trimmed();
    if (publicUrl.isEmpty())
        publicUrl = gitHubPagesUrl(repo, folder);
    while (publicUrl.endsWith(QLatin1Char('/')))
        publicUrl.chop(1);
    QJsonObject headers{
        {QStringLiteral("Accept"), QStringLiteral("application/vnd.github+json")},
        {QStringLiteral("X-GitHub-Api-Version"), QStringLiteral("2022-11-28")},
    };
    if (!s.token.trimmed().isEmpty())
        headers.insert(QStringLiteral("Authorization"), QStringLiteral("Bearer ") + s.token.trimmed());
    // PUT /repos/{owner}/{repo}/contents/{path}: the file goes base64 inside a
    // JSON body, and the commit message names it so the repository's log
    // reads like an upload log. Built as JSON so the branch is escaped.
    const QJsonObject data{
        {QStringLiteral("message"), QStringLiteral("unisic: $filename$")},
        {QStringLiteral("branch"), branch},
        {QStringLiteral("content"), QStringLiteral("$base64$")},
    };
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("http")},
        {QStringLiteral("requestUrl"), QStringLiteral("https://api.github.com/repos/") + repo
                                           + QStringLiteral("/contents/")
                                           + (folder.isEmpty() ? QString() : folder + QLatin1Char('/'))
                                           + QStringLiteral("%file%")},
        {QStringLiteral("method"), QStringLiteral("PUT")},
        {QStringLiteral("headers"), headers},
        {QStringLiteral("body"), QStringLiteral("json")},
        {QStringLiteral("data"), QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))},
        {QStringLiteral("responseType"), QStringLiteral("json")},
        {QStringLiteral("urlPath"), publicUrl + QLatin1String(kGitHubNameToken)},
        // The file's page on GitHub, where it can be deleted.
        {QStringLiteral("deletionUrlPath"), QStringLiteral("$json:content.html_url$")},
    };
}

UploadManager::GitHubSetup UploadManager::gitHubSetup(const QJsonObject &dest)
{
    GitHubSetup s;
    if (!isGitHub(dest))
        return s;
    static const QRegularExpression re(
        QStringLiteral("^https://api\\.github\\.com/repos/([^/]+/[^/]+)/contents/(?:(.*)/)?%file%$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(dest.value(QStringLiteral("requestUrl")).toString().trimmed());
    const QString storedRepo = m.captured(1);
    if (storedRepo != QLatin1String(kGitHubPlaceholderRepo))
        s.repo = storedRepo;
    s.folder = m.captured(2);
    s.branch = QJsonDocument::fromJson(dest.value(QStringLiteral("data")).toString().toUtf8())
                   .object().value(QStringLiteral("branch")).toString();
    const QString auth = dest.value(QStringLiteral("headers")).toObject()
                             .value(QStringLiteral("Authorization")).toString().trimmed();
    if (auth.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        s.token = auth.mid(7).trimmed();
    else if (auth.startsWith(QLatin1String("token "), Qt::CaseInsensitive))
        s.token = auth.mid(6).trimmed();
    QString urlPath = dest.value(QStringLiteral("urlPath")).toString().trimmed();
    if (urlPath.endsWith(QLatin1String(kGitHubNameToken))) {
        urlPath.chop(int(qstrlen(kGitHubNameToken)));
        if (urlPath != gitHubPagesUrl(storedRepo, s.folder))
            s.publicUrl = urlPath;
    }
    return s;
}

bool UploadManager::gitHubReady(const QJsonObject &dest)
{
    const GitHubSetup s = gitHubSetup(dest);
    return !s.repo.isEmpty() && !s.token.isEmpty();
}

QVariantMap UploadManager::gitHubSetupOf(const QVariantMap &dest) const
{
    const GitHubSetup s = gitHubSetup(QJsonObject::fromVariantMap(dest));
    return {{QStringLiteral("repo"), s.repo},
            {QStringLiteral("branch"), s.branch},
            {QStringLiteral("folder"), s.folder},
            {QStringLiteral("token"), s.token},
            {QStringLiteral("publicUrl"), s.publicUrl}};
}

QVariantMap UploadManager::gitHubDestinationFor(const QString &repo, const QString &branch,
                                                const QString &folder, const QString &token,
                                                const QString &publicUrl) const
{
    return gitHubDestination({repo, branch, folder, token, publicUrl}).toVariantMap();
}
#endif

UploadManager::UploadManager(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // Manager-wide fallback for any request that does not state its own (see
    // kCaptureStallMs). Without it a server that accepts the connection and
    // stalls pins the reply + the full multipart payload forever and wedges the
    // busy state. Surfaces as OperationCanceledError through the existing
    // finished handler. (int overload: the chrono one needs Qt 6.7.)
    m_nam->setTransferTimeout(kCaptureStallMs);
    loadDestinations();
    // Before anything persists: a save makes the top server the active one,
    // and up to 0.8.8 the active one could sit anywhere in the list.
    const bool moved = moveActiveToTop();
    ensureBuiltins();
    // The save also points the setting at the top: a 0.8.8 config may name a
    // server that was deleted, or none.
    if (moved || (m_settings && m_settings->activeDestination() != destinationNames().value(0)))
        persistDestinations();
    if (m_settings)
        connect(m_settings, &Settings::activeDestinationChanged, this, [this] {
            if (moveActiveToTop())
                persistDestinations();
        });
}

// The top of the list IS the active server. The Servers page picks it by
// dragging; everything else (the Capture page's combo, an sxcu import, a
// settings import) names it, and the named one moves up. Memory only.
bool UploadManager::moveActiveToTop()
{
    if (!m_settings)
        return false;
    const int at = destinationNames().indexOf(m_settings->activeDestination());
    if (at <= 0)
        return false;
    m_destinations.prepend(m_destinations.takeAt(at));
    return true;
}

QString UploadManager::configPath() const
{
    // UnisicConfig::configDir(): "unisic-dev" in dev builds — the dev app is
    // fully separate from the installed stable one.
    const QString path = UnisicConfig::configDir() + QStringLiteral("/destinations.json");
#ifdef UNISIC_DEV_BUILD
    // First dev run: start from the stable app's destinations (plain JSON, a
    // raw copy is fine) instead of only the builtins.
    const QString stable = UnisicConfig::stableConfigDir() + QStringLiteral("/destinations.json");
    if (!QFile::exists(path) && QFile::exists(stable))
        QFile::copy(stable, path);
#endif
    return path;
}

// destinations.json stores FTP/SFTP logins and API keys in clear text, so it
// must never be group- or world-readable. QSaveFile normalises the committed
// file to the umask default (0644 on a stock Fedora), which is why this runs
// after every write, and once more on load to repair a config left behind by
// an older build. Same reasoning as exportSettings() in AppContext.
static void lockToOwner(const QString &path)
{
    const QFileDevice::Permissions ownerOnly =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (!QFile::exists(path) || QFile::permissions(path) == ownerOnly)
        return;
    if (!QFile::setPermissions(path, ownerOnly))
        qWarning() << "could not restrict permissions on" << path;
}

void UploadManager::loadDestinations()
{
    lockToOwner(configPath());
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = f.readAll();
    f.close();
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isArray()) {
        m_destinations = doc.array();
        return;
    }
    if (raw.trimmed().isEmpty())
        return;
    // Unparseable but non-empty (hand-edit typo, truncation): move it aside
    // instead of proceeding — ensureBuiltins() would otherwise rewrite the
    // file with only the builtins, silently destroying every custom
    // destination (including stored SFTP credentials).
    const QString bak = configPath() + QStringLiteral(".broken-")
                        + QString::number(QDateTime::currentSecsSinceEpoch());
    QFile::rename(configPath(), bak);
    qWarning() << "destinations.json unparseable, preserved as" << bak;
}

void UploadManager::persistDestinations()
{
    // Atomic write — a crash mid-write must never truncate the file (same
    // QSaveFile pattern as HistoryStore).
    QSaveFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_destinations).toJson(QJsonDocument::Indented));
        if (f.commit())
            lockToOwner(configPath());
        else
            qWarning() << "could not write" << configPath() << f.errorString();
    } else {
        qWarning() << "could not open" << configPath() << f.errorString();
    }
    // A reorder, a delete or a rename can change the top; the setting follows.
    if (m_settings && !m_destinations.isEmpty())
        m_settings->setActiveDestination(
            m_destinations.first().toObject().value(QStringLiteral("name")).toString());
    emit destinationsChanged();
}

void UploadManager::ensureBuiltins()
{
    auto has = [this](const QString &name) {
        for (const QJsonValue &v : std::as_const(m_destinations))
            if (v.toObject().value(QStringLiteral("name")).toString() == name)
                return true;
        return false;
    };
    bool changed = false;
    if (!has(QStringLiteral("catbox.moe"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("catbox.moe")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://catbox.moe/user/api.php")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("fileToUpload")},
            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("reqtype"), QStringLiteral("fileupload")}}},
            {QStringLiteral("responseType"), QStringLiteral("text")},
            {QStringLiteral("urlPath"), QStringLiteral("$text$")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
    // 0x0.st was a builtin up to 0.7.1b and is dropped: the host rejects the
    // uploads in practice, so it only ever produced failures. Prune the stored
    // copy from existing configs (builtin-flagged only — a user's renamed clone
    // drops that flag, and a hand-made 0x0.st destination is theirs to keep).
    for (int i = m_destinations.size() - 1; i >= 0; --i) {
        const QJsonObject d = m_destinations.at(i).toObject();
        if (d.value(QStringLiteral("name")).toString() == QStringLiteral("0x0.st")
            && d.value(QStringLiteral("builtin")).toBool()) {
            m_destinations.removeAt(i);
            changed = true;
        }
    }
    // The pruned host may still be the selected one; uploads would silently
    // go to the first destination while the UI shows a dead name. Point the
    // setting at the default instead.
    if (m_settings && m_settings->activeDestination() == QStringLiteral("0x0.st")) {
        m_settings->setActiveDestination(QStringLiteral("catbox.moe"));
        moveActiveToTop();
    }
    // The Imgur builtin ships WITHOUT an Authorization header: the user pastes
    // their own Client-ID (Destinations → Imgur). Until then startUpload() fails
    // it fast with an actionable message instead of letting Imgur reject it.
    if (!has(QStringLiteral("Imgur (anonymous)"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Imgur (anonymous)")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://api.imgur.com/3/image")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("image")},
            {QStringLiteral("responseType"), QStringLiteral("json")},
            {QStringLiteral("urlPath"), QStringLiteral("$json:data.link$")},
            {QStringLiteral("deletionUrlPath"), QStringLiteral("https://imgur.com/delete/$json:data.deletehash$")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
    // Repair configs written by 0.7 and earlier: the stored builtin carries the
    // placeholder Client-ID, so every Imgur upload failed. Strip the dead header
    // (leaving the destination visibly unconfigured) rather than dropping the
    // destination — a user who pastes a real ID keeps their selection.
    for (int i = 0; i < m_destinations.size(); ++i) {
        QJsonObject d = m_destinations.at(i).toObject();
        if (!d.value(QStringLiteral("builtin")).toBool() || !isImgur(d))
            continue;
        QJsonObject h = d.value(QStringLiteral("headers")).toObject();
        if (!h.value(QStringLiteral("Authorization")).toString()
                 .contains(QLatin1String(kImgurPlaceholderId)))
            continue;
        h.remove(QStringLiteral("Authorization"));
        if (h.isEmpty())
            d.remove(QStringLiteral("headers"));
        else
            d.insert(QStringLiteral("headers"), h);
        m_destinations.replace(i, d);
        changed = true;
    }
    if (!has(QStringLiteral("uguu.se (48h)"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("uguu.se (48h)")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://uguu.se/upload")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("files[]")},
            {QStringLiteral("responseType"), QStringLiteral("json")},
            {QStringLiteral("urlPath"), QStringLiteral("$json:files[0].url$")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
    if (!has(QStringLiteral("litterbox (72h)"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("litterbox (72h)")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://litterbox.catbox.moe/resources/internals/api.php")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("fileToUpload")},
            {QStringLiteral("arguments"), QJsonObject{{QStringLiteral("reqtype"), QStringLiteral("fileupload")},
                                                      {QStringLiteral("time"), QStringLiteral("72h")}}},
            {QStringLiteral("responseType"), QStringLiteral("text")},
            {QStringLiteral("urlPath"), QStringLiteral("$text$")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
    if (!has(QStringLiteral("tmpfiles.org (1h)"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("tmpfiles.org (1h)")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://tmpfiles.org/api/v1/upload")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("file")},
            {QStringLiteral("responseType"), QStringLiteral("json")},
            {QStringLiteral("urlPath"), QStringLiteral("$json:data.url$")},
            // API returns a viewer URL; rewrite to the direct-download form.
            {QStringLiteral("urlReplace"), QStringLiteral("tmpfiles.org/")},
            {QStringLiteral("urlReplaceWith"), QStringLiteral("tmpfiles.org/dl/")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
    // The one builtin that goes out over curl, and the reason the curl path
    // learned the %file% token and response parsing: buzzheavier takes an
    // anonymous PUT whose URL ends in the file name (no account, no key, no
    // form field) and answers with the id its download page is built from -
    // {"code":201,"data":{"id":"…","name":"…",…}}. Verified against the
    // published API and one real upload.
    if (!has(QStringLiteral("buzzheavier.com"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("buzzheavier.com")},
            {QStringLiteral("type"), QStringLiteral("curl")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://w.buzzheavier.com/%file%")},
            {QStringLiteral("urlPath"), QStringLiteral("https://buzzheavier.com/$json:data.id$")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
    if (!has(QStringLiteral("vgy.me"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("vgy.me")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://vgy.me/upload")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("file")},
            {QStringLiteral("responseType"), QStringLiteral("json")},
            {QStringLiteral("urlPath"), QStringLiteral("$json:image$")},
            {QStringLiteral("deletionUrlPath"), QStringLiteral("$json:delete$")},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
#if 0 // GitHub Pages: parked, see docs/dev/architecture.md
    // Ships without a repository or token (both are the user's), so it sits
    // in the list as "needs setup" until the editor fills it in.
    if (!has(QStringLiteral("GitHub Pages"))) {
        QJsonObject gh = gitHubDestination({});
        gh.insert(QStringLiteral("name"), QStringLiteral("GitHub Pages"));
        gh.insert(QStringLiteral("builtin"), true);
        m_destinations.append(gh);
        changed = true;
    }
#endif
    if (changed)
        persistDestinations();
}

QVariantList UploadManager::destinationsVariant() const
{
    return m_destinations.toVariantList();
}

QStringList UploadManager::destinationNames() const
{
    QStringList names;
    for (const QJsonValue &v : std::as_const(m_destinations))
        names << v.toObject().value(QStringLiteral("name")).toString();
    return names;
}

QVariantMap UploadManager::destination(const QString &name) const
{
    return findDestination(name).toVariantMap();
}

void UploadManager::saveDestination(const QVariantMap &dest, const QString &replacing)
{
    const QString name = dest.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) return;
    const QString target = replacing.isEmpty() ? name : replacing;
    QJsonObject obj = QJsonObject::fromVariantMap(dest);
    for (int i = 0; i < m_destinations.size(); ++i) {
        if (m_destinations[i].toObject().value(QStringLiteral("name")).toString() == target) {
            m_destinations[i] = obj;
            persistDestinations();
            return;
        }
    }
    m_destinations.append(obj);
    persistDestinations();
}

void UploadManager::moveDestination(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_destinations.size()
        || to >= m_destinations.size())
        return;
    m_destinations.insert(to, m_destinations.takeAt(from));
    persistDestinations();
}

void UploadManager::removeDestination(const QString &name)
{
    for (int i = 0; i < m_destinations.size(); ++i) {
        if (m_destinations[i].toObject().value(QStringLiteral("name")).toString() == name) {
            m_destinations.removeAt(i);
            persistDestinations();
            return;
        }
    }
}

void UploadManager::replaceAllDestinations(const QJsonArray &arr)
{
    m_destinations = arr;
    // The imported settings named the active server; an older export did not
    // keep it on top.
    moveActiveToTop();
    ensureBuiltins(); // guarantees the defaults survive a foreign import
    persistDestinations();
}

// ShareX response tokens -> Unisic tokens. ShareX has used both the older
// $json:path$ form (already ours) and the current {json:path} / {regex:...} /
// {response} braces form. Normalise all of them to $json:...$ / $regex:...$ /
// $text$ so extractUrl() can resolve them.
static QString translateShareXTokens(const QString &in)
{
    if (in.isEmpty())
        return in;
    QString s = in;
    // {response} -> $text$
    s.replace(QLatin1String("{response}"), QLatin1String("$text$"));
    // {json:PATH} -> $json:PATH$   ,   {regex:PATT} -> $regex:PATT$
    static const QRegularExpression brace(QStringLiteral("\\{(json|regex):([^}]+)\\}"));
    QString out;
    int last = 0;
    auto it = brace.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += QLatin1Char('$') + m.captured(1) + QLatin1Char(':') + m.captured(2) + QLatin1Char('$');
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

// Map a parsed .sxcu object to a Unisic destination object. Handles the common
// image/file uploader shape; unknown fields are ignored.
static QJsonObject sxcuToDestination(const QJsonObject &sx, const QString &fallbackName)
{
    const auto pick = [&sx](std::initializer_list<const char *> keys) -> QJsonValue {
        for (const char *k : keys) {
            const QString key = QString::fromLatin1(k);
            if (sx.contains(key))
                return sx.value(key);
        }
        return {};
    };

    QJsonObject dest;
    dest.insert(QStringLiteral("type"), QStringLiteral("http"));

    QString name = pick({"Name"}).toString();
    if (name.isEmpty())
        name = fallbackName;
    dest.insert(QStringLiteral("name"), name);

    // The request endpoint comes ONLY from RequestURL/RequestUrl. ShareX's "URL"
    // field is the *response* template (mapped to urlPath below), so it must never
    // be reused as the endpoint — otherwise a file missing RequestURL would be
    // imported as a broken destination that POSTs to the literal template string.
    QString url = pick({"RequestURL", "RequestUrl"}).toString();
    // ShareX allows query parameters as a separate "Parameters" object.
    const QJsonObject params = pick({"Parameters"}).toObject();
    // Only merge Parameters into a real endpoint — merging into an empty URL
    // yields "?key=val", which slips past importSxcu's missing-RequestURL check.
    if (!params.isEmpty() && !url.isEmpty()) {
        QUrl u(url);
        QUrlQuery q(u);
        for (auto it = params.begin(); it != params.end(); ++it)
            q.addQueryItem(it.key(), it.value().toString());
        u.setQuery(q);
        url = u.toString();
    }
    dest.insert(QStringLiteral("requestUrl"), url);

    QString method = pick({"RequestMethod", "RequestType"}).toString(QStringLiteral("POST")).toUpper();
    if (method.isEmpty()) method = QStringLiteral("POST");
    dest.insert(QStringLiteral("method"), method);

    const QString body = pick({"Body"}).toString();
    if (body.compare(QLatin1String("JSON"), Qt::CaseInsensitive) == 0) {
        dest.insert(QStringLiteral("body"), QStringLiteral("json"));
        dest.insert(QStringLiteral("data"), translateShareXTokens(pick({"Data"}).toString()));
    }
    // "MultipartFormData" and everything else use the default multipart path.

    const QString fileForm = pick({"FileFormName"}).toString();
    if (!fileForm.isEmpty())
        dest.insert(QStringLiteral("fileFormName"), fileForm);

    const QJsonObject headers = pick({"Headers"}).toObject();
    if (!headers.isEmpty())
        dest.insert(QStringLiteral("headers"), headers);

    const QJsonObject arguments = pick({"Arguments"}).toObject();
    if (!arguments.isEmpty())
        dest.insert(QStringLiteral("arguments"), arguments);

    const QString urlPath = translateShareXTokens(pick({"URL"}).toString());
    // If RequestURL and URL collided (some sxcu reuse "URL" for the endpoint),
    // only treat it as the result path when it actually carries a token.
    if (!urlPath.isEmpty() && urlPath.contains(QLatin1Char('$')))
        dest.insert(QStringLiteral("urlPath"), urlPath);
    else
        dest.insert(QStringLiteral("urlPath"), QStringLiteral("$text$"));

    const QString delPath = translateShareXTokens(pick({"DeletionURL", "DeletionUrl"}).toString());
    if (!delPath.isEmpty())
        dest.insert(QStringLiteral("deletionUrlPath"), delPath);

    dest.insert(QStringLiteral("responseType"),
                (body.compare(QLatin1String("JSON"), Qt::CaseInsensitive) == 0
                 || dest.value(QStringLiteral("urlPath")).toString().contains(QLatin1String("$json:")))
                    ? QStringLiteral("json") : QStringLiteral("text"));
    return dest;
}

// One entry of templateHelp()'s "vars". Free function so the table below reads
// as a table.
static QVariantMap tplVar(const QString &token, const QString &label,
                          const QString &description, int caretBack = 0)
{
    return {{QStringLiteral("token"), token},
            {QStringLiteral("label"), label},
            {QStringLiteral("description"), description},
            {QStringLiteral("caretBack"), caretBack}};
}

QVariantMap UploadManager::templateHelp(const QString &field, const QString &type) const
{
    const bool curl = type.compare(QLatin1String("curl"), Qt::CaseInsensitive) == 0;
    QVariantList vars;
    QString pattern;

    if (field == QLatin1String("requestUrl")) {
        // Both senders substitute it (requestUrlWithFileName); only curl also
        // appends the name when the token is absent, so only curl's chip can
        // promise the field works without it.
        pattern = QStringLiteral("%file%");
        vars = {tplVar(QStringLiteral("%file%"), tr("File name"),
                       curl ? tr("The name of the uploaded file. Without it the name is added at "
                                 "the end of the address, which is what FTP and SFTP folders want.")
                            : tr("The name of the uploaded file, put right into the address."))};
    } else if (field == QLatin1String("data")) {
        // The three tokens sendHttp() substitutes into a custom JSON body.
        pattern = QStringLiteral("\\$(?:base64|filename|mime)\\$");
        vars = {tplVar(QStringLiteral("$base64$"), tr("File as base64"),
                       tr("The whole file, base64-encoded, inline in the body.")),
                tplVar(QStringLiteral("$filename$"), tr("File name"),
                       tr("The name of the uploaded file.")),
                tplVar(QStringLiteral("$mime$"), tr("File type"),
                       tr("The file's MIME type, for example image/png."))};
    } else if (field == QLatin1String("urlPath")) {
        // Kept character for character in step with extractUrl()'s tokenRe: the
        // pill has to appear exactly when the token is one that resolves.
        pattern = QStringLiteral("\\$(?:text|json:[^$]+|regex:[^$]+)\\$");
        vars = {tplVar(QStringLiteral("$text$"), tr("Whole answer"),
                       tr("Everything the server answered, used as the link.")),
                tplVar(QStringLiteral("$json:$"), tr("Value from JSON"),
                       tr("A field of a JSON answer, by path: $json:data.link$ or "
                          "$json:files[0].url$."),
                       /*caretBack=*/1),
                tplVar(QStringLiteral("$regex:$"), tr("Match by pattern"),
                       tr("The first group a regular expression matches in the answer."),
                       /*caretBack=*/1)};
    }

    return {{QStringLiteral("pattern"), pattern}, {QStringLiteral("vars"), vars}};
}

QString UploadManager::importSxcu(const QString &pathOrUrl)
{
    m_lastImportError.clear();
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1String("file://")))
        path = QUrl(path).toLocalFile();

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_lastImportError = tr("Cannot open %1").arg(path);
        return {};
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastImportError = tr("Not a valid .sxcu file: %1").arg(perr.errorString());
        return {};
    }

    QFileInfo fi(path);
    const QJsonObject src = doc.object();
    const QJsonObject dest = sxcuToDestination(src, fi.completeBaseName());
    if (dest.value(QStringLiteral("requestUrl")).toString().isEmpty()) {
        m_lastImportError = tr("The .sxcu file has no RequestURL");
        return {};
    }
    // ShareX RegexList-based response parsing has different semantics from our
    // $regex:$ (which is a literal pattern). Importing it silently would resolve
    // every URL to garbage, so reject it with a clear message instead.
    const QString up = dest.value(QStringLiteral("urlPath")).toString();
    const QString dp = dest.value(QStringLiteral("deletionUrlPath")).toString();
    if (src.contains(QStringLiteral("RegexList"))
        || up.contains(QStringLiteral("$regex:")) || dp.contains(QStringLiteral("$regex:"))) {
        m_lastImportError = tr("This .sxcu uses ShareX RegexList response parsing, which "
                               "Unisic can't import yet. Edit the destination's URL extractor "
                               "to a $json:…$ or $text$ token after importing.");
        return {};
    }
    saveDestination(dest.toVariantMap());
    return dest.value(QStringLiteral("name")).toString();
}

QJsonObject UploadManager::findDestination(const QString &name) const
{
    for (const QJsonValue &v : std::as_const(m_destinations))
        if (v.toObject().value(QStringLiteral("name")).toString() == name)
            return v.toObject();
    return {};
}

void UploadManager::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

void UploadManager::uploadFile(const QString &filePath, Callback cb, const QString &primary)
{
    uploadFileTo(uploadChainFor(primary), filePath, std::move(cb));
}

void UploadManager::uploadData(const QByteArray &data, const QString &fileName,
                               const QString &mime, Callback cb, const QString &primary)
{
    startUpload(uploadChainFor(primary), data, {}, fileName, mime, std::move(cb));
}

void UploadManager::uploadDataTo(const QStringList &servers, const QByteArray &data,
                                 const QString &fileName, const QString &mime, Callback cb)
{
    startUpload(servers, data, {}, fileName, mime, std::move(cb));
}

void UploadManager::uploadFileTo(const QStringList &servers, const QString &filePath, Callback cb)
{
    {
        // Early readability check only - the payload itself is streamed from
        // disk (a saved recording can be hundreds of MB; readAll() used to pin
        // it in RAM for the whole transfer, twice on the curl path).
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            cb({}, {}, tr("Cannot read %1").arg(filePath));
            return;
        }
    }
    startUpload(servers, {}, filePath, QFileInfo(filePath).fileName(),
                QMimeDatabase().mimeTypeForFile(filePath).name(), std::move(cb));
}

QStringList UploadManager::uploadChain(const QString &primary, const QStringList &order,
                                       const QStringList &ready, bool chain)
{
    const QString first = order.contains(primary) ? primary : order.value(0);
    if (first.isEmpty())
        return {};
    QStringList names{first};
    if (chain)
        for (const QString &n : order)
            if (ready.contains(n) && !names.contains(n))
                names << n;
    return names;
}

QStringList UploadManager::uploadChainFor(const QString &primary) const
{
    QStringList ready;
    for (const QJsonValue &v : std::as_const(m_destinations)) {
        const QJsonObject d = v.toObject();
        // GitHub Pages (parked, docs/dev/architecture.md) adds:
        //     && !(isGitHub(d) && !gitHubReady(d))
        if (!(isImgur(d) && imgurClientId(d).isEmpty()) && !(isVgyMe(d) && vgyMeUserKey(d).isEmpty()))
            ready << d.value(QStringLiteral("name")).toString();
    }
    return uploadChain(primary, destinationNames(), ready,
                       m_settings && m_settings->uploadFailureChain());
}

// One upload in flight, over one server or a chain of them. The payload rides
// along until it ends: `data` is the implicitly shared capture buffer (no
// copy), a recording stays on disk as `srcPath`.
struct UploadManager::Chain {
    QByteArray data;
    QString srcPath, fileName, mime;
    QStringList names;  // servers to try, in order (uploadChain())
    QStringList errors; // "name: error" per failed server; size() = next index
    Callback cb;
};

void UploadManager::startUpload(const QStringList &servers, const QByteArray &data,
                                const QString &srcPath, const QString &fileName,
                                const QString &mime, Callback cb)
{
    if (servers.isEmpty()) {
        cb({}, {}, tr("No upload server configured"));
        return;
    }
    // Hold busy across the gap between one server failing and the next one
    // starting, or the editor's Upload button would flicker back on.
    ++m_active;
    setBusy(true);
    runChain(std::make_shared<Chain>(Chain{data, srcPath, fileName, mime, servers, {}, std::move(cb)}));
}

void UploadManager::runChain(const std::shared_ptr<Chain> &c)
{
    const QString name = c->names.at(c->errors.size());
    auto done = [this, c, name](const QString &url, const QString &del, const QString &err) {
        if (!err.isEmpty()) {
            c->errors << tr("%1: %2").arg(name, err);
            if (c->errors.size() < c->names.size()) {
                // Names only: an error can echo the server's response body.
                qInfo() << "upload:" << name << "failed, trying" << c->names.at(c->errors.size());
                runChain(c); // a synchronous refusal recurses at most names.size() deep
                return;
            }
        }
        setBusy(--m_active > 0);
        if (!err.isEmpty()) {
            // A lone server's error reaches the caller untouched, as it always has.
            c->cb({}, {}, c->names.size() == 1 ? err : c->errors.join(QStringLiteral("; ")));
            return;
        }
        if (!c->errors.isEmpty()) {
            m_fallbackFrom = c->names.mid(0, c->errors.size()).join(QStringLiteral(", "));
            m_fallbackTo = name;
            // Why the server the user picked failed: the toast names it,
            // or a wrong repository/token on a personal host looks like
            // the app quietly choosing another server.
            m_fallbackReason = c->errors.first();
        }
        c->cb(url, del, {});
        m_fallbackFrom.clear();
        m_fallbackTo.clear();
        m_fallbackReason.clear();
    };
    // Deleted since the list was built: it fails its turn instead of being
    // swapped for another server.
    const QJsonObject dest = findDestination(name);
    if (dest.isEmpty())
        done({}, {}, tr("Upload server \"%1\" no longer exists").arg(name));
    else
        startUploadTo(dest, c->data, c->srcPath, c->fileName, c->mime, Purpose::Capture, done);
}

void UploadManager::startUploadTo(const QJsonObject &dest, const QByteArray &data,
                                  const QString &srcPath, const QString &fileName,
                                  const QString &mime, Purpose purpose, Callback cb)
{
    if (dest.isEmpty()) {
        cb({}, {}, tr("No upload server configured"));
        return;
    }
    // Imgur without a Client-ID is a guaranteed rejection (and the server answers
    // 429/403 with no hint of what is actually missing), so say what to do here
    // instead of spending the upload.
    if (isImgur(dest) && imgurClientId(dest).isEmpty()) {
        cb({}, {}, tr("Imgur needs your own Client-ID. Register a free application at "
                      "https://api.imgur.com/oauth2/addclient (\"Anonymous usage without "
                      "user authorisation\"), then paste the Client-ID into "
                      "Destinations → %1 → Edit.").arg(dest.value(QStringLiteral("name")).toString()));
        return;
    }
    if (isVgyMe(dest) && vgyMeUserKey(dest).isEmpty()) {
        cb({}, {}, tr("vgy.me requires a user key. Log in to "
                      "https://vgy.me/account/details#userkeys to find your key, "
                      "then paste it into Destinations → %1 → Edit.").arg(dest.value(QStringLiteral("name")).toString()));
        return;
    }
#if 0 // GitHub Pages: parked, see docs/dev/architecture.md
    if (isGitHub(dest) && !gitHubReady(dest)) {
        cb({}, {}, tr("GitHub Pages needs a repository and a token. Open Destinations → %1 → "
                      "Edit, enter the repository (owner/name) and paste a personal access "
                      "token that can write its contents.").arg(dest.value(QStringLiteral("name")).toString()));
        return;
    }
#endif
    const QString type = dest.value(QStringLiteral("type")).toString(QStringLiteral("http"));
    // `busy` means "a capture is on its way to a server": it greys the editor's
    // Upload button out and relabels it "Uploading…". A configuration test is
    // not that - it moves a few hundred bytes of generated PNG on a server the
    // user is still editing - so it must not disable an unrelated open editor.
    // The sheet shows its own "Testing…" state and never reads this flag.
    const bool counted = purpose == Purpose::Capture;
    if (counted) {
        ++m_active; // concurrent uploads: busy until the last one completes
        setBusy(true);
    }
    auto done = [this, cb, counted](const QString &url, const QString &del, const QString &err) {
        if (counted)
            setBusy(--m_active > 0);
        cb(url, del, err);
    };
    if (type == QLatin1String("curl"))
        curlUpload(dest, data, srcPath, fileName, mime, purpose, done);
    else
        httpUpload(dest, data, srcPath, fileName, mime, purpose, done);
}

void UploadManager::testDestination(const QVariantMap &destMap)
{
    // The sheet can outrun itself: close it on a pending test, reopen it on
    // another server, test again, and its own guard ("my sheet is open and
    // testing") is true for BOTH answers. The stale one would land first and be
    // painted as the new server's verdict, after which the real one is dropped
    // as "not testing". So each QML test takes a generation and only the newest
    // may speak; the superseded test is silently abandoned (its sheet is gone
    // by definition).
    const quint64 gen = ++m_qmlTestGen;
    testDestination(destMap, [this, gen](bool ok, const QString &url, const QString &error) {
        if (gen != m_qmlTestGen)
            return;
        emit testFinished(ok, url, error);
    });
}

void UploadManager::testDestination(const QVariantMap &destMap, TestCallback cb)
{
    const QJsonObject dest = QJsonObject::fromVariantMap(destMap);
    if (dest.value(QStringLiteral("requestUrl")).toString().trimmed().isEmpty()) {
        cb(false, {}, tr("Enter a request URL first."));
        return;
    }
    // Same rule as the real upload path, but worded for someone who is already
    // standing in the editor the long message would send them to.
    if (isImgur(dest) && imgurClientId(dest).isEmpty()) {
        cb(false, {}, tr("Imgur needs your own Client-ID. Paste it into the "
                         "field above, then test again."));
        return;
    }
    if (isVgyMe(dest) && vgyMeUserKey(dest).isEmpty()) {
        cb(false, {}, tr("vgy.me requires a user key. Paste it into the "
                         "field above, then test again."));
        return;
    }
#if 0 // GitHub Pages: parked, see docs/dev/architecture.md
    if (isGitHub(dest) && !gitHubReady(dest)) {
        cb(false, {}, tr("GitHub Pages needs a repository and a token. Fill in the "
                         "fields above, then test again."));
        return;
    }
#endif

    // A tiny recognisable tile: some hosts reject degenerate dimensions, and a
    // visible checker is friendlier than a blank pixel when the user opens the
    // returned link. A few hundred bytes, so a test costs almost nothing (and
    // Imgur's per-Client-ID daily cap is not something to spend carelessly).
    QImage img(64, 64, QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, ((x / 16 + y / 16) % 2) ? kTestTileSecondary : kTestTileAccent);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    buf.close();

    // Purpose::Test changes exactly two things (see the enum): it fails faster,
    // and it stays out of the busy state. Everything else - headers, verb, body
    // encoding, URL extraction - is deliberately the real upload path.
    startUploadTo(dest, png, {}, QStringLiteral("unisic-test.png"), QStringLiteral("image/png"),
                  Purpose::Test,
                  [cb = std::move(cb)](const QString &url, const QString &, const QString &err) {
        cb(err.isEmpty(), url, err);
    });
}

// The human-readable part of a JSON error body: vgy.me's "messages" (an object
// of field: text, or one string), else the usual "message" or "error" string.
// Empty when the body is not JSON or names none of them.
QString UploadManager::serverErrorMessage(const QByteArray &response)
{
    const QJsonObject obj = QJsonDocument::fromJson(response).object();
    const QJsonValue msgVal = obj.value(QStringLiteral("messages"));
    if (msgVal.isObject()) {
        QStringList parts;
        const QJsonObject msgObj = msgVal.toObject();
        for (auto it = msgObj.begin(); it != msgObj.end(); ++it) {
            parts << (it.value().isString() ? it.value().toString()
                                            : QStringLiteral("%1: %2").arg(it.key(), it.value().toVariant().toString()));
        }
        return parts.join(QStringLiteral(", "));
    }
    if (msgVal.isString())
        return msgVal.toString();
    if (obj.value(QStringLiteral("message")).isString())
        return obj.value(QStringLiteral("message")).toString();
    return obj.value(QStringLiteral("error")).toString();
}

// Resolve a single $text$/$json:...$/$regex:...$ token against the response.
// A string that is not a recognized token is returned verbatim.
QString UploadManager::extractToken(const QString &token, const QByteArray &response)
{
    if (token == QLatin1String("$text$"))
        return QString::fromUtf8(response).trimmed();

    static const QRegularExpression jsonSpec(QStringLiteral("^\\$json:(.+)\\$$"));
    static const QRegularExpression regexSpec(QStringLiteral("^\\$regex:(.+)\\$$"));

    if (auto m = jsonSpec.match(token); m.hasMatch()) {
        // Path syntax: a.b[0].c
        const QString path = m.captured(1);
        const QJsonDocument doc = QJsonDocument::fromJson(response);
        QJsonValue cur = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
        static const QRegularExpression tokenRe(QStringLiteral("([^.\\[\\]]+)|\\[(\\d+)\\]"));
        auto it = tokenRe.globalMatch(path);
        while (it.hasNext()) {
            const auto tok = it.next();
            if (!tok.captured(1).isEmpty())
                cur = cur.toObject().value(tok.captured(1));
            else
                cur = cur.toArray().at(tok.captured(2).toInt());
        }
        // Numeric/bool leaves (e.g. an id) stringify too, not just strings.
        return cur.isString() ? cur.toString() : cur.toVariant().toString();
    }
    if (auto m = regexSpec.match(token); m.hasMatch()) {
        const QRegularExpression re(m.captured(1));
        const auto match = re.match(QString::fromUtf8(response));
        if (match.hasMatch())
            return match.lastCapturedIndex() >= 1 ? match.captured(1) : match.captured(0);
        return {}; // no match must not report the raw spec as a "URL"
    }
    return token; // literal fallback
}

QString UploadManager::extractUrl(const QJsonObject &dest, const QString &key, const QByteArray &response)
{
    const QString spec = dest.value(key).toString();
    if (spec.isEmpty())
        return {};

    QString url;
    static const QRegularExpression tokenRe(
        QStringLiteral("\\$(?:text|json:[^$]+|regex:[^$]+)\\$"));
    // Whole-spec token: keeps exact legacy behaviour, incl. regex specs that
    // themselves contain '$' (which inline scanning could not handle). But the
    // anchored greedy match would also swallow a multi-token template like
    // "$json:a$/$json:b$" — two or more complete inline tokens means the spec
    // is a template, so route it to the inline branch instead.
    static const QRegularExpression wholeToken(QStringLiteral("^\\$(?:text|json:.+|regex:.+)\\$$"));
    int inlineTokens = 0;
    for (auto it = tokenRe.globalMatch(spec); it.hasNext() && inlineTokens < 2; it.next())
        ++inlineTokens;
    if (wholeToken.match(spec).hasMatch() && inlineTokens < 2) {
        url = extractToken(spec, response);
    } else {
        // Inline templating: replace each embedded token in place, leaving the
        // surrounding literal text untouched. No tokens -> spec returned as-is.
        int last = 0;
        auto it = tokenRe.globalMatch(spec);
        while (it.hasNext()) {
            const auto m = it.next();
            url += spec.mid(last, m.capturedStart() - last);
            url += extractToken(m.captured(0), response);
            last = m.capturedEnd();
        }
        url += spec.mid(last);
    }

    const QString find = dest.value(QStringLiteral("urlReplace")).toString();
    if (!find.isEmpty())
        url.replace(find, dest.value(QStringLiteral("urlReplaceWith")).toString());
    return url;
}

// Quotes and control characters in a filename would corrupt (or inject into)
// multipart headers and a JSON body template, where even a tab is invalid;
// path separators would change the remote path on curl targets.
static QString sanitizeFileName(QString name)
{
    name.remove(QLatin1Char('"'));
    name.removeIf([](QChar c) { return c.unicode() < 0x20 || c.unicode() == 0x7f; });
    name.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("upload.bin") : name;
}

void UploadManager::httpUpload(const QJsonObject &dest, const QByteArray &data,
                               const QString &srcPath, const QString &fileName,
                               const QString &mime, Purpose purpose, Callback cb)
{
    QNetworkRequest req{QUrl(requestUrlWithFileName(
        dest.value(QStringLiteral("requestUrl")).toString(), fileName))};
    // Stall protection, the http twin of curlUpload's --speed-time 60: a server
    // that accepts the connection and then goes quiet would otherwise hold the
    // QNetworkReply, the busy state and (for the server editor's Test upload)
    // a sheet stuck on "Testing…" forever, with nothing to cancel it. A stall
    // ends as OperationCanceledError in the normal finished() cleanup path.
    // Both values are stated here rather than letting the capture case fall
    // through to the manager-wide default: a request-level setTransferTimeout()
    // OVERRIDES the manager's, so one unqualified call would silently retime
    // every capture upload too.
    req.setTransferTimeout(purpose == Purpose::Test ? kTestStallMs : kCaptureStallMs);
    const QJsonObject headers = dest.value(QStringLiteral("headers")).toObject();
    bool hasContentType = false;
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        // An imported destinations file must not be able to inject headers.
        QByteArray key = it.key().toUtf8();
        QByteArray val = it.value().toString().toUtf8();
        key.replace('\r', "").replace('\n', "");
        val.replace('\r', "").replace('\n', "");
        req.setRawHeader(key, val);
        if (it.key().compare(QLatin1String("Content-Type"), Qt::CaseInsensitive) == 0)
            hasContentType = true;
    }

    QString method = dest.value(QStringLiteral("method")).toString(QStringLiteral("POST")).toUpper();
    // Strict HTTP-token check: the verb comes from an imported destinations
    // file and goes RAW into the request line via sendCustomRequest — CR/LF
    // or spaces there are request-line injection (the header sanitization
    // below would be pointless with an unchecked verb).
    static const QRegularExpression verbRe(QStringLiteral("^[A-Z]{1,16}$"));
    if (!verbRe.match(method).hasMatch())
        method = QStringLiteral("POST");
    const QString bodyType = dest.value(QStringLiteral("body")).toString().toLower();
    QNetworkReply *reply = nullptr;

    if (bodyType == QLatin1String("json")) {
        // Raw JSON body from the user's template. $base64$/$filename$/$mime$ are
        // substituted; no multipart file part is sent. $base64$ requires the
        // bytes in memory even for a file source.
        QByteArray bytes = data;
        if (bytes.isEmpty() && !srcPath.isEmpty()) {
            QFile f(srcPath);
            if (!f.open(QIODevice::ReadOnly)) {
                cb({}, {}, tr("Cannot read %1").arg(srcPath));
                return;
            }
            bytes = f.readAll();
        }
        // Assemble the payload as QByteArray: the old QString::replace of a
        // multi-MB base64 blob peaked at ~8x the payload in live allocations
        // (UTF-16 copies of the blob + the template). Split on $base64$ first,
        // substitute the small tokens per part (same order as before), then
        // join with the raw base64 — byte-identical output, ~2x peak.
        const QByteArray b64 = bytes.toBase64();
        const QStringList parts = dest.value(QStringLiteral("data")).toString()
                                      .split(QLatin1String("$base64$"));
        QByteArray payload;
        for (int i = 0; i < parts.size(); ++i) {
            QString part = parts[i];
            part.replace(QLatin1String("$filename$"), sanitizeFileName(fileName));
            part.replace(QLatin1String("$mime$"), mime);
            if (i > 0)
                payload += b64;
            payload += part.toUtf8();
        }
        if (!hasContentType)
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        // Honor the stored verb (imported .sxcu may use PATCH/GET/DELETE),
        // not just PUT-vs-POST.
        reply = (method == QLatin1String("PUT"))  ? m_nam->put(req, payload)
              : (method == QLatin1String("POST")) ? m_nam->post(req, payload)
              : m_nam->sendCustomRequest(req, method.toUtf8(), payload);
    } else {
        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        const QJsonObject args = dest.value(QStringLiteral("arguments")).toObject();
        for (auto it = args.begin(); it != args.end(); ++it) {
            QString key = it.key();
            if (isVgyMe(dest) && key.compare(QLatin1String("userkey"), Qt::CaseInsensitive) == 0)
                key = QStringLiteral("userkey");
            QHttpPart part;
            part.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"%1\"").arg(sanitizeFileName(key)));
            part.setBody(it.value().toString().toUtf8());
            multi->append(part);
        }
        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader, mime);
        filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"")
                               .arg(sanitizeFileName(dest.value(QStringLiteral("fileFormName"))
                                                         .toString(QStringLiteral("file"))),
                                    sanitizeFileName(fileName)));
        if (!srcPath.isEmpty()) {
            // Stream from disk: QHttpMultiPart reads the device on demand, so
            // a multi-hundred-MB recording never sits in RAM.
            auto *file = new QFile(srcPath);
            if (!file->open(QIODevice::ReadOnly)) {
                delete file;
                delete multi;
                cb({}, {}, tr("Cannot read %1").arg(srcPath));
                return;
            }
            file->setParent(multi); // multi is parented to the reply below
            filePart.setBodyDevice(file);
        } else {
            filePart.setBody(data);
        }
        multi->append(filePart);

        reply = (method == QLatin1String("PUT"))  ? m_nam->put(req, multi)
              : (method == QLatin1String("POST")) ? m_nam->post(req, multi)
              : m_nam->sendCustomRequest(req, method.toUtf8(), multi);
        multi->setParent(reply);
    }

    connect(reply, &QNetworkReply::finished, this, [dest, reply, cb]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString serverMsg = serverErrorMessage(body);
            if (serverMsg.isEmpty())
                serverMsg = QString::fromUtf8(body.left(300));
#if 0 // GitHub Pages: parked, see docs/dev/architecture.md
            // The Contents API's answers name the symptom, not the cause a
            // Pages user can act on: 422 is almost always "that file name is
            // already in the repository" (it wants the old blob's sha to
            // overwrite), 404 is a wrong repository or a token without access.
            if (isGitHub(dest)) {
                const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString hint;
                if (status == 401)
                    hint = tr("GitHub did not accept the token (%1)").arg(serverMsg);
                else if (status == 404)
                    hint = tr("GitHub found no such repository, or the token cannot write to it (%1)").arg(serverMsg);
                else if (status == 409)
                    hint = tr("GitHub reports a conflict; check that the branch exists (%1)").arg(serverMsg);
                else if (status == 422)
                    hint = tr("GitHub rejected the commit; a file with this name is probably already in the repository (%1)").arg(serverMsg);
                // The hint is the whole message: Qt's "Error transferring
                // <url> - server replied: Not Found" in front of it only
                // pushes the part the user can act on out of a toast.
                if (!hint.isEmpty()) {
                    cb({}, {}, hint);
                    return;
                }
            }
#endif
            cb({}, {}, QStringLiteral("%1: %2").arg(reply->errorString(), serverMsg));
            return;
        }
        const QString url = extractUrl(dest, QStringLiteral("urlPath"), body);
        const QString del = extractUrl(dest, QStringLiteral("deletionUrlPath"), body);
        if (url.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject() && doc.object().value(QStringLiteral("error")).toBool()) {
                const QString serverMsg = serverErrorMessage(body);
                if (!serverMsg.isEmpty()) {
                    cb({}, {}, UploadManager::tr("Server reported error: %1").arg(serverMsg));
                    return;
                }
            }
            cb({}, {}, QStringLiteral("Upload succeeded but no URL found in response: %1")
                           .arg(QString::fromUtf8(body.left(300))));
            return;
        }
        cb(url, del, {});
    });
}

// %file% in a request URL, replaced by the percent-encoded name. Shared by both
// senders: it is the same field with the same job whichever way the destination
// uploads, so it takes the same token, and a destination switched from curl to
// http does not quietly stop substituting. Absent the token the URL is returned
// untouched - the append below is curl's own rule, not this one, and appending
// a name to an http endpoint would post to a path that does not exist.
QString UploadManager::requestUrlWithFileName(const QString &requestUrl, const QString &fileName)
{
    if (!requestUrl.contains(QLatin1String("%file%")))
        return requestUrl;
    const QString encoded =
        QString::fromUtf8(QUrl::toPercentEncoding(sanitizeFileName(fileName)));
    return QString(requestUrl).replace(QLatin1String("%file%"), encoded);
}

// Where curl puts the file. %file% anywhere in the request URL is replaced by
// the percent-encoded name: hosts that take the name as a path segment of their
// own ("/upload/%file%?to=inbox") or as a query value could not be addressed at
// all while the name was only ever appended. Without the token the old append
// stands, which is what every stored FTP/SFTP destination relies on.
QString UploadManager::curlTargetUrl(const QString &requestUrl, const QString &fileName)
{
    const QString encoded =
        QString::fromUtf8(QUrl::toPercentEncoding(sanitizeFileName(fileName)));
    if (requestUrl.contains(QLatin1String("%file%")))
        return requestUrlWithFileName(requestUrl, fileName);
    QString target = requestUrl;
    if (!target.endsWith(QLatin1Char('/')))
        target += QLatin1Char('/');
    return target + encoded;
}

// One line of a curl -K config file. curl parses it line by line, so an embedded
// newline in a value would inject arbitrary options ("insecure", "output
// <file>"); quotes and backslashes have to survive as themselves.
static QByteArray curlConfigLine(const QString &option, const QString &value)
{
    QString v = value;
    v.remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));
    v.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
     .replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QStringLiteral("%1 = \"%2\"\n").arg(option, v).toUtf8();
}

// S3 answers a refused request with <Error><Code>..</Code><Message>..</Message>;
// "403" alone cannot tell a wrong secret from a wrong bucket or region.
static QString s3ErrorOf(const QByteArray &body)
{
    static const QRegularExpression code(QStringLiteral("<Code>([^<]*)</Code>"));
    static const QRegularExpression message(QStringLiteral("<Message>([^<]*)</Message>"));
    const QString text = QString::fromUtf8(body.left(4096));
    const QString c = code.match(text).captured(1);
    const QString m = message.match(text).captured(1);
    return c.isEmpty() ? m : (m.isEmpty() ? c : c + QStringLiteral(": ") + m);
}

void UploadManager::curlUpload(const QJsonObject &dest, const QByteArray &data,
                               const QString &srcPath, const QString &fileName,
                               const QString &mime, Purpose purpose, Callback cb)
{
    // File source: hand curl the original path directly — copying a recording
    // into a QTemporaryFile doubled both the RAM (readAll upstream) and disk.
    QTemporaryFile *tmp = nullptr;
    QString uploadPath = srcPath;
    if (srcPath.isEmpty()) {
        tmp = new QTemporaryFile(this);
        if (!tmp->open()) {
            delete tmp;
            cb({}, {}, tr("Cannot create temp file"));
            return;
        }
        // A short write or failed flush (disk full) would upload a truncated or
        // empty file yet still return a "success" URL — bail before curl runs.
        if (tmp->write(data) != data.size() || !tmp->flush()) {
            delete tmp;
            cb({}, {}, tr("Cannot write temp file"));
            return;
        }
        uploadPath = tmp->fileName();
    }

    const QString safeName = sanitizeFileName(fileName);
    const QString target =
        curlTargetUrl(dest.value(QStringLiteral("requestUrl")).toString(), fileName);

    // Stall protection: without it a server that accepts the connection and
    // then hangs leaks the curl process, temp file and busy state forever.
    // --speed-* aborts only below 1 byte/s for 60 s — progressing uploads of
    // any length are unaffected; a stall exits non-zero into the normal
    // finished cleanup path.
    // --proto: the destination is user-editable and importable (.sxcu,
    // destinations.json), and curl would otherwise happily "upload" to
    // file:///... and write a local file. -g: the file name is always
    // percent-encoded, but a '[' or '{' the user typed into the folder part of
    // requestUrl is a URL glob to curl and fails with "bad range in URL position".
    // S3 and the stores that speak its API (R2, B2, MinIO, Wasabi): curl signs
    // the PUT with AWS Signature Version 4 itself (curl 7.75+), and `user` is
    // then access-key:secret-key.
    const QString s3Region = dest.value(QStringLiteral("s3Region")).toString().trimmed();
    const bool s3 = !s3Region.isEmpty();
    // --fail-with-body keeps S3's error document, which names the actual fault.
    QStringList args{QStringLiteral("-sS"), s3 ? QStringLiteral("--fail-with-body") : QStringLiteral("--fail"),
                     QStringLiteral("--proto"), QStringLiteral("=ftp,ftps,sftp,http,https"),
                     QStringLiteral("-g"),
                     QStringLiteral("--connect-timeout"), QStringLiteral("30"),
                     QStringLiteral("--speed-time"), QStringLiteral("60"),
                     QStringLiteral("--speed-limit"), QStringLiteral("1"),
                     QStringLiteral("-T"), uploadPath};
    // Credentials must never be on the command line — argv is world-readable
    // in /proc/<pid>/cmdline for the whole transfer. Feed them as a config
    // file on stdin instead (curl -K -). Custom headers ride the same channel
    // for the same reason: an API key in an Authorization header is every bit
    // as much a secret as a password. So does the target URL: requestUrl can
    // carry the key itself ("?key=...").
    QByteArray curlConfig = curlConfigLine(QStringLiteral("url"), target);
    const QString user = dest.value(QStringLiteral("user")).toString();
    if (!user.isEmpty())
        curlConfig += curlConfigLine(QStringLiteral("user"), user);
    const QJsonObject headers = dest.value(QStringLiteral("headers")).toObject();
    bool hasContentType = false;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        curlConfig += curlConfigLine(QStringLiteral("header"),
                                     it.key() + QStringLiteral(": ") + it.value().toString());
        hasContentType = hasContentType
                || it.key().compare(QLatin1String("Content-Type"), Qt::CaseInsensitive) == 0;
    }
    if (s3) {
        curlConfig += curlConfigLine(QStringLiteral("aws-sigv4"),
                                     QStringLiteral("aws:amz:%1:s3").arg(s3Region));
        // Without it the object is stored as binary/octet-stream and a browser
        // downloads the link instead of showing the picture.
        if (!hasContentType && !mime.isEmpty())
            curlConfig += curlConfigLine(QStringLiteral("header"),
                                         QStringLiteral("Content-Type: ") + mime);
    }
    args << QStringLiteral("-K") << QStringLiteral("-");
    // Skipping host-key verification enables silent MITM; only on explicit
    // per-destination opt-in ("insecure": true) for curl builds whose sftp
    // backend can't read known_hosts.
    if (target.startsWith(QLatin1String("sftp://"))
        && dest.value(QStringLiteral("insecure")).toBool())
        args << QStringLiteral("--insecure");

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [proc, tmp, dest, safeName, s3, target, cb](int code, QProcess::ExitStatus) {
        const QString errOut = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        // curl's stdout is the server's answer. It used to be dropped on the
        // floor, which pinned every curl destination to publicUrlBase + name:
        // a host that replies with the link, or with an id to build one from,
        // could not be used at all.
        const QByteArray body = proc->readAllStandardOutput();
        proc->deleteLater();
        if (tmp)
            tmp->deleteLater();
        if (code != 0) {
            QString err = errOut.isEmpty() ? QStringLiteral("curl exited with code %1").arg(code) : errOut;
            if (const QString s3Err = s3 ? s3ErrorOf(body) : QString(); !s3Err.isEmpty())
                err += QStringLiteral(" (") + s3Err + QLatin1Char(')');
            cb({}, {}, err);
            return;
        }
        // An explicit extractor wins: it is the one the user typed.
        if (!dest.value(QStringLiteral("urlPath")).toString().isEmpty()) {
            const QString url = extractUrl(dest, QStringLiteral("urlPath"), body);
            const QString del = extractUrl(dest, QStringLiteral("deletionUrlPath"), body);
            if (url.isEmpty()) {
                cb({}, {}, QStringLiteral("Upload succeeded but no URL found in response: %1")
                               .arg(QString::fromUtf8(body.left(300))));
                return;
            }
            cb(url, del, {});
            return;
        }
        QString publicUrl = dest.value(QStringLiteral("publicUrlBase")).toString();
        if (!publicUrl.isEmpty()) {
            if (!publicUrl.endsWith(QLatin1Char('/')))
                publicUrl += QLatin1Char('/');
            publicUrl += QString::fromUtf8(QUrl::toPercentEncoding(safeName));
        } else if (s3) {
            // The object's own address: the link, for a bucket that is public.
            publicUrl = target;
        }
        cb(publicUrl, {}, {});
    });
    // FailedToStart (curl missing/unlaunchable) never emits finished(): without
    // this the QProcess/temp file would leak and the wrapping done() lambda would
    // never clear m_busy, wedging the UI in a permanent "uploading" state.
    connect(proc, &QProcess::errorOccurred, this,
            [proc, tmp, cb](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return; // other errors still deliver finished(); let that path handle them
        proc->deleteLater();
        if (tmp)
            tmp->deleteLater();
        cb({}, {}, tr("Could not run curl. Is it installed? (needed for FTP/SFTP uploads)"));
    });
    proc->start(QStringLiteral("curl"), args);
    // Absolute watchdog on top of --speed-*: some curl builds don't apply the
    // low-speed check while stuck in non-transfer protocol states (e.g. an SFTP
    // handshake). Parented to proc, so it auto-cancels on normal completion;
    // kill() delivers finished() and the standard cleanup path runs. The two
    // ceilings are up at the constants block with the http timeouts.
    QTimer::singleShot(purpose == Purpose::Test ? kTestCurlCeilingMs : kCaptureCurlCeilingMs,
                       proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->write(curlConfig);
    proc->closeWriteChannel();
}

static bool isTinyUrl(const QString &service)
{
    return service == QLatin1String("tinyurl");
}

QString UploadManager::shortenerName(const QString &service)
{
    return isTinyUrl(service) ? QStringLiteral("TinyURL") : QStringLiteral("is.gd");
}

QString UploadManager::parseShortenReply(const QString &service, int httpStatus, const QByteArray &body,
                                         QString *error)
{
    const QString text = QString::fromUtf8(body.left(512)).trimmed();
    // StrictMode refuses whitespace, so a body of several lines or a page of
    // HTML can never pass for a link.
    const QUrl link(text, QUrl::StrictMode);
    const QLatin1String host = isTinyUrl(service) ? QLatin1String("tinyurl.com") : QLatin1String("is.gd");
    if (httpStatus == 200 && link.isValid() && link.scheme() == QLatin1String("https")
        && link.host() == host && link.path().size() > 1)
        return text;
    if (error)
        *error = text.startsWith(QLatin1String("Error: ")) ? text.mid(7).left(200) : QString();
    return {};
}

void UploadManager::shortenUrl(const QString &service, const QString &url,
                               std::function<void(const QString &, const QString &)> done)
{
    QUrl api;
    QUrlQuery query;
    if (isTinyUrl(service)) {
        // Not in TinyURL's documented API, which needs an account token; this
        // keyless form answers with the bare link, or "Error" and a 400.
        api.setUrl(QStringLiteral("https://tinyurl.com/api-create.php"));
    } else {
        api.setUrl(QStringLiteral("https://is.gd/create.php"));
        query.addQueryItem(QStringLiteral("format"), QStringLiteral("simple"));
    }
    query.addQueryItem(QStringLiteral("url"), QString::fromLatin1(QUrl::toPercentEncoding(url)));
    api.setQuery(query);
    QNetworkRequest req(api);
    // A short link is a nicety: waiting longer than this for one only delays
    // the full link the user could already have.
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam->get(req);
    // Both answer with one line; anything this big is not that answer.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > 4096)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, service, done = std::move(done)] {
        reply->deleteLater();
        QString error;
        const QString shortUrl = parseShortenReply(
            service, reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll(), &error);
        if (shortUrl.isEmpty() && error.isEmpty())
            error = reply->errorString();
        done(shortUrl, error);
    });
}

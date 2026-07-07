#include "UploadManager.h"
#include "Settings.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QTemporaryFile>
#include <QMimeDatabase>
#include <QDebug>

// Imgur's anonymous image endpoint needs an "Authorization: Client-ID <id>"
// header. Register a free application at https://api.imgur.com/oauth2/addclient
// ("Anonymous usage without user authorisation") and drop the ID here — or edit
// the "Imgur (anonymous)" destination in Settings to use your own. A shared ID
// hits Imgur's per-app daily cap (~1250 uploads) across all users.
static const char kImgurClientId[] = "REPLACE_WITH_YOUR_IMGUR_CLIENT_ID";

UploadManager::UploadManager(Settings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    loadDestinations();
    ensureBuiltins();
}

QString UploadManager::configPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/unisic";
    QDir().mkpath(dir);
    return dir + "/destinations.json";
}

void UploadManager::loadDestinations()
{
    QFile f(configPath());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isArray())
            m_destinations = doc.array();
    }
}

void UploadManager::persistDestinations()
{
    QFile f(configPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_destinations).toJson(QJsonDocument::Indented));
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
    if (!has(QStringLiteral("0x0.st"))) {
        m_destinations.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("0x0.st")},
            {QStringLiteral("type"), QStringLiteral("http")},
            {QStringLiteral("requestUrl"), QStringLiteral("https://0x0.st")},
            {QStringLiteral("method"), QStringLiteral("POST")},
            {QStringLiteral("fileFormName"), QStringLiteral("file")},
            {QStringLiteral("responseType"), QStringLiteral("text")},
            {QStringLiteral("urlPath"), QStringLiteral("$text$")},
            {QStringLiteral("headers"), QJsonObject{{QStringLiteral("User-Agent"),
                QStringLiteral("Unisic/" UNISIC_VERSION " (screenshot tool)")}}},
            {QStringLiteral("builtin"), true},
        });
        changed = true;
    }
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
            {QStringLiteral("headers"), QJsonObject{{QStringLiteral("Authorization"),
                QStringLiteral("Client-ID ") + QLatin1String(kImgurClientId)}}},
            {QStringLiteral("builtin"), true},
        });
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
    if (changed)
        persistDestinations();
}

QVariantList UploadManager::destinationsVariant() const
{
    return m_destinations.toVariantList();
}

QVariantMap UploadManager::destination(const QString &name) const
{
    for (const QJsonValue &v : std::as_const(m_destinations))
        if (v.toObject().value(QStringLiteral("name")).toString() == name)
            return v.toObject().toVariantMap();
    return {};
}

void UploadManager::saveDestination(const QVariantMap &dest)
{
    const QString name = dest.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) return;
    QJsonObject obj = QJsonObject::fromVariantMap(dest);
    for (int i = 0; i < m_destinations.size(); ++i) {
        if (m_destinations[i].toObject().value(QStringLiteral("name")).toString() == name) {
            m_destinations[i] = obj;
            persistDestinations();
            return;
        }
    }
    m_destinations.append(obj);
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
    ensureBuiltins(); // guarantees the defaults survive a foreign import
    persistDestinations();
}

QJsonObject UploadManager::activeDestination() const
{
    const QString name = m_settings->activeDestination();
    for (const QJsonValue &v : std::as_const(m_destinations))
        if (v.toObject().value(QStringLiteral("name")).toString() == name)
            return v.toObject();
    return m_destinations.isEmpty() ? QJsonObject{} : m_destinations.first().toObject();
}

void UploadManager::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged();
}

void UploadManager::uploadFile(const QString &filePath, Callback cb)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        cb({}, {}, tr("Cannot read %1").arg(filePath));
        return;
    }
    const QByteArray data = f.readAll();
    const QString mime = QMimeDatabase().mimeTypeForFile(filePath).name();
    uploadData(data, QFileInfo(filePath).fileName(), mime, std::move(cb));
}

void UploadManager::uploadData(const QByteArray &data, const QString &fileName,
                               const QString &mime, Callback cb)
{
    const QJsonObject dest = activeDestination();
    if (dest.isEmpty()) {
        cb({}, {}, tr("No upload destination configured"));
        return;
    }
    const QString type = dest.value(QStringLiteral("type")).toString(QStringLiteral("http"));
    ++m_active; // concurrent uploads: busy until the last one completes
    setBusy(true);
    auto done = [this, cb](const QString &url, const QString &del, const QString &err) {
        setBusy(--m_active > 0);
        cb(url, del, err);
    };
    if (type == QLatin1String("curl"))
        curlUpload(dest, data, fileName, done);
    else
        httpUpload(dest, data, fileName, mime, done);
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
    // Whole-spec token: keeps exact legacy behaviour, incl. regex specs that
    // themselves contain '$' (which inline scanning could not handle).
    static const QRegularExpression wholeToken(QStringLiteral("^\\$(?:text|json:.+|regex:.+)\\$$"));
    if (wholeToken.match(spec).hasMatch()) {
        url = extractToken(spec, response);
    } else {
        // Inline templating: replace each embedded token in place, leaving the
        // surrounding literal text untouched. No tokens -> spec returned as-is.
        static const QRegularExpression tokenRe(
            QStringLiteral("\\$(?:text|json:[^$]+|regex:[^$]+)\\$"));
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

// Quotes and CR/LF in a filename would corrupt (or inject into) multipart
// headers; path separators would change the remote path on curl targets.
static QString sanitizeFileName(QString name)
{
    name.remove(QLatin1Char('"')).remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));
    name.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("upload.bin") : name;
}

void UploadManager::httpUpload(const QJsonObject &dest, const QByteArray &data,
                               const QString &fileName, const QString &mime, Callback cb)
{
    QNetworkRequest req{QUrl(dest.value(QStringLiteral("requestUrl")).toString())};
    const QJsonObject headers = dest.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        // An imported destinations file must not be able to inject headers.
        QByteArray key = it.key().toUtf8();
        QByteArray val = it.value().toString().toUtf8();
        key.replace('\r', "").replace('\n', "");
        val.replace('\r', "").replace('\n', "");
        req.setRawHeader(key, val);
    }

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const QJsonObject args = dest.value(QStringLiteral("arguments")).toObject();
    for (auto it = args.begin(); it != args.end(); ++it) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(it.key()));
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
    filePart.setBody(data);
    multi->append(filePart);

    const QString method = dest.value(QStringLiteral("method")).toString(QStringLiteral("POST")).toUpper();
    QNetworkReply *reply = (method == QLatin1String("PUT")) ? m_nam->put(req, multi)
                                                            : m_nam->post(req, multi);
    multi->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [dest, reply, cb]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            cb({}, {}, QStringLiteral("%1 — %2").arg(reply->errorString(),
                                                     QString::fromUtf8(body.left(300))));
            return;
        }
        const QString url = extractUrl(dest, QStringLiteral("urlPath"), body);
        const QString del = extractUrl(dest, QStringLiteral("deletionUrlPath"), body);
        if (url.isEmpty()) {
            cb({}, {}, QStringLiteral("Upload succeeded but no URL found in response: %1")
                           .arg(QString::fromUtf8(body.left(300))));
            return;
        }
        cb(url, del, {});
    });
}

void UploadManager::curlUpload(const QJsonObject &dest, const QByteArray &data,
                               const QString &fileName, Callback cb)
{
    auto *tmp = new QTemporaryFile(this);
    if (!tmp->open()) {
        delete tmp;
        cb({}, {}, tr("Cannot create temp file"));
        return;
    }
    tmp->write(data);
    tmp->flush();

    const QString safeName = sanitizeFileName(fileName);
    QString target = dest.value(QStringLiteral("requestUrl")).toString();
    if (!target.endsWith(QLatin1Char('/')))
        target += QLatin1Char('/');
    target += QString::fromUtf8(QUrl::toPercentEncoding(safeName));

    QStringList args{QStringLiteral("-sS"), QStringLiteral("--fail"),
                     QStringLiteral("-T"), tmp->fileName(), target};
    // Credentials must never be on the command line — argv is world-readable
    // in /proc/<pid>/cmdline for the whole transfer. Feed them as a config
    // file on stdin instead (curl -K -).
    const QString user = dest.value(QStringLiteral("user")).toString();
    QByteArray curlConfig;
    if (!user.isEmpty()) {
        QString escaped = user;
        // curl parses the config line-by-line: an embedded newline would
        // inject arbitrary options (e.g. "insecure", "output <file>").
        escaped.remove(QLatin1Char('\r')).remove(QLatin1Char('\n'));
        escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
               .replace(QLatin1Char('"'), QLatin1String("\\\""));
        curlConfig = QStringLiteral("user = \"%1\"\n").arg(escaped).toUtf8();
        args << QStringLiteral("-K") << QStringLiteral("-");
    }
    // Skipping host-key verification enables silent MITM; only on explicit
    // per-destination opt-in ("insecure": true) for curl builds whose sftp
    // backend can't read known_hosts.
    if (target.startsWith(QLatin1String("sftp://"))
        && dest.value(QStringLiteral("insecure")).toBool())
        args << QStringLiteral("--insecure");

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [proc, tmp, dest, safeName, cb](int code, QProcess::ExitStatus) {
        const QString errOut = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        tmp->deleteLater();
        if (code != 0) {
            cb({}, {}, errOut.isEmpty() ? QStringLiteral("curl exited with code %1").arg(code) : errOut);
            return;
        }
        QString publicUrl = dest.value(QStringLiteral("publicUrlBase")).toString();
        if (!publicUrl.isEmpty()) {
            if (!publicUrl.endsWith(QLatin1Char('/')))
                publicUrl += QLatin1Char('/');
            publicUrl += QString::fromUtf8(QUrl::toPercentEncoding(safeName));
        }
        cb(publicUrl, {}, {});
    });
    // Without this, a missing curl binary means finished() never fires:
    // the callback is lost and busy stays true forever.
    connect(proc, &QProcess::errorOccurred, this,
            [proc, tmp, cb](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        tmp->deleteLater();
        cb({}, {}, tr("curl could not be started — is it installed?"));
    });
    proc->start(QStringLiteral("curl"), args);
    if (!curlConfig.isEmpty())
        proc->write(curlConfig);
    proc->closeWriteChannel();
}

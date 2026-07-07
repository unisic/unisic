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
            {QStringLiteral("headers"), QJsonObject{{QStringLiteral("User-Agent"), QStringLiteral("Unisic/0.1 (screenshot tool)")}}},
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
    setBusy(true);
    auto done = [this, cb](const QString &url, const QString &del, const QString &err) {
        setBusy(false);
        cb(url, del, err);
    };
    if (type == QLatin1String("curl"))
        curlUpload(dest, data, fileName, done);
    else
        httpUpload(dest, data, fileName, mime, done);
}

QString UploadManager::extractUrl(const QJsonObject &dest, const QString &key, const QByteArray &response)
{
    const QString spec = dest.value(key).toString();
    if (spec.isEmpty())
        return {};
    if (spec == QLatin1String("$text$"))
        return QString::fromUtf8(response).trimmed();

    static const QRegularExpression jsonSpec(QStringLiteral("^\\$json:(.+)\\$$"));
    static const QRegularExpression regexSpec(QStringLiteral("^\\$regex:(.+)\\$$"));

    if (auto m = jsonSpec.match(spec); m.hasMatch()) {
        // Path syntax: a.b[0].c
        const QString path = m.captured(1);
        QJsonValue cur = QJsonDocument::fromJson(response).object();
        static const QRegularExpression tokenRe(QStringLiteral("([^.\\[\\]]+)|\\[(\\d+)\\]"));
        auto it = tokenRe.globalMatch(path);
        while (it.hasNext()) {
            const auto tok = it.next();
            if (!tok.captured(1).isEmpty())
                cur = cur.toObject().value(tok.captured(1));
            else
                cur = cur.toArray().at(tok.captured(2).toInt());
        }
        return cur.toString();
    }
    if (auto m = regexSpec.match(spec); m.hasMatch()) {
        const QRegularExpression re(m.captured(1));
        const auto match = re.match(QString::fromUtf8(response));
        if (match.hasMatch())
            return match.lastCapturedIndex() >= 1 ? match.captured(1) : match.captured(0);
    }
    return spec; // literal template fallback
}

void UploadManager::httpUpload(const QJsonObject &dest, const QByteArray &data,
                               const QString &fileName, const QString &mime, Callback cb)
{
    QNetworkRequest req{QUrl(dest.value(QStringLiteral("requestUrl")).toString())};
    const QJsonObject headers = dest.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

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
                           .arg(dest.value(QStringLiteral("fileFormName")).toString(QStringLiteral("file")), fileName));
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

    QString target = dest.value(QStringLiteral("requestUrl")).toString();
    if (!target.endsWith(QLatin1Char('/')))
        target += QLatin1Char('/');
    target += fileName;

    QStringList args{QStringLiteral("-sS"), QStringLiteral("--fail"),
                     QStringLiteral("-T"), tmp->fileName(), target};
    const QString user = dest.value(QStringLiteral("user")).toString();
    if (!user.isEmpty())
        args << QStringLiteral("-u") << user;
    if (target.startsWith(QLatin1String("sftp://")))
        args << QStringLiteral("--insecure"); // host key via known_hosts is curl-build dependent

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [proc, tmp, dest, fileName, cb](int code, QProcess::ExitStatus) {
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
            publicUrl += fileName;
        }
        cb(publicUrl, {}, {});
    });
    proc->start(QStringLiteral("curl"), args);
}

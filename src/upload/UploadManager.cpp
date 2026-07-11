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
    // Inactivity timeout (restarts whenever bytes flow, so slow-but-progressing
    // transfers are unaffected). Without it a server that accepts the connection
    // and stalls pins the reply + the full multipart payload forever and wedges
    // the busy state. Surfaces as OperationCanceledError through the existing
    // finished handler. (int overload: the chrono one needs Qt 6.7.)
    m_nam->setTransferTimeout(120000);
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
        f.commit();
    }
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
    if (!params.isEmpty()) {
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
    {
        // Early readability check only — the payload itself is streamed from
        // disk (a saved recording can be hundreds of MB; readAll() used to pin
        // it in RAM for the whole transfer, twice on the curl path).
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            cb({}, {}, tr("Cannot read %1").arg(filePath));
            return;
        }
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(filePath).name();
    startUpload({}, filePath, QFileInfo(filePath).fileName(), mime, std::move(cb));
}

void UploadManager::uploadData(const QByteArray &data, const QString &fileName,
                               const QString &mime, Callback cb)
{
    startUpload(data, {}, fileName, mime, std::move(cb));
}

void UploadManager::startUpload(const QByteArray &data, const QString &srcPath,
                                const QString &fileName, const QString &mime, Callback cb)
{
    const QJsonObject dest = activeDestination();
    if (dest.isEmpty()) {
        cb({}, {}, tr("No upload server configured"));
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
        curlUpload(dest, data, srcPath, fileName, done);
    else
        httpUpload(dest, data, srcPath, fileName, mime, done);
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
                               const QString &srcPath, const QString &fileName,
                               const QString &mime, Callback cb)
{
    QNetworkRequest req{QUrl(dest.value(QStringLiteral("requestUrl")).toString())};
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
            part.replace(QLatin1String("$filename$"), fileName);
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
            cb({}, {}, QStringLiteral("%1: %2").arg(reply->errorString(),
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
                               const QString &srcPath, const QString &fileName, Callback cb)
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
        tmp->write(data);
        tmp->flush();
        uploadPath = tmp->fileName();
    }

    const QString safeName = sanitizeFileName(fileName);
    QString target = dest.value(QStringLiteral("requestUrl")).toString();
    if (!target.endsWith(QLatin1Char('/')))
        target += QLatin1Char('/');
    target += QString::fromUtf8(QUrl::toPercentEncoding(safeName));

    // Stall protection: without it a server that accepts the connection and
    // then hangs leaks the curl process, temp file and busy state forever.
    // --speed-* aborts only below 1 byte/s for 60 s — progressing uploads of
    // any length are unaffected; a stall exits non-zero into the normal
    // finished cleanup path.
    QStringList args{QStringLiteral("-sS"), QStringLiteral("--fail"),
                     QStringLiteral("--connect-timeout"), QStringLiteral("30"),
                     QStringLiteral("--speed-time"), QStringLiteral("60"),
                     QStringLiteral("--speed-limit"), QStringLiteral("1"),
                     QStringLiteral("-T"), uploadPath};
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
    // The URL goes last, after end-of-options: a destination-controlled
    // requestUrl starting with '-' must never be parsed as curl options
    // (e.g. "-K<file>" reads an arbitrary config file).
    args << QStringLiteral("--") << target;

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [proc, tmp, dest, safeName, cb](int code, QProcess::ExitStatus) {
        const QString errOut = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();
        if (tmp)
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
    // kill() delivers finished() and the standard cleanup path runs.
    QTimer::singleShot(30 * 60 * 1000, proc, [proc] {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    if (!curlConfig.isEmpty())
        proc->write(curlConfig);
    proc->closeWriteChannel();
}

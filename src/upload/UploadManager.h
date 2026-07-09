#pragma once
#include <QObject>
#include <QVariantList>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <qqmlregistration.h>

class QNetworkAccessManager;
class Settings;
class HistoryStore;

// ShareX-style modular destinations. Each destination is a JSON object
// (~/.config/unisic/destinations.json), analogous to .sxcu:
// {
//   "name": "my-server", "type": "http",
//   "requestUrl": "https://x.example/upload", "method": "POST",
//   "fileFormName": "file",
//   "headers": {"Authorization": "Bearer ..."},
//   "arguments": {"key": "value"},
//   "responseType": "json" | "text",
//   "urlPath": "$json:files[0].url$" | "$text$" | "$regex:...$",
//   "deletionUrlPath": "$json:deletion_url$"
// }
// "urlPath"/"deletionUrlPath" may also embed tokens inline, e.g.
//   "https://imgur.com/delete/$json:data.deletehash$"
// Optional "urlReplace"/"urlReplaceWith" post-process the extracted URL with a
// plain string replace (e.g. tmpfiles.org viewer URL -> direct /dl/ URL).
// type "curl" handles ftp://, ftps://, sftp:// via the curl CLI:
// { "name":"my-sftp", "type":"curl", "requestUrl":"sftp://host/dir/",
//   "user":"name:pass", "publicUrlBase":"https://host/dir/" }
// Optional "insecure": true skips sftp host-key verification (curl builds
// whose sftp backend cannot read known_hosts). Off by default.
//
// "body" selects the request encoding:
//   absent / "multipart" -> multipart/form-data with the file part (default),
//   "json" -> the raw string in "data" is POSTed as the body; tokens
//     $base64$ (file bytes, base64), $filename$, $mime$ are substituted first,
//     and Content-Type defaults to application/json unless a header overrides.
class UploadManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppContext")

    Q_PROPERTY(QVariantList destinations READ destinationsVariant NOTIFY destinationsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    using Callback = std::function<void(const QString &url, const QString &deleteUrl, const QString &error)>;

    explicit UploadManager(Settings *settings, QObject *parent = nullptr);

    void uploadFile(const QString &filePath, Callback cb);
    void uploadData(const QByteArray &data, const QString &fileName, const QString &mime, Callback cb);

    QVariantList destinationsVariant() const;
    bool busy() const { return m_busy; }

    Q_INVOKABLE void saveDestination(const QVariantMap &dest);
    Q_INVOKABLE void removeDestination(const QString &name);
    Q_INVOKABLE QVariantMap destination(const QString &name) const;

    // Import a ShareX Custom Uploader (.sxcu) file. Accepts a plain path or a
    // file:// URL. Returns the imported destination's name on success, or an
    // empty string on failure (with errorOut set). Maps the common case:
    // RequestMethod POST + Body MultipartFormData/JSON + JSON/text response.
    Q_INVOKABLE QString importSxcu(const QString &pathOrUrl);
    // Last import error message for the QML layer to surface.
    Q_INVOKABLE QString lastImportError() const { return m_lastImportError; }

    // Settings export/import support.
    QJsonArray destinationsJson() const { return m_destinations; }
    void replaceAllDestinations(const QJsonArray &arr);

signals:
    void destinationsChanged();
    void busyChanged();

private:
    QJsonObject activeDestination() const;
    void loadDestinations();
    void persistDestinations();
    void ensureBuiltins();
    QString configPath() const;
    // Exactly one of `data` (in-memory capture bytes) or `srcPath` (a file on
    // disk — recordings can be hundreds of MB) is used. The path variants
    // STREAM the payload (multipart body device / curl -T <path>) instead of
    // holding the whole file in RAM for the duration of the transfer.
    void httpUpload(const QJsonObject &dest, const QByteArray &data, const QString &srcPath,
                    const QString &fileName, const QString &mime, Callback cb);
    void curlUpload(const QJsonObject &dest, const QByteArray &data, const QString &srcPath,
                    const QString &fileName, Callback cb);
    void startUpload(const QByteArray &data, const QString &srcPath, const QString &fileName,
                     const QString &mime, Callback cb);
    static QString extractUrl(const QJsonObject &dest, const QString &key, const QByteArray &response);
    static QString extractToken(const QString &token, const QByteArray &response);
    void setBusy(bool b);

    Settings *m_settings;
    QNetworkAccessManager *m_nam;
    QJsonArray m_destinations;
    bool m_busy = false;
    QString m_lastImportError;
    int m_active = 0; // in-flight uploads; busy = m_active > 0
};

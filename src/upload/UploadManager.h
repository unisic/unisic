#pragma once
#include <QObject>
#include <QVariantList>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <qqmlregistration.h>
#include <QUrl>

class QNetworkAccessManager;
class Settings;
class HistoryStore;

// Modular upload destinations. Each destination is a JSON object
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
    // True while at least one CAPTURE upload is in flight; the server editor's
    // test upload deliberately does not count (see Purpose).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    using Callback = std::function<void(const QString &url, const QString &deleteUrl, const QString &error)>;
    // Same three values testFinished() carries, delivered to one caller only.
    using TestCallback = std::function<void(bool ok, const QString &url, const QString &error)>;

    explicit UploadManager(Settings *settings, QObject *parent = nullptr);

    void uploadFile(const QString &filePath, Callback cb);
    void uploadData(const QByteArray &data, const QString &fileName, const QString &mime, Callback cb);
    void uploadDataTo(const QString &destination, const QByteArray &data,
                      const QString &fileName, const QString &mime, Callback cb);

    QVariantList destinationsVariant() const;
    bool busy() const { return m_busy; }

    Q_INVOKABLE void saveDestination(const QVariantMap &dest);
    Q_INVOKABLE void removeDestination(const QString &name);
    Q_INVOKABLE QVariantMap destination(const QString &name) const;

    // Try a destination that has NOT been saved yet: the server editor hands in
    // the current (unsaved) form state, this uploads a tiny generated PNG
    // through the very same http/curl path a real capture takes and answers
    // with testFinished(). Nothing is persisted: no destinations.json write, no
    // history entry, no clipboard touch. It takes the whole object and not a
    // name (the way uploadDataTo() does) because a name has to be looked up,
    // and destinationNamed() silently falls back to the FIRST stored
    // destination for an unknown one - an unsaved config would be "tested"
    // against somebody else's server.
    Q_INVOKABLE void testDestination(const QVariantMap &dest);
    // The same test, answered on a PRIVATE channel. testFinished() is one
    // global signal that the server editor's sheet listens to permanently
    // (guarded only by "my sheet is open and testing"), so a second tester
    // emitting it would let the two steal each other's answer: whichever
    // finishes first repaints the sheet, and the other reads a verdict it never
    // asked for. Everything that is not that sheet - the developer check, the
    // F8 smoke run - therefore takes this overload.
    void testDestination(const QVariantMap &dest, TestCallback cb);

    // Imgur talks to a per-user Client-ID that the app deliberately does not
    // ship (see the note in the .cpp). The destination list marks an Imgur
    // destination without one as needing setup, and its editor swaps the raw
    // headers JSON for a plain Client-ID field.
    Q_INVOKABLE bool isImgurDestination(const QVariantMap &dest) const
    { return isImgur(QJsonObject::fromVariantMap(dest)); }
    Q_INVOKABLE QString imgurClientIdOf(const QVariantMap &dest) const
    { return imgurClientId(QJsonObject::fromVariantMap(dest)); }

    // Import a ShareX Custom Uploader (.sxcu) file. Accepts a plain path or a
    // file:// URL. Returns the imported destination's name on success, or an
    // empty string on failure (with errorOut set). Maps the common case:
    // any RequestMethod + Body MultipartFormData/JSON + JSON/text response.
    Q_INVOKABLE QString importSxcu(const QString &pathOrUrl);
    // Last import error message for the QML layer to surface.
    Q_INVOKABLE QString lastImportError() const { return m_lastImportError; }

    // Settings export/import support.
    QJsonArray destinationsJson() const { return m_destinations; }
    void replaceAllDestinations(const QJsonArray &arr);

signals:
    void destinationsChanged();
    void busyChanged();
    // Result of testDestination(). `ok` false carries `error`; `ok` true with an
    // empty `url` is the legitimate curl case (no publicUrlBase configured).
    void testFinished(bool ok, const QString &url, const QString &error);

private:
    // What a request is FOR. A capture upload and the server editor's test
    // upload take the very same code path and differ in exactly two ways:
    //   - how long a server that goes quiet is given (a capture may be a
    //     hundreds-of-MB recording on a slow link; a test is a few hundred
    //     bytes with someone watching a "Testing…" sheet, so it fails fast -
    //     the timeouts and their reasoning live at the top of the .cpp);
    //   - whether it raises busy(), which is the UI's "a capture is being
    //     uploaded" lamp. A test must not grey out an open editor's Upload
    //     button, so it stays out of the count.
    // Both travel as this argument rather than as a flag on the request, so
    // every entry point has to say which kind of upload it is starting.
    enum class Purpose { Capture, Test };

    QJsonObject activeDestination() const;
    QJsonObject destinationNamed(const QString &name) const;
    void loadDestinations();
    void persistDestinations();
    void ensureBuiltins();
    QString configPath() const;
    // Exactly one of `data` (in-memory capture bytes) or `srcPath` (a file on
    // disk — recordings can be hundreds of MB) is used. The path variants
    // STREAM the payload (multipart body device / curl -T <path>) instead of
    // holding the whole file in RAM for the duration of the transfer.
    void httpUpload(const QJsonObject &dest, const QByteArray &data, const QString &srcPath,
                    const QString &fileName, const QString &mime, Purpose purpose, Callback cb);
    void curlUpload(const QJsonObject &dest, const QByteArray &data, const QString &srcPath,
                    const QString &fileName, Purpose purpose, Callback cb);
    void startUpload(const QByteArray &data, const QString &srcPath, const QString &fileName,
                     const QString &mime, const QString &destination, Callback cb);
    // Same as startUpload() but with the destination object already resolved, so
    // a caller can upload to a config that is not (or not yet) in the store.
    // Every caller states its Purpose: the three public upload entry points are
    // real captures, testDestination() is the one test.
    void startUploadTo(const QJsonObject &dest, const QByteArray &data, const QString &srcPath,
                       const QString &fileName, const QString &mime, Purpose purpose, Callback cb);
    static QString extractUrl(const QJsonObject &dest, const QString &key, const QByteArray &response);
    static QString extractToken(const QString &token, const QByteArray &response);
    static bool isImgur(const QJsonObject &dest);
    static QString imgurClientId(const QJsonObject &dest);
    void setBusy(bool b);

    Settings *m_settings;
    QNetworkAccessManager *m_nam;
    QJsonArray m_destinations;
    bool m_busy = false;
    QString m_lastImportError;
    int m_active = 0; // in-flight CAPTURE uploads; busy = m_active > 0
    // Correlates a testFinished() emission with the QML test that asked for it
    // (see the QML overload in the .cpp): only the newest one may speak.
    quint64 m_qmlTestGen = 0;
};

#include "CaptureNotification.h"
#include "AppContext.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUuid>

CaptureNotification::CaptureNotification(AppContext *app, const QImage &img,
                                         const QString &filePath, const QString &kind,
                                         QObject *parent)
    : QObject(parent), m_app(app), m_image(img), m_filePath(filePath), m_kind(kind)
{
    // Thumbnails are only removed in the destructor, so a crash/SIGKILL leaves
    // them behind forever — sweep leftovers once per process.
    static bool sweepDone = false;
    if (!sweepDone) {
        sweepDone = true;
        QDir cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                   + QStringLiteral("/unisic"));
        const QStringList stale = cache.entryList({QStringLiteral("notif-*.png")}, QDir::Files);
        for (const QString &f : stale)
            cache.remove(f);
    }

    // Cache a small thumbnail to disk and hand QML a file:// URL — avoids
    // registering a bespoke image provider just for the popup. The full image
    // stays in RAM for the action buttons.
    if (!m_image.isNull()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + QStringLiteral("/unisic");
        QDir().mkpath(dir);
        m_thumbFile = dir + QStringLiteral("/notif-")
                      + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
        const QImage t = m_image.scaled(320, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (t.save(m_thumbFile, "PNG"))
            m_thumbSource = QUrl::fromLocalFile(m_thumbFile).toString();
        else
            m_thumbFile.clear();
    }
}

CaptureNotification::~CaptureNotification()
{
    if (!m_thumbFile.isEmpty())
        QFile::remove(m_thumbFile);
    if (!m_dragFile.isEmpty())
        QFile::remove(m_dragFile);
}

QString CaptureNotification::dragUri()
{
    // Prefer the real saved file (recordings always have one; so do saved
    // screenshots). For an unsaved image, write the full-resolution frame to a
    // private temp PNG once — mirrors AppContext::openPreview so a drag doesn't
    // litter the user's save folder on cancel. The poster frame of a recording
    // is never dragged: a recording always has m_filePath, so we never reach the
    // temp branch for it.
    if (m_filePath.isEmpty() && m_dragFile.isEmpty() && !m_image.isNull()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString f = dir + QStringLiteral("/") + QCoreApplication::applicationName()
                          + QStringLiteral("-drag-")
                          + QUuid::createUuid().toString(QUuid::WithoutBraces)
                          + QStringLiteral(".png");
        if (m_image.save(f, "PNG")) {
            // /tmp is world-listable; keep the frame owner-only.
            QFile::setPermissions(f, QFile::ReadOwner | QFile::WriteOwner);
            m_dragFile = f;
        }
    }
    const QString path = !m_filePath.isEmpty() ? m_filePath : m_dragFile;
    return path.isEmpty() ? QString()
                          : QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString CaptureNotification::fileName() const
{
    return m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).fileName();
}

void CaptureNotification::setUrl(const QString &url)
{
    // Always emit when leaving the uploading state, even if the URL is unchanged
    // (an FTP/SFTP destination with no public URL completes with an empty url) —
    // DesktopNotifier::retire() frees this object on that very transition, so a
    // dropped signal here leaks the notification and its cached thumbnail.
    const bool changed = (m_url != url) || m_uploading;
    m_uploading = false;
    m_url = url;
    if (changed)
        emit stateChanged();
}

void CaptureNotification::setUploading(bool on)
{
    if (m_uploading == on)
        return;
    m_uploading = on;
    emit stateChanged();
}

void CaptureNotification::edit()
{
    m_app->openEditor(m_image);
    dismiss();
}

void CaptureNotification::preview()
{
    // Preview outlives the popup — don't dismiss, the user may still want the
    // action buttons while the floating preview is up.
    m_app->openPreview(m_image);
}

void CaptureNotification::copyImage()
{
    m_app->copyImageToClipboard(m_image);
    m_app->showToast(tr("Copied to clipboard"));
}

void CaptureNotification::copyAs(const QString &format)
{
    // A notification can represent an unsaved, clipboard-only capture. Path
    // based forms need a durable file, so save only for that explicit request;
    // data URI and an uploaded URL keep working without an unnecessary write.
    if (format == QLatin1String("path") && m_filePath.isEmpty())
        save();
    m_app->copyImageAs(m_image, m_filePath, m_url, format);
}

void CaptureNotification::copyUrl()
{
    if (m_url.isEmpty())
        return;
    m_app->copyText(m_url);
    m_app->showToast(tr("Link copied"));
}

void CaptureNotification::showQr()
{
    if (!m_url.isEmpty())
        m_app->showQr(m_url);
}

void CaptureNotification::save()
{
    if (!m_filePath.isEmpty())
        return;
    const QString path = m_app->saveImageAuto(m_image);
    if (!path.isEmpty()) {
        m_filePath = path;
        // finishCapture registered this capture as a PATHLESS history entry —
        // attach the file to exactly that entry (by id), or Delete/upload-URL
        // bookkeeping would look it up by path and silently miss.
        m_app->history()->setFilePathById(m_historyId, path);
        m_app->showToast(tr("Saved %1").arg(path));
        emit stateChanged();
    } else {
        m_app->showToast(tr("Could not save the capture. Check the save folder in Settings"),
                         true);
    }
}

void CaptureNotification::showInFolder()
{
    if (m_filePath.isEmpty())
        save();
    if (!m_filePath.isEmpty())
        m_app->showInFileManager(m_filePath);
}

void CaptureNotification::openCapture()
{
    if (m_filePath.isEmpty())
        save();
    if (!m_filePath.isEmpty())
        m_app->openFile(m_filePath);
}

void CaptureNotification::upload()
{
    m_app->uploadFromNotification(this, m_image, m_filePath);
}

void CaptureNotification::deleteCapture()
{
    // removeByFile refuses starred entries — surface that instead of
    // dismissing the card as if the file were gone.
    if (!m_filePath.isEmpty() && m_app->history()->fileIsFavorite(m_filePath)) {
        m_app->showToast(tr("Not deleted: this capture is starred in History (unstar it first)"),
                         true);
        return;
    }
    if (!m_filePath.isEmpty())
        m_app->history()->removeByFile(m_filePath);
    dismiss();
}

void CaptureNotification::ocr()
{
    m_app->ocrImage(m_image);
}

void CaptureNotification::trim()
{
    // Recordings only, and only once they are on disk — the trim window works on
    // a file, not on the in-memory capture.
    if (m_filePath.isEmpty())
        return;
    m_app->openTrimRecording(m_filePath);
    emit closeRequested();
}

void CaptureNotification::dismiss()
{
    emit closeRequested();
}

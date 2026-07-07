#include "CaptureNotification.h"
#include "AppContext.h"
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
}

QString CaptureNotification::fileName() const
{
    return m_filePath.isEmpty() ? QString() : QFileInfo(m_filePath).fileName();
}

void CaptureNotification::setUrl(const QString &url)
{
    m_uploading = false;
    if (m_url == url)
        return;
    m_url = url;
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

void CaptureNotification::copyImage()
{
    m_app->copyImageToClipboard(m_image);
    m_app->showToast(tr("Copied to clipboard"));
}

void CaptureNotification::copyUrl()
{
    if (m_url.isEmpty())
        return;
    m_app->copyText(m_url);
    m_app->showToast(tr("Link copied"));
}

void CaptureNotification::save()
{
    if (!m_filePath.isEmpty())
        return;
    const QString path = m_app->saveImageAuto(m_image);
    if (!path.isEmpty()) {
        m_filePath = path;
        m_app->showToast(tr("Saved %1").arg(path));
        emit stateChanged();
    }
}

void CaptureNotification::showInFolder()
{
    if (m_filePath.isEmpty())
        save();
    if (!m_filePath.isEmpty())
        m_app->openDirectory(QFileInfo(m_filePath).absolutePath());
}

void CaptureNotification::upload()
{
    m_app->uploadFromNotification(this, m_image, m_filePath);
}

void CaptureNotification::deleteCapture()
{
    if (!m_filePath.isEmpty())
        m_app->history()->removeByFile(m_filePath);
    dismiss();
}

void CaptureNotification::ocr()
{
    m_app->ocrImage(m_image);
}

void CaptureNotification::dismiss()
{
    emit closeRequested();
}

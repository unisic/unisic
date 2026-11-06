// AppContext's "share what you already have" half: a file from disk, whatever
// is on the clipboard, and a QR code of copied text. Same class as
// AppContext.cpp, separate translation unit, so the upload entry points that
// are not captures stay out of the capture pipeline's file.
#include "AppContext.h"
#include "Settings.h"
#include "upload/UploadManager.h"

#include <KSystemClipboard>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMimeData>

QString AppContext::clipboardUploadKind(const QMimeData *mime)
{
    if (!mime)
        return {};
    // A copied file also offers its path as text, and some file managers add a
    // thumbnail as an image: the file itself is what was copied.
    for (const QUrl &url : mime->urls())
        if (url.isLocalFile())
            return QStringLiteral("files");
    if (mime->hasImage() && !qvariant_cast<QImage>(mime->imageData()).isNull())
        return QStringLiteral("image");
    if (mime->hasText() && !mime->text().trimmed().isEmpty())
        return QStringLiteral("text");
    return {};
}

QString AppContext::clipboardTextFileName() const
{
    return QFileInfo(makeFileName()).completeBaseName() + QStringLiteral(".txt");
}

// KSystemClipboard reads over the data-control protocol where the compositor
// has it, so the tray menu works with no Unisic window focused; elsewhere it is
// QClipboard. The payload is consumed before this returns: the clipboard can
// change the moment control goes back to the event loop.
static const QMimeData *clipboardMime()
{
    return KSystemClipboard::instance()->mimeData(QClipboard::Clipboard);
}

void AppContext::uploadAnyFile()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("Upload a file"), m_settings->saveDirectory(), tr("All files (*)"));
    if (!path.isEmpty())
        uploadPath(path);
}

void AppContext::uploadPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        showToast(info.isDir() ? tr("Unisic uploads files, not folders")
                               : tr("Can't find %1").arg(info.fileName()),
                  true);
        return;
    }
    // The history upload already is "this file, through the chain, then the
    // after-upload actions", and it attaches the link when the file happens
    // to be one of the captures listed there.
    uploadFromHistory(info.absoluteFilePath());
}

void AppContext::uploadClipboard()
{
    uploadMimeData(clipboardMime());
}

void AppContext::uploadMimeData(const QMimeData *mime)
{
    const QString kind = clipboardUploadKind(mime);
    if (kind == QLatin1String("files")) {
        // One file per upload: every link is copied as it arrives, so a batch
        // would leave only the last one on the clipboard. The "Uploading
        // <name>" toast says which one went.
        for (const QUrl &url : mime->urls())
            if (url.isLocalFile())
                return uploadPath(url.toLocalFile());
        return;
    }
    if (kind == QLatin1String("image")) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        img.setDevicePixelRatio(1.0);
        showToast(tr("Uploading the copied image…"));
        // An unsaved capture's upload: same encode, upload format and pathless
        // history entry, with no card to update.
        uploadFromNotification(nullptr, img, {}, {});
        return;
    }
    if (kind == QLatin1String("text")) {
        const QByteArray data = mime->text().toUtf8();
        showToast(tr("Uploading the copied text…"));
        m_uploads->uploadData(data, clipboardTextFileName(), QStringLiteral("text/plain"),
                              [this](const QString &url, const QString &, const QString &err) {
                                  if (!err.isEmpty()) {
                                      showToast(tr("Upload failed: %1").arg(err), true);
                                      return;
                                  }
                                  afterUploadActions(url);
                              });
        return;
    }
    showToast(tr("The clipboard is empty"), true);
}

void AppContext::qrFromClipboard()
{
    const QMimeData *mime = clipboardMime();
    const QString text = mime && mime->hasText() ? mime->text().trimmed() : QString();
    if (text.isEmpty()) {
        showToast(tr("Copy some text first, then make its QR code"), true);
        return;
    }
    showQr(text);
}

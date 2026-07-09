#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QTimer>
#include <qqmlregistration.h>

class QFileSystemWatcher;

// Capture/upload history persisted as JSON, with thumbnails on disk.
// Newest entries first.
class HistoryStore : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppContext")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        ThumbnailRole,
        UrlRole,
        DeleteUrlRole,
        KindRole,       // "image" | "gif"
        TimestampRole,
    };

    explicit HistoryStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addEntry(const QString &filePath, const QImage &thumbSource,
                  const QString &kind, const QString &url = {}, const QString &deleteUrl = {});
    void setUrl(const QString &filePath, const QString &url, const QString &deleteUrl);
    // Regenerate the thumbnail of an existing entry (after editing it in place);
    // adds a new entry if the file wasn't tracked. A fresh thumb path is used so
    // the QML Image, keyed by URL, actually reloads.
    void refreshEntry(const QString &filePath, const QImage &img);

    // Removes the entry at row and moves its capture file to the trash.
    Q_INVOKABLE void remove(int row);
    // Removes the entry whose capture is filePath (and trashes the file).
    Q_INVOKABLE void removeByFile(const QString &filePath);
    // Clears all history entries but keeps the capture files on disk.
    Q_INVOKABLE void clearAll();

signals:
    void countChanged();
    // Emitted when a capture file could not be moved to the trash; the entry is
    // still removed. AppContext surfaces this as a toast.
    void fileTrashFailed(const QString &path);

private:
    struct Entry {
        QString filePath;
        QString thumbPath;
        QString url;
        QString deleteUrl;
        QString kind;
        QDateTime timestamp;
    };
    void load();
    void persist();
    QString dataDir() const;
    void removeRow(int row, bool trashFile);
    void rebuildWatches();   // watch the parent directories of all capture files
    void pruneMissing();     // drop entries whose capture file vanished on disk

    QVector<Entry> m_entries;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_validateTimer;
};

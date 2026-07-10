#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QSet>
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
        FavoriteRole,   // starred: survives Clear all, delete is blocked
    };

    explicit HistoryStore(QObject *parent = nullptr);
    ~HistoryStore() override;   // flushes a pending debounced persist

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns the new entry's runtime id (see Entry::id) so callers holding a
    // capture card can address exactly THIS entry later.
    quint64 addEntry(const QString &filePath, const QImage &thumbSource,
                     const QString &kind, const QString &url = {}, const QString &deleteUrl = {});
    void setUrl(const QString &filePath, const QString &url, const QString &deleteUrl);
    // Attach an upload URL to exactly the entry created for this capture
    // (id from addEntry) instead of adding a duplicate. False if evicted.
    bool setUrlById(quint64 id, const QString &url, const QString &deleteUrl);
    // Point the entry at a just-saved file (capture-card Save) so path-keyed
    // lookups (Delete, upload URL) find it. False if the entry is gone.
    bool setFilePathById(quint64 id, const QString &filePath);
    // True when the entry holding this capture file is starred (the capture
    // card uses it to refuse Delete with feedback instead of silently).
    bool fileIsFavorite(const QString &filePath) const;
    // Regenerate the thumbnail of an existing entry (after editing it in place);
    // adds a new entry if the file wasn't tracked. A fresh thumb path is used so
    // the QML Image, keyed by URL, actually reloads.
    void refreshEntry(const QString &filePath, const QImage &img);

    // Removes the entry at row and moves its capture file to the trash.
    // Favorited entries are protected — un-star first.
    Q_INVOKABLE void remove(int row);
    // Removes the entry whose capture is filePath (and trashes the file).
    // Favorited entries are protected — un-star first.
    Q_INVOKABLE void removeByFile(const QString &filePath);
    // Clears the history AND moves the capture files to the trash — except
    // favorited entries, which are kept (entry + file).
    Q_INVOKABLE void clearAll();
    // Star/un-star an entry (see FavoriteRole).
    Q_INVOKABLE void setFavorite(int row, bool favorite);

signals:
    void countChanged();
    // Emitted when a capture file could not be moved to the trash; the entry is
    // still removed. AppContext surfaces this as a toast.
    void fileTrashFailed(const QString &path);

private:
    struct Entry {
        // Runtime-only identity (not persisted): fresh ids are assigned on
        // every load. Capture cards hold one to address their entry after
        // rows moved or multiple pathless entries accumulated.
        quint64 id = 0;
        QString filePath;
        QString thumbPath;
        QString url;
        QString deleteUrl;
        QString kind;
        QDateTime timestamp;
        bool favorite = false;
    };
    void load();
    quint64 m_nextId = 1; // runtime entry-id counter (0 = "no entry")
    // Startup-only: delete thumbs/ PNGs no loaded entry references (orphaned by
    // a crash inside the persist-debounce window) and clear thumbPath on
    // entries whose thumb file is gone.
    void sweepThumbs();
    // Debounced persistence: at the 500-entry cap a persist is a ~100 KB JSON
    // rebuild + full rewrite, and the capture path used to do it twice per shot
    // (addEntry + setUrl). persist() coalesces; persistNow() writes atomically
    // (QSaveFile) and is used for explicit destructive actions + shutdown flush.
    void persist();
    void persistNow();
    QString dataDir() const;
    void removeRow(int row, bool trashFile);
    void rebuildWatches();   // watch the parent directories of all capture files
    // Drop entries whose capture file vanished on disk. Entries whose whole
    // parent directory is absent (unmounted volume) are kept, not pruned.
    // With dirFilter non-null only entries living in one of those directories
    // are stat()ed — the directory watcher knows exactly which dirs changed,
    // so a capture save no longer triggers an existence sweep over all 500
    // entries.
    void pruneMissing(const QSet<QString> *dirFilter = nullptr);

    QVector<Entry> m_entries;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_validateTimer;
    QTimer m_persistTimer;
    QSet<QString> m_changedDirs; // dirs reported by the watcher since last validation
};

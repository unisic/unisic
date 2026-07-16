#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSize>
#include <QTimer>
#include <QVariantMap>
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
        KindRole,       // "image" | "gif" | "video" — the MEDIA type
        TimestampRole,
        FavoriteRole,   // starred: survives Clear all, delete is blocked
        // "image" | "gif" | "video" | "replay" — what the History page filters
        // by. Kind plus how the capture was made: an instant-replay clip is a
        // video in every other respect (it trims, it plays, its card offers the
        // same actions), so it stays kind "video" and only splits off here.
        CategoryRole,
        EntryIdRole,    // Entry::id — the stable key the History page selects by
        SizeTextRole,   // capture file size, localized ("1,2 MB"); empty if unsaved
        DimensionsRole, // "1920×1080" for images, empty otherwise (see m_dimCache)
    };

    explicit HistoryStore(QObject *parent = nullptr);
    ~HistoryStore() override;   // flushes a pending debounced persist

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns the new entry's runtime id (see Entry::id) so callers holding a
    // capture card can address exactly THIS entry later. `origin` records how
    // the capture was made when the file cannot say it (currently only
    // "replay"); it feeds CategoryRole and is persisted.
    quint64 addEntry(const QString &filePath, const QImage &thumbSource,
                     const QString &kind, const QString &url = {}, const QString &deleteUrl = {},
                     const QString &origin = {});
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

    // ---- id-addressed API, used by the History page ----
    // The page filters through a proxy model and selects across filter changes,
    // so a row index is not a usable handle: it means a different entry after a
    // filter switch, and a batch delete shifts every row behind it. These take
    // Entry::id (the `entryId` role) instead.
    // Entry fields for one id: filePath, url, deleteUrl, kind, favorite,
    // thumbnail, timestamp. Empty map when the id is gone (evicted/deleted).
    Q_INVOKABLE QVariantMap entryById(quint64 id) const;
    // Batch delete (trashing the files), skipping starred entries like remove().
    // One persist, one watch rebuild and one trash cue for the whole batch.
    Q_INVOKABLE void removeByIds(const QVariantList &ids);
    // Batch star/un-star; one persist for the whole batch.
    Q_INVOKABLE void setFavoriteByIds(const QVariantList &ids, bool favorite);

signals:
    void countChanged();
    // Emitted when a capture file could not be moved to the trash; the entry is
    // still removed. AppContext surfaces this as a toast.
    void fileTrashFailed(const QString &path);
    // Emitted once per explicit user deletion (single remove or Clear all) —
    // AppContext plays the fixed trash sound cue on it.
    void entryTrashed();

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
        // "" for an ordinary capture, "replay" for an instant-replay export.
        QString origin;
    };
    // CategoryRole's value for one entry. Entries written before 0.7.1 carry no
    // origin, so a replay saved back then is recognized by the file name
    // GifRecorder gives every export ("Unisic_Replay_<stamp>.mp4") — a renamed
    // old clip falls back to plain "video", which is what it used to be anyway.
    static QString categoryOf(const Entry &e);
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
    // DimensionsRole reads the image header (QImageReader::size(), no decode) and
    // data() runs per delegate binding — scrolling would re-read the same files
    // continuously, so the answer is cached per path. Bounded by the 500-entry cap.
    mutable QHash<QString, QSize> m_dimCache;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_validateTimer;
    QTimer m_persistTimer;
    QSet<QString> m_changedDirs; // dirs reported by the watcher since last validation
};

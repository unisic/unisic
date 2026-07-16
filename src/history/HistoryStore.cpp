#include "HistoryStore.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImage>
#include <QImageReader>
#include <QLocale>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QSet>
#include <QDateTime>
#include <QDebug>

// Thumbnail scale in two steps: a cheap nearest-neighbour pass to ~2x the
// target first, so the smooth pass works on ~0.5 MP instead of a full 8+ MP
// capture (~10x cheaper on the GUI thread, visually identical at 480x300).
// The guard keeps the fast pass downscale-only.
static QImage makeThumb(const QImage &src)
{
    QImage s = src;
    if (s.width() > 960 || s.height() > 600)
        s = s.scaled(960, 600, Qt::KeepAspectRatio, Qt::FastTransformation);
    return s.scaled(480, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

HistoryStore::HistoryStore(QObject *parent) : QAbstractListModel(parent)
{
    load();
    sweepThumbs();

    // External-deletion sync: watch the directories the captures live in (a
    // handful even at 500 entries) rather than 500 individual files. Our own
    // saves also fire directoryChanged, so validation is debounced, only does a
    // cheap existence check, and only over entries in the dirs that changed.
    m_watcher = new QFileSystemWatcher(this);
    m_validateTimer.setSingleShot(true);
    m_validateTimer.setInterval(250);
    connect(&m_validateTimer, &QTimer::timeout, this, [this] {
        // Swap first: events arriving during the prune re-arm cleanly.
        const QSet<QString> dirs = std::move(m_changedDirs);
        m_changedDirs.clear();
        pruneMissing(&dirs);
        rebuildWatches();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &dir) {
        m_changedDirs.insert(dir);
        m_validateTimer.start();
    });

    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(500);
    connect(&m_persistTimer, &QTimer::timeout, this, &HistoryStore::persistNow);
    // A SIGTERM/logout kill skips destructors — flush pending writes on quit.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_persistTimer.isActive())
            persistNow();
    });

    pruneMissing();   // catch deletions that happened while Unisic was closed
    rebuildWatches();
}

HistoryStore::~HistoryStore()
{
    if (m_persistTimer.isActive())
        persistNow();
}

void HistoryStore::rebuildWatches()
{
    QSet<QString> dirs;
    for (const Entry &e : m_entries) {
        if (e.filePath.isEmpty())
            continue;
        const QString dir = QFileInfo(e.filePath).absolutePath();
        if (!dir.isEmpty())
            dirs.insert(dir);
    }
    const QStringList wanted = QStringList(dirs.begin(), dirs.end());
    const QStringList current = m_watcher->directories();
    if (QSet<QString>(current.begin(), current.end()) == dirs)
        return; // unchanged — avoid churning inotify watches on every capture
    if (!current.isEmpty())
        m_watcher->removePaths(current);
    if (!wanted.isEmpty())
        m_watcher->addPaths(wanted);
}

void HistoryStore::pruneMissing(const QSet<QString> *dirFilter)
{
    // Batch: removeRow() persists + rebuilds watches per call — O(n²) after a
    // bulk external deletion. Walk back-to-front so removals don't shift the
    // indices still to check.
    bool removed = false;
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        const Entry &e = m_entries[i];
        if (dirFilter && !e.filePath.isEmpty()
            && !dirFilter->contains(QFileInfo(e.filePath).absolutePath()))
            continue;
        // Missing file + present parent dir = actually deleted. If the whole
        // directory is gone (unmounted removable/network volume) keep the
        // entry — it revalidates on the next launch/change after remount
        // instead of being destroyed while the capture file still exists.
        if (!e.filePath.isEmpty() && !QFile::exists(e.filePath)
            && QFileInfo(e.filePath).dir().exists()) {
            beginRemoveRows({}, i, i);
            QFile::remove(e.thumbPath);
            m_entries.removeAt(i);
            endRemoveRows();
            removed = true;
        }
    }
    if (removed) {
        persist();
        rebuildWatches();
        emit countChanged();
    }
}

void HistoryStore::sweepThumbs()
{
    // Crash reconciliation: thumb PNGs are written/deleted immediately while
    // the JSON index is persisted up to 500 ms later, so a hard kill in that
    // window leaves orphaned PNGs in thumbs/ (nothing else ever reclaims them)
    // or entries pointing at an already-deleted thumb. Runs once at startup,
    // before the model is exposed — no change notifications needed.
    QSet<QString> referenced;
    for (Entry &e : m_entries) {
        if (e.thumbPath.isEmpty())
            continue;
        if (QFile::exists(e.thumbPath))
            referenced.insert(QFileInfo(e.thumbPath).absoluteFilePath());
        else
            e.thumbPath.clear();   // dangling — show a clean no-thumb tile
    }
    const QFileInfoList thumbs = QDir(dataDir() + "/thumbs")
        .entryInfoList({QStringLiteral("*.png")}, QDir::Files);
    for (const QFileInfo &fi : thumbs) {
        if (!referenced.contains(fi.absoluteFilePath()))
            QFile::remove(fi.absoluteFilePath());
    }
}

QString HistoryStore::dataDir() const
{
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (d.isEmpty())
        d = QDir::homePath() + "/.local/share/unisic";
    QDir().mkpath(d + "/thumbs");
    return d;
}

void HistoryStore::load()
{
    const QString path = dataDir() + "/history.json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (!doc.isArray()) {
        // Non-empty but unparseable / not an array (hand edit, truncated write
        // from a pre-QSaveFile version, FS corruption): move it aside instead
        // of proceeding — the next persistNow() would otherwise atomically
        // overwrite it with the empty model, destroying every entry and every
        // starred favorite. Same .broken-<ts> preservation as
        // UploadManager::loadDestinations. (An empty file just loads nothing.)
        if (!raw.trimmed().isEmpty()) {
            const QString bak = path + QStringLiteral(".broken-")
                                + QString::number(QDateTime::currentSecsSinceEpoch());
            QFile::rename(path, bak);
            qWarning() << "history.json unparseable, preserved as" << bak
                       << err.errorString();
        }
        return;
    }
    const QJsonArray arr = doc.array();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        m_entries.append({
            m_nextId++, // runtime-only id; fresh every load
            o.value(QStringLiteral("file")).toString(),
            o.value(QStringLiteral("thumb")).toString(),
            o.value(QStringLiteral("url")).toString(),
            o.value(QStringLiteral("deleteUrl")).toString(),
            o.value(QStringLiteral("kind")).toString(QStringLiteral("image")),
            QDateTime::fromString(o.value(QStringLiteral("ts")).toString(), Qt::ISODate),
            o.value(QStringLiteral("fav")).toBool(),
            o.value(QStringLiteral("origin")).toString(),
        });
    }
}

void HistoryStore::persist()
{
    m_persistTimer.start();   // coalesce bursts (addEntry + setUrl per capture)
}

void HistoryStore::persistNow()
{
    m_persistTimer.stop();
    QJsonArray arr;
    for (const Entry &e : m_entries) {
        QJsonObject o{
            {QStringLiteral("file"), e.filePath},
            {QStringLiteral("thumb"), e.thumbPath},
            {QStringLiteral("url"), e.url},
            {QStringLiteral("deleteUrl"), e.deleteUrl},
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("ts"), e.timestamp.toString(Qt::ISODate)},
            {QStringLiteral("fav"), e.favorite},
        };
        // Absent for ordinary captures — the common entry stays as it was.
        if (!e.origin.isEmpty())
            o.insert(QStringLiteral("origin"), e.origin);
        arr.append(o);
    }
    // QSaveFile: atomic rename-replace — a crash mid-write can no longer
    // truncate history.json to garbage.
    QSaveFile f(dataDir() + "/history.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

int HistoryStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QString HistoryStore::categoryOf(const Entry &e)
{
    if (e.origin == QLatin1String("replay"))
        return QStringLiteral("replay");
    // Pre-0.7.1 entries have no origin: fall back to the name every replay
    // export is given (GifRecorder writes "Unisic_Replay_<stamp>.mp4"), so the
    // clips already in someone's history land in the right chip too.
    if (e.kind == QLatin1String("video")
        && QFileInfo(e.filePath).fileName().startsWith(QLatin1String("Unisic_Replay_")))
        return QStringLiteral("replay");
    return e.kind;
}

QVariant HistoryStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries[index.row()];
    switch (role) {
    case FilePathRole: return e.filePath;
    case ThumbnailRole: return e.thumbPath;
    case UrlRole: return e.url;
    case DeleteUrlRole: return e.deleteUrl;
    case KindRole: return e.kind;
    case TimestampRole: return e.timestamp;
    case FavoriteRole: return e.favorite;
    case CategoryRole: return categoryOf(e);
    case EntryIdRole: return e.id;
    case SizeTextRole: {
        // Formatted here, not in QML: QLocale knows the locale's decimal comma,
        // and QML has no equivalent of formattedDataSize().
        if (e.filePath.isEmpty())
            return QString();
        const qint64 bytes = QFileInfo(e.filePath).size();
        if (bytes <= 0)
            return QString();
        // Precision by magnitude, so the tile's one meta line stays short enough
        // to fit: "6,4 MiB" carries as much as anyone reads, "832,03 KiB" just
        // pushes the file size out of the tile. formattedDataSize picks the unit
        // itself — scale here only to decide how many decimals it deserves.
        double scaled = double(bytes);
        while (scaled >= 1024.0)
            scaled /= 1024.0;
        return QLocale().formattedDataSize(bytes, scaled < 10.0 ? 1 : 0);
    }
    case DimensionsRole: {
        // Recordings would need a demux to answer; only images are cheap here.
        if (e.kind != QLatin1String("image") || e.filePath.isEmpty())
            return QString();
        auto it = m_dimCache.constFind(e.filePath);
        if (it == m_dimCache.constEnd())
            it = m_dimCache.insert(e.filePath, QImageReader(e.filePath).size());
        return it->isValid() ? QStringLiteral("%1×%2").arg(it->width()).arg(it->height())
                             : QString();
    }
    }
    return {};
}

QHash<int, QByteArray> HistoryStore::roleNames() const
{
    return {
        {FilePathRole, "filePath"},
        {ThumbnailRole, "thumbnail"},
        {UrlRole, "url"},
        {DeleteUrlRole, "deleteUrl"},
        {KindRole, "kind"},
        {TimestampRole, "timestamp"},
        {FavoriteRole, "favorite"},
        {CategoryRole, "category"},
        {EntryIdRole, "entryId"},
        {SizeTextRole, "fileSizeText"},
        {DimensionsRole, "dimensions"},
    };
}

quint64 HistoryStore::addEntry(const QString &filePath, const QImage &thumbSource,
                               const QString &kind, const QString &url, const QString &deleteUrl,
                               const QString &origin)
{
    QString thumbPath;
    if (!thumbSource.isNull()) {
        thumbPath = dataDir() + "/thumbs/" +
                    QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        makeThumb(thumbSource).save(thumbPath, "PNG");
    }
    const quint64 id = m_nextId++;
    beginInsertRows({}, 0, 0);
    m_entries.prepend({id, filePath, thumbPath, url, deleteUrl, kind,
                       QDateTime::currentDateTime(), /*favorite=*/false, origin});
    endInsertRows();
    while (m_entries.size() > 500) {
        // Cap eviction spares favorites: evict the oldest non-favorite.
        int victim = -1;
        for (int i = m_entries.size() - 1; i >= 0; --i) {
            if (!m_entries[i].favorite) { victim = i; break; }
        }
        if (victim < 0)
            break; // everything is starred — let the list exceed the cap
        beginRemoveRows({}, victim, victim);
        QFile::remove(m_entries[victim].thumbPath);
        m_entries.removeAt(victim);
        endRemoveRows();
    }
    persist();
    rebuildWatches();
    emit countChanged();
    return id;
}

void HistoryStore::setUrl(const QString &filePath, const QString &url, const QString &deleteUrl)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].filePath == filePath) {
            m_entries[i].url = url;
            m_entries[i].deleteUrl = deleteUrl;
            emit dataChanged(index(i), index(i), {UrlRole, DeleteUrlRole});
            persist();
            return;
        }
    }
}

bool HistoryStore::setUrlById(quint64 id, const QString &url, const QString &deleteUrl)
{
    // Attach an upload URL to exactly the capture's own entry: with several
    // unsaved cards on screen a "newest pathless" heuristic would attach the
    // URL to a different capture.
    if (id == 0)
        return false;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            m_entries[i].url = url;
            m_entries[i].deleteUrl = deleteUrl;
            emit dataChanged(index(i), index(i), {UrlRole, DeleteUrlRole});
            persist();
            return true;
        }
    }
    return false;
}

bool HistoryStore::setFilePathById(quint64 id, const QString &filePath)
{
    // The capture card's Save button persists an until-now unsaved capture:
    // point its own entry (by id — see setUrlById) at the file so path-keyed
    // lookups (Delete, upload URL) find it.
    if (id == 0 || filePath.isEmpty())
        return false;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) {
            m_entries[i].filePath = filePath;
            emit dataChanged(index(i), index(i), {FilePathRole});
            persist();
            rebuildWatches();
            return true;
        }
    }
    return false;
}

bool HistoryStore::fileIsFavorite(const QString &filePath) const
{
    if (filePath.isEmpty())
        return false;
    for (const Entry &e : m_entries)
        if (e.favorite && e.filePath == filePath)
            return true;
    return false;
}

void HistoryStore::refreshEntry(const QString &filePath, const QImage &img)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].filePath != filePath)
            continue;
        const QString oldThumb = m_entries[i].thumbPath;
        const QString newThumb = dataDir() + "/thumbs/" +
                                 QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        if (!img.isNull() && makeThumb(img).save(newThumb, "PNG")) {
            m_entries[i].thumbPath = newThumb;
            if (!oldThumb.isEmpty())
                QFile::remove(oldThumb);
        }
        emit dataChanged(index(i), index(i), {ThumbnailRole});
        persist();
        return;
    }
    // File wasn't tracked (edited something outside history) — record it.
    addEntry(filePath, img, QStringLiteral("image"));
}

void HistoryStore::removeRow(int row, bool trashFile)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const Entry e = m_entries[row];
    // Move the capture to the trash on an explicit user delete. Never hard-delete
    // on failure (e.g. moveToTrash unsupported on the volume) — surface it instead.
    if (trashFile && !e.filePath.isEmpty() && QFile::exists(e.filePath)) {
        if (!QFile::moveToTrash(e.filePath))
            emit fileTrashFailed(e.filePath);
    }
    beginRemoveRows({}, row, row);
    QFile::remove(e.thumbPath);
    m_entries.removeAt(row);
    endRemoveRows();
    persistNow();   // explicit delete — never resurrectable by a crash
    rebuildWatches();
    emit countChanged();
    if (trashFile)
        emit entryTrashed(); // explicit user delete only, not stale-entry cleanup
}

void HistoryStore::remove(int row)
{
    // Starred entries are protected — the UI disables the button too, but the
    // model is the actual gate (a favorite must be un-starred to be deleted).
    if (row >= 0 && row < m_entries.size() && m_entries[row].favorite)
        return;
    removeRow(row, /*trashFile=*/true);
}

void HistoryStore::setFavorite(int row, bool favorite)
{
    if (row < 0 || row >= m_entries.size() || m_entries[row].favorite == favorite)
        return;
    m_entries[row].favorite = favorite;
    emit dataChanged(index(row), index(row), {FavoriteRole});
    persist();
}

QVariantMap HistoryStore::entryById(quint64 id) const
{
    for (const Entry &e : m_entries) {
        if (e.id != id)
            continue;
        return QVariantMap{
            {QStringLiteral("filePath"), e.filePath},
            {QStringLiteral("thumbnail"), e.thumbPath},
            {QStringLiteral("url"), e.url},
            {QStringLiteral("deleteUrl"), e.deleteUrl},
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("timestamp"), e.timestamp},
            {QStringLiteral("favorite"), e.favorite},
        };
    }
    return {};
}

void HistoryStore::removeByIds(const QVariantList &ids)
{
    QSet<quint64> wanted;
    for (const QVariant &v : ids)
        wanted.insert(v.toULongLong());
    if (wanted.isEmpty())
        return;
    // Back-to-front: removals must not shift the rows still to visit.
    bool removed = false;
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        // Copy: the entry dies inside the loop body.
        const Entry e = m_entries.at(i);
        if (!wanted.contains(e.id) || e.favorite) // starred: same gate as remove()
            continue;
        if (!e.filePath.isEmpty() && QFile::exists(e.filePath)) {
            if (!QFile::moveToTrash(e.filePath))
                emit fileTrashFailed(e.filePath);
        }
        beginRemoveRows({}, i, i);
        QFile::remove(e.thumbPath);
        m_entries.removeAt(i);
        endRemoveRows();
        removed = true;
    }
    if (!removed)
        return;
    // Once for the batch, not per entry: removeRow() would persist + rebuild the
    // watches (and fire the trash cue) on every single deletion.
    persistNow();
    rebuildWatches();
    emit countChanged();
    emit entryTrashed();
}

void HistoryStore::setFavoriteByIds(const QVariantList &ids, bool favorite)
{
    QSet<quint64> wanted;
    for (const QVariant &v : ids)
        wanted.insert(v.toULongLong());
    bool changed = false;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (!wanted.contains(m_entries[i].id) || m_entries[i].favorite == favorite)
            continue;
        m_entries[i].favorite = favorite;
        emit dataChanged(index(i), index(i), {FavoriteRole});
        changed = true;
    }
    if (changed)
        persist();
}

void HistoryStore::removeByFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].filePath == filePath) {
            // Same gate as remove(): starred entries must be un-starred before
            // they can be deleted — this path is reachable from the capture
            // notification's Delete button, which has no favorite check.
            if (m_entries[i].favorite)
                return;
            removeRow(i, /*trashFile=*/true);
            return;
        }
    }
}

void HistoryStore::clearAll()
{
    // Clears entries AND trashes their capture files (the History page shows a
    // warning dialog first). Favorites survive untouched — entry and file.
    // Trash, never hard-delete: recoverable, and failures are surfaced.
    beginResetModel();
    QVector<Entry> kept;
    for (const Entry &e : m_entries) {
        if (e.favorite) {
            kept.append(e);
            continue;
        }
        if (!e.filePath.isEmpty() && QFile::exists(e.filePath)) {
            if (!QFile::moveToTrash(e.filePath))
                emit fileTrashFailed(e.filePath);
        }
        QFile::remove(e.thumbPath);
    }
    const bool removedAny = kept.size() != m_entries.size();
    m_entries = kept;
    endResetModel();
    persistNow();   // explicit clear — flush immediately
    rebuildWatches(); // drop stale directory watches, they'd keep firing validations
    emit countChanged();
    if (removedAny)
        emit entryTrashed(); // one cue for the whole clear, not per entry
}

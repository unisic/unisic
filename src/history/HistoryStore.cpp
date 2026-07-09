#include "HistoryStore.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
#include <QSet>

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
        if (!e.filePath.isEmpty() && !QFile::exists(e.filePath)) {
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
    QFile f(dataDir() + "/history.json");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        m_entries.append({
            o.value(QStringLiteral("file")).toString(),
            o.value(QStringLiteral("thumb")).toString(),
            o.value(QStringLiteral("url")).toString(),
            o.value(QStringLiteral("deleteUrl")).toString(),
            o.value(QStringLiteral("kind")).toString(QStringLiteral("image")),
            QDateTime::fromString(o.value(QStringLiteral("ts")).toString(), Qt::ISODate),
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
        arr.append(QJsonObject{
            {QStringLiteral("file"), e.filePath},
            {QStringLiteral("thumb"), e.thumbPath},
            {QStringLiteral("url"), e.url},
            {QStringLiteral("deleteUrl"), e.deleteUrl},
            {QStringLiteral("kind"), e.kind},
            {QStringLiteral("ts"), e.timestamp.toString(Qt::ISODate)},
        });
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
    };
}

void HistoryStore::addEntry(const QString &filePath, const QImage &thumbSource,
                            const QString &kind, const QString &url, const QString &deleteUrl)
{
    QString thumbPath;
    if (!thumbSource.isNull()) {
        thumbPath = dataDir() + "/thumbs/" +
                    QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        makeThumb(thumbSource).save(thumbPath, "PNG");
    }
    beginInsertRows({}, 0, 0);
    m_entries.prepend({filePath, thumbPath, url, deleteUrl, kind, QDateTime::currentDateTime()});
    endInsertRows();
    while (m_entries.size() > 500) {
        beginRemoveRows({}, m_entries.size() - 1, m_entries.size() - 1);
        QFile::remove(m_entries.last().thumbPath);
        m_entries.removeLast();
        endRemoveRows();
    }
    persist();
    rebuildWatches();
    emit countChanged();
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
}

void HistoryStore::remove(int row)
{
    removeRow(row, /*trashFile=*/true);
}

void HistoryStore::removeByFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].filePath == filePath) {
            removeRow(i, /*trashFile=*/true);
            return;
        }
    }
}

void HistoryStore::clearAll()
{
    beginResetModel();
    for (const Entry &e : m_entries)
        QFile::remove(e.thumbPath);
    m_entries.clear();
    endResetModel();
    persistNow();   // explicit clear — flush immediately
    rebuildWatches(); // drop stale directory watches, they'd keep firing validations
    emit countChanged();
}

#include "HistoryStore.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

HistoryStore::HistoryStore(QObject *parent) : QAbstractListModel(parent)
{
    load();
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
    QFile f(dataDir() + "/history.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
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
        thumbSource.scaled(480, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(thumbPath, "PNG");
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

void HistoryStore::remove(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows({}, row, row);
    QFile::remove(m_entries[row].thumbPath);
    m_entries.removeAt(row);
    endRemoveRows();
    persist();
    emit countChanged();
}

void HistoryStore::clearAll()
{
    beginResetModel();
    for (const Entry &e : m_entries)
        QFile::remove(e.thumbPath);
    m_entries.clear();
    endResetModel();
    persist();
    emit countChanged();
}

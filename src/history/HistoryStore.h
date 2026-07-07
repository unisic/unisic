#pragma once
#include <QAbstractListModel>
#include <QDateTime>
#include <qqmlregistration.h>

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

    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void clearAll();

signals:
    void countChanged();

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

    QVector<Entry> m_entries;
};

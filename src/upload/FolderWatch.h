#pragma once
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

// Hands over each file that appears in one folder once it has stopped
// changing for a settle period. What was there when the watch started is never
// handed over, nor are dotfiles, subfolders or a browser's partial download.
// inotify reports a folder's creates and renames, not writes into a file, so
// a new file is re-checked by timer until its size and mtime hold still.
class FolderWatch : public QObject
{
    Q_OBJECT
public:
    explicit FolderWatch(QObject *parent = nullptr);

    // Empty stops watching. False, and nothing is watched, when `path` is not
    // a folder that can be watched.
    bool setPath(const QString &path);
    QString path() const { return m_path; }

    static constexpr int kSettleMs = 1000;
    // At most this many files per settle tick, the rest wait for the next one:
    // a folder of a thousand files moved in at once must not start a thousand
    // uploads in the same second.
    static constexpr int kPerTick = 4;

signals:
    void fileReady(const QString &absolutePath);

private:
    struct Stamp {
        qint64 size = -1;
        QDateTime modified;
        bool operator==(const Stamp &o) const { return size == o.size && modified == o.modified; }
    };
    void scan();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QString m_path;
    QSet<QString> m_known;           // in the folder already, or handed over
    QHash<QString, Stamp> m_pending; // new, waiting to hold still
};

#include "upload/FolderWatch.h"

#include <QDir>
#include <QFileInfo>

FolderWatch::FolderWatch(QObject *parent)
    : QObject(parent)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(kSettleMs);
    connect(&m_settle, &QTimer::timeout, this, &FolderWatch::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_settle.start(); });
}

bool FolderWatch::setPath(const QString &path)
{
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_settle.stop();
    m_known.clear();
    m_pending.clear();
    m_path.clear();
    if (path.isEmpty())
        return true;
    const QFileInfo info(path);
    if (!info.isDir())
        return false;
    const QString abs = info.absoluteFilePath();
    // Listed before the watch starts: a file landing in between is not in the
    // snapshot, so the next change event picks it up instead of losing it.
    const QStringList existing = QDir(abs).entryList(QDir::Files);
    if (!m_watcher.addPath(abs))
        return false;
    m_path = abs;
    m_known = QSet<QString>(existing.cbegin(), existing.cend());
    return true;
}

static bool isPartialDownload(const QString &name)
{
    for (const char *suffix : {".part", ".crdownload", ".download", ".tmp", "~"})
        if (name.endsWith(QLatin1String(suffix), Qt::CaseInsensitive))
            return true;
    return false;
}

void FolderWatch::scan()
{
    if (m_path.isEmpty())
        return;
    // Without QDir::Hidden dotfiles are not listed at all.
    const QFileInfoList files = QDir(m_path).entryInfoList(QDir::Files);
    QSet<QString> present;
    QStringList ready;
    bool recheck = false;
    for (const QFileInfo &fi : files) {
        const QString name = fi.fileName();
        present.insert(name);
        if (m_known.contains(name) || isPartialDownload(name))
            continue;
        const Stamp now{fi.size(), fi.lastModified()};
        const auto it = m_pending.constFind(name);
        if (it == m_pending.cend() || !(*it == now) || now.size == 0 || ready.size() == kPerTick) {
            m_pending.insert(name, now);
            // ponytail: an empty file does not keep the timer alive, it is
            // looked at again on the folder's next create, delete or rename.
            // A writer that creates a file and fills it seconds later in one
            // go waits for that event; poll if anyone hits it.
            recheck = recheck || now.size > 0;
            continue;
        }
        m_pending.remove(name);
        m_known.insert(name);
        ready << fi.absoluteFilePath();
    }
    // A name that left the folder is forgotten, so both sets stay bounded by
    // what is in it and a file moved out and back in counts as new.
    m_known.intersect(present);
    for (auto it = m_pending.begin(); it != m_pending.end();)
        it = present.contains(it.key()) ? std::next(it) : m_pending.erase(it);
    if (recheck)
        m_settle.start();
    // Emitted last: a receiver may call setPath() and reset the sets.
    for (const QString &path : std::as_const(ready))
        emit fileReady(path);
}

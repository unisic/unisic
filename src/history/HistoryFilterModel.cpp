#include "HistoryFilterModel.h"
#include "HistoryStore.h"
#include <QFileInfo>

HistoryFilterModel::HistoryFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    // dynamicSortFilter (on by default) re-tests a row on dataChanged, so an
    // entry that gets starred or gains an upload URL enters/leaves the
    // favourites/uploaded filters on its own.
    auto emitCount = [this] { emit countChanged(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, emitCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, emitCount);
    connect(this, &QAbstractItemModel::modelReset, this, emitCount);
    connect(this, &HistoryFilterModel::filterChanged, this, emitCount);
}

void HistoryFilterModel::setSearchText(const QString &t)
{
    if (m_searchText == t)
        return;
    m_searchText = t;
    invalidateFilter();
    emit filterChanged();
}

void HistoryFilterModel::setKindFilter(const QString &k)
{
    if (m_kindFilter == k)
        return;
    m_kindFilter = k;
    invalidateFilter();
    emit filterChanged();
}

void HistoryFilterModel::setFavoritesOnly(bool v)
{
    if (m_favoritesOnly == v)
        return;
    m_favoritesOnly = v;
    invalidateFilter();
    emit filterChanged();
}

void HistoryFilterModel::setUploadedOnly(bool v)
{
    if (m_uploadedOnly == v)
        return;
    m_uploadedOnly = v;
    invalidateFilter();
    emit filterChanged();
}

bool HistoryFilterModel::filtering() const
{
    return !m_searchText.isEmpty() || !m_kindFilter.isEmpty() || m_favoritesOnly || m_uploadedOnly;
}

QDateTime HistoryFilterModel::timestampAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return index(row, 0).data(HistoryStore::TimestampRole).toDateTime();
}

QVariantList HistoryFilterModel::entryIdsBetween(int fromRow, int toRow) const
{
    // Accepts the range in either direction: a shift-click anchor can sit below
    // or above the clicked row.
    int lo = qMax(0, qMin(fromRow, toRow));
    int hi = qMin(rowCount() - 1, qMax(fromRow, toRow));
    QVariantList ids;
    for (int r = lo; r <= hi; ++r)
        ids.append(index(r, 0).data(HistoryStore::EntryIdRole));
    return ids;
}

bool HistoryFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *src = sourceModel();
    if (!src)
        return false;
    const QModelIndex idx = src->index(sourceRow, 0, sourceParent);

    if (m_favoritesOnly && !idx.data(HistoryStore::FavoriteRole).toBool())
        return false;
    if (m_uploadedOnly && idx.data(HistoryStore::UrlRole).toString().isEmpty())
        return false;

    if (!m_kindFilter.isEmpty()
        && idx.data(HistoryStore::CategoryRole).toString() != m_kindFilter)
        return false;

    if (!m_searchText.isEmpty()) {
        // File NAME, not the whole path: the path's directories are the same for
        // every capture, so matching them would make most searches match all.
        const QString name = QFileInfo(idx.data(HistoryStore::FilePathRole).toString()).fileName();
        const QString url = idx.data(HistoryStore::UrlRole).toString();
        if (!name.contains(m_searchText, Qt::CaseInsensitive)
            && !url.contains(m_searchText, Qt::CaseInsensitive))
            return false;
    }
    return true;
}

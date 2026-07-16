#pragma once
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <qqmlregistration.h>

// Search/filter layer the History page puts in front of HistoryStore.
//
// The store stays a plain newest-first list (the capture pipeline, the capture
// cards and the smoke test all address it directly); everything the page's
// search box and filter chips do lives here. Rows the page hands back to the
// store are addressed by HistoryStore::EntryIdRole, never by row — a proxy row
// means a different entry after a filter switch.
class HistoryFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT

    // Matched case-insensitively against the capture's file name and its
    // upload URL. Empty = no text filter.
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    // Matched against HistoryStore::CategoryRole, so the values are the page's
    // chips: "" (all) | "image" | "gif" | "video" | "replay". A clip out of the
    // instant-replay ring is its own category, not a "video" — that is the one
    // thing the file itself cannot tell you.
    Q_PROPERTY(QString kindFilter READ kindFilter WRITE setKindFilter NOTIFY filterChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filterChanged)
    Q_PROPERTY(bool uploadedOnly READ uploadedOnly WRITE setUploadedOnly NOTIFY filterChanged)
    // Row count AFTER filtering (QSortFilterProxyModel has no count property).
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // True when any filter is narrowing the list — the page tells "no captures
    // yet" apart from "nothing matches" with it.
    Q_PROPERTY(bool filtering READ filtering NOTIFY filterChanged)

public:
    explicit HistoryFilterModel(QObject *parent = nullptr);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &t);
    QString kindFilter() const { return m_kindFilter; }
    void setKindFilter(const QString &k);
    bool favoritesOnly() const { return m_favoritesOnly; }
    void setFavoritesOnly(bool v);
    bool uploadedOnly() const { return m_uploadedOnly; }
    void setUploadedOnly(bool v);
    int count() const { return rowCount(); }
    bool filtering() const;

    // Timestamp of a visible row. The page's sticky date header needs the date
    // of whichever row is topmost right now — GridView::indexAt() gives the row,
    // but a view cannot read a model role without an instantiated delegate.
    Q_INVOKABLE QDateTime timestampAt(int row) const;
    // Entry ids of the rows that pass the filter — "select all", and (with a
    // row range) shift-click range selection. Rows outside the viewport have no
    // delegate, so the page cannot collect these itself.
    Q_INVOKABLE QVariantList entryIds() const { return entryIdsBetween(0, rowCount() - 1); }
    Q_INVOKABLE QVariantList entryIdsBetween(int fromRow, int toRow) const;

signals:
    void filterChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    QString m_kindFilter;
    bool m_favoritesOnly = false;
    bool m_uploadedOnly = false;
};

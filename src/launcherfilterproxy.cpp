#include "include/launcherfilterproxy.h"
#include "include/appitem.h"
#include "include/launchermodel.h"

#include <QStandardItemModel>

LauncherFilterProxy::LauncherFilterProxy(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setRecursiveFilteringEnabled(false);   // we handle category recursion below
}

void LauncherFilterProxy::setFilterText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_filterText) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    m_filterText = trimmed;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    m_filterText = trimmed;
    invalidateFilter();
#endif
}

bool LauncherFilterProxy::canDropMimeData(const QMimeData *data,
                                          Qt::DropAction action,
                                          int row, int column,
                                          const QModelIndex &parent) const
{
    if (!sourceModel())
        return false;
    return sourceModel()->canDropMimeData(
        data, action, row, column, mapToSource(parent));
}

bool LauncherFilterProxy::dropMimeData(const QMimeData *data,
                                       Qt::DropAction action,
                                       int row, int column,
                                       const QModelIndex &parent)
{
    if (!sourceModel())
        return false;
    return sourceModel()->dropMimeData(
        data, action, row, column, mapToSource(parent));
}

bool LauncherFilterProxy::filterAcceptsRow(int sourceRow,
                                           const QModelIndex &sourceParent) const
{
    if (m_filterText.isEmpty()) return true;

    auto *model = qobject_cast<QStandardItemModel *>(sourceModel());
    if (!model) return true;

    auto *item = model->itemFromIndex(model->index(sourceRow, 0, sourceParent));
    if (!item) return false;

    const int type = item->data(TypeRole).toInt();

    // Top-level category: accept if its own name matches, or if any child matches
    if (type == TypeCategory) {
        if (item->text().contains(m_filterText, Qt::CaseInsensitive))
            return true;
        for (int i = 0; i < item->rowCount(); ++i)
            if (itemMatches(item->child(i)))
                return true;
        return false;
    }

    // Child of a category: if the parent category name matches, accept everything
    if (sourceParent.isValid()) {
        auto *parent = model->itemFromIndex(sourceParent);
        if (parent && parent->data(TypeRole).toInt() == TypeCategory &&
            parent->text().contains(m_filterText, Qt::CaseInsensitive))
            return true;
    }

    return itemMatches(item);
}

bool LauncherFilterProxy::itemMatches(QStandardItem *item) const
{
    if (!item) return false;
    if (item->text().contains(m_filterText, Qt::CaseInsensitive))
        return true;
    if (auto *app = dynamic_cast<AppItem *>(item)) {
        if (app->binaryPath.contains(m_filterText, Qt::CaseInsensitive))
            return true;
        for (const QString &arg : app->arguments)
            if (arg.contains(m_filterText, Qt::CaseInsensitive))
                return true;
    }
    return false;
}
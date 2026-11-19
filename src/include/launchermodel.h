#ifndef LAUNCHERMODEL_H
#define LAUNCHERMODEL_H

#include <QStandardItemModel>
#include <QMimeData>

static constexpr int TypeRole     = Qt::UserRole + 1;
static constexpr int IconPathRole = Qt::UserRole + 2;
static constexpr int ExpandedRole = Qt::UserRole + 3;
enum ItemType { TypeCategory = 1, TypeApp = 2, TypeSeparator = 3 };

class LauncherModel : public QStandardItemModel
{
    Q_OBJECT
public:
    using QStandardItemModel::QStandardItemModel;

    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        m_dragged.clear();
        for (const QModelIndex &idx : indexes)
            m_dragged.append(QPersistentModelIndex(idx));
        return QStandardItemModel::mimeData(indexes);
    }

    bool canDropMimeData(const QMimeData *, Qt::DropAction action,
                         int, int, const QModelIndex &parent) const override
    {
        if (action != Qt::MoveAction || m_dragged.isEmpty())
            return false;

        QStandardItem *srcItem = itemFromIndex(m_dragged.first());
        if (!srcItem) return false;

        const int srcType = srcItem->data(TypeRole).toInt();
        QStandardItem *dstItem = parent.isValid() ? itemFromIndex(parent) : nullptr;
        if (parent.isValid() && !dstItem) return false;
        const int dstType = dstItem ? dstItem->data(TypeRole).toInt() : -1;

        if (srcType == TypeCategory)
            return !parent.isValid();                 // only at top level
        if (srcType == TypeApp || srcType == TypeSeparator)
            return dstType == TypeCategory;           // only into a category
        return false;
    }

    bool dropMimeData(const QMimeData *, Qt::DropAction action,
                      int row, int, const QModelIndex &parent) override
    {
        if (action != Qt::MoveAction || m_dragged.isEmpty())
            return false;

        const QPersistentModelIndex srcIdx = m_dragged.first();
        m_dragged.clear();
        if (!srcIdx.isValid()) return false;

        QStandardItem *srcItem = itemFromIndex(srcIdx);
        if (!srcItem) return false;

        // Reject dropping onto itself
        if (QModelIndex(srcIdx) == parent) return false;

        QStandardItem *srcParent = srcItem->parent();
        QStandardItem *dstParent = parent.isValid() ? itemFromIndex(parent) : nullptr;

        const int srcRow = srcParent ? srcItem->row() : srcIdx.row();
        int dstRow = row;
        if (dstRow < 0)
            dstRow = dstParent ? dstParent->rowCount() : rowCount();

        // Same-parent move: shift drop row if we removed something above it
        if (srcParent == dstParent && srcRow < dstRow)
            --dstRow;

        QStandardItem *taken = srcParent
            ? srcParent->takeRow(srcRow).first()
            : takeRow(srcRow).first();
        if (!taken) return false;

        if (dstParent) dstParent->insertRow(dstRow, taken);
        else            insertRow(dstRow, taken);

        emit itemMoved(taken);
        return true;
    }

signals:
    void itemMoved(QStandardItem *item);

private:
    mutable QList<QPersistentModelIndex> m_dragged;
};

#endif // LAUNCHERMODEL_H
#ifndef LAUNCHERFILTERPROXY_H
#define LAUNCHERFILTERPROXY_H

#include <QSortFilterProxyModel>
#include <QString>

class QStandardItem;
class QMimeData;

class LauncherFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit LauncherFilterProxy(QObject *parent = nullptr);

    void setFilterText(const QString &text);
    QString filterText() const { return m_filterText; }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                         int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                      int row, int column,
                      const QModelIndex &parent) override;

private:
    bool itemMatches(QStandardItem *item) const;
    QString m_filterText;
};

#endif // LAUNCHERFILTERPROXY_H
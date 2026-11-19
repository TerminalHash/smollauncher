#ifndef PRISMPICKERDIALOG_H
#define PRISMPICKERDIALOG_H

#include "prismimporter.h"

#include <QDialog>
#include <QList>
#include <QSet>

class QCheckBox;
class QLineEdit;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class PrismPickerDialog; }
QT_END_NAMESPACE

class PrismPickerDialog : public QDialog
{
    Q_OBJECT
public:
    PrismPickerDialog(const QList<PrismInstance> &instances,
                      const QSet<QString> &alreadyImported,
                      QWidget *parent = nullptr);
    ~PrismPickerDialog() override;

    QList<PrismInstance> selectedInstances() const;

private slots:
    void applyFilter();

private:
    void populate();

    Ui::PrismPickerDialog *ui = nullptr;
    QList<PrismInstance>  m_instances;
    QSet<QString>         m_alreadyImported;

    QCheckBox   *m_skipImported = nullptr;
    QLineEdit   *m_filter       = nullptr;
    QListWidget *m_list         = nullptr;
};

#endif // PRISMPICKERDIALOG_H

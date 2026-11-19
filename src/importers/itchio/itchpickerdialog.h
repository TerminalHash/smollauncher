#ifndef ITCHPICKERDIALOG_H
#define ITCHPICKERDIALOG_H

#include "itchimporter.h"

#include <QDialog>
#include <QList>
#include <QSet>
#include <QStringList>

class QCheckBox;
class QLineEdit;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class ItchPickerDialog; }
QT_END_NAMESPACE

class ItchPickerDialog : public QDialog
{
    Q_OBJECT
public:
    ItchPickerDialog(const QStringList &roots,
                     const QList<ItchGame> &games,
                     const QSet<QString> &alreadyImported,
                     QWidget *parent = nullptr);
    ~ItchPickerDialog() override;

    QList<ItchGame> selectedGames() const;

private slots:
    void applyFilter(const QString &text);

private:
    void populate();

    Ui::ItchPickerDialog *ui = nullptr;
    QStringList       m_roots;
    QList<ItchGame>   m_games;
    QSet<QString>     m_alreadyImported;

    QLineEdit   *m_filter       = nullptr;
    QCheckBox   *m_skipImported = nullptr;
    QListWidget *m_list         = nullptr;
};

#endif // ITCHPICKERDIALOG_H

#ifndef HEROICPICKERDIALOG_H
#define HEROICPICKERDIALOG_H

#include "heroicimporter.h"

#include <QDialog>
#include <QList>
#include <QSet>

class QCheckBox;
class QLineEdit;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class HeroicPickerDialog; }
QT_END_NAMESPACE

class HeroicPickerDialog : public QDialog
{
    Q_OBJECT
public:
    HeroicPickerDialog(const QString &sourceLabel,
                       const QList<HeroicGame> &games,
                       const QSet<QString> &alreadyImportedIds,
                       QWidget *parent = nullptr);
    ~HeroicPickerDialog() override;

    QList<HeroicGame> selectedGames() const;

private slots:
    void applyFilter(const QString &text);

private:
    void populate();

    Ui::HeroicPickerDialog *ui = nullptr;
    QString           m_sourceLabel;
    QList<HeroicGame> m_games;
    QSet<QString>     m_alreadyImported;

    QLineEdit   *m_filter       = nullptr;
    QCheckBox   *m_skipImported = nullptr;
    QListWidget *m_list         = nullptr;
};

#endif // HEROICPICKERDIALOG_H

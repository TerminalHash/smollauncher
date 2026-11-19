#ifndef RETROARCHPICKERDIALOG_H
#define RETROARCHPICKERDIALOG_H

#include "retroarchimporter.h"

#include <QDialog>
#include <QList>
#include <QSet>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class RetroArchPickerDialog; }
QT_END_NAMESPACE

class RetroArchPickerDialog : public QDialog
{
    Q_OBJECT
public:
    RetroArchPickerDialog(const QList<RetroArchGame> &games,
                          const QSet<QString> &alreadyImported,
                          QWidget *parent = nullptr);
    ~RetroArchPickerDialog() override;

    QList<RetroArchGame> selectedGames() const;

private slots:
    void applyFilter();

private:
    void populate();

    Ui::RetroArchPickerDialog *ui = nullptr;
    QList<RetroArchGame> m_games;
    QSet<QString>        m_alreadyImported;

    QComboBox   *m_playlistFilter = nullptr;
    QCheckBox   *m_skipImported   = nullptr;
    QLineEdit   *m_filter         = nullptr;
    QListWidget *m_list           = nullptr;
};

#endif // RETROARCHPICKERDIALOG_H

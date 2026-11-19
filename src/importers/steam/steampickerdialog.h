#ifndef STEAMPICKERDIALOG_H
#define STEAMPICKERDIALOG_H

#include "steamimporter.h"

#include <QDialog>
#include <QList>
#include <QSet>

class QCheckBox;
class QLineEdit;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class SteamPickerDialog; }
QT_END_NAMESPACE

class SteamPickerDialog : public QDialog
{
    Q_OBJECT
public:
    SteamPickerDialog(const QList<SteamGame> &games,
                      const QSet<QString> &alreadyImportedIds,
                      QWidget *parent = nullptr);
    ~SteamPickerDialog() override;

    QList<SteamGame> selectedGames() const;

private slots:
    void applyFilter(const QString &text);

private:
    void populate();

    Ui::SteamPickerDialog *ui = nullptr;
    QList<SteamGame> m_games;
    QSet<QString>    m_alreadyImported;

    QLineEdit   *m_filter       = nullptr;
    QCheckBox   *m_skipImported = nullptr;
    QListWidget *m_list         = nullptr;
};

#endif // STEAMPICKERDIALOG_H

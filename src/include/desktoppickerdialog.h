#ifndef DESKTOPPICKERDIALOG_H
#define DESKTOPPICKERDIALOG_H

#include "desktopentry.h"
#include <QDialog>

class QLineEdit;
class QListWidget;

QT_BEGIN_NAMESPACE
namespace Ui { class DesktopPickerDialog; }
QT_END_NAMESPACE

class DesktopPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DesktopPickerDialog(QWidget *parent = nullptr);
    ~DesktopPickerDialog() override;

    DesktopEntry selected() const { return m_selected; }

private slots:
    void applyFilter(const QString &text);
    void accept() override;

private:
    Ui::DesktopPickerDialog *ui = nullptr;
    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QList<DesktopEntry> m_entries;
    DesktopEntry m_selected;

    void populate();
};

#endif // DESKTOPPICKERDIALOG_H
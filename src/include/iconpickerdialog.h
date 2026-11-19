#ifndef ICONPICKERDIALOG_H
#define ICONPICKERDIALOG_H

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPushButton;

QT_BEGIN_NAMESPACE
namespace Ui { class IconPickerDialog; }
QT_END_NAMESPACE

class IconPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit IconPickerDialog(QWidget *parent = nullptr);
    ~IconPickerDialog() override;
    QString selectedIcon() const { return m_selected; }

private slots:
    void applyFilter(const QString &text);
    void accept() override;
    void browseFile();

private:
    Ui::IconPickerDialog *ui = nullptr;
    QLineEdit   *m_filter = nullptr;
    QListWidget *m_list   = nullptr;
    QPushButton *m_okBtn  = nullptr;
    QString      m_selected;
};

#endif // ICONPICKERDIALOG_H
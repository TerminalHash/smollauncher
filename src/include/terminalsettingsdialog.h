#ifndef TERMINALSETTINGSDIALOG_H
#define TERMINALSETTINGSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui { class TerminalSettingsDialog; }
QT_END_NAMESPACE

class TerminalSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TerminalSettingsDialog(QWidget *parent = nullptr);
    ~TerminalSettingsDialog() override;

public slots:
    void accept() override;

private:
    void updateCustomState();

    Ui::TerminalSettingsDialog *ui = nullptr;
};

#endif // TERMINALSETTINGSDIALOG_H

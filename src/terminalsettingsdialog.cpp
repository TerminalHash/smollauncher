#include "include/terminalsettingsdialog.h"
#include "include/terminalsettings.h"
#include "ui_terminalsettingsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardPaths>

TerminalSettingsDialog::TerminalSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::TerminalSettingsDialog)
{
    ui->setupUi(this);

    // Settings dialogs stay at their designed size, like the pickers.
    setFixedSize(size());

    const TerminalSettings::Config config = TerminalSettings::load();

    for (const QString &program : TerminalSettings::defaultTerminals())
        ui->terminalCombo->addItem(program, program);

    QString selected = config.program;
    if (selected.isEmpty()) {
        for (const QString &program : TerminalSettings::defaultTerminals()) {
            if (!QStandardPaths::findExecutable(program).isEmpty()) {
                selected = program;
                break;
            }
        }
    }
    int index = ui->terminalCombo->findData(selected);
    if (index < 0 && ui->terminalCombo->count() > 0)
        index = 0;
    ui->terminalCombo->setCurrentIndex(index);

    ui->customCheck->setChecked(config.custom);
    ui->binField->setText(config.customBin);
    ui->argsField->setText(config.customArgs);

    connect(ui->customCheck, &QCheckBox::toggled,
            this, [this]{ updateCustomState(); });
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &TerminalSettingsDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);

    updateCustomState();
}

TerminalSettingsDialog::~TerminalSettingsDialog()
{
    delete ui;
}

void TerminalSettingsDialog::updateCustomState()
{
    const bool custom = ui->customCheck->isChecked();
    ui->terminalCombo->setEnabled(!custom);
    ui->binField->setEnabled(custom);
    ui->argsField->setEnabled(custom);
}

void TerminalSettingsDialog::accept()
{
    TerminalSettings::Config config;
    config.program    = ui->terminalCombo->currentData().toString();
    config.custom     = ui->customCheck->isChecked();
    config.customBin  = ui->binField->text().trimmed();
    config.customArgs = ui->argsField->text().trimmed();

    if (config.custom && config.customBin.isEmpty()) {
        QMessageBox::warning(this, tr("Terminal emulator"),
                             tr("Choose a custom terminal binary first."));
        ui->binField->setFocus();
        return;
    }

    TerminalSettings::save(config);
    QDialog::accept();
}

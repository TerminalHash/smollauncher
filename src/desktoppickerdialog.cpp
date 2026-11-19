#include "include/desktoppickerdialog.h"
#include "ui_desktoppickerdialog.h"

#include <QDialogButtonBox>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>

DesktopPickerDialog::DesktopPickerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::DesktopPickerDialog)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());

    m_filter = ui->filterEdit;
    m_list   = ui->entryList;

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged,
            this, &DesktopPickerDialog::applyFilter);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *it){
        if (it && !it->isHidden()) accept();
    });

    populate();
    applyFilter(QString());
    m_filter->setFocus();
}

DesktopPickerDialog::~DesktopPickerDialog()
{
    delete ui;
}

void DesktopPickerDialog::populate()
{
    m_entries = DesktopEntryScanner::scan();

    for (int i = 0; i < m_entries.size(); ++i) {
        const DesktopEntry &e = m_entries[i];

        auto *item = new QListWidgetItem(e.name);
        const QString iconPath = DesktopEntryScanner::resolveIconName(e.icon);
        if (!iconPath.isEmpty())
            item->setIcon(QIcon(iconPath));
        item->setToolTip(e.comment.isEmpty() ? e.desktopFilePath : e.comment);
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
    }
}

void DesktopPickerDialog::applyFilter(const QString &text)
{
    const QString t = text.trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        it->setHidden(!t.isEmpty() &&
                      !it->text().contains(t, Qt::CaseInsensitive));
    }
    // Select first visible
    for (int i = 0; i < m_list->count(); ++i) {
        if (!m_list->item(i)->isHidden()) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void DesktopPickerDialog::accept()
{
    auto *it = m_list->currentItem();
    if (!it || it->isHidden()) return;
    const int idx = it->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_entries.size()) return;
    m_selected = m_entries[idx];
    QDialog::accept();
}

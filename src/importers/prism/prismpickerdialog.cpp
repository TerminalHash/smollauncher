#include "prismpickerdialog.h"
#include "ui_prismpickerdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

PrismPickerDialog::PrismPickerDialog(
        const QList<PrismInstance> &instances,
        const QSet<QString> &alreadyImported,
        QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::PrismPickerDialog)
    , m_instances(instances)
    , m_alreadyImported(alreadyImported)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());

    m_skipImported = ui->skipImportedCheck;
    m_filter       = ui->filterEdit;
    m_list         = ui->instanceList;

    ui->hintLabel->setText(
        tr("%1 instance(s) found. Select which to add:")
            .arg(m_instances.size()));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged,
            this, &PrismPickerDialog::applyFilter);
    connect(m_skipImported, &QCheckBox::toggled,
            this, [this](bool){ applyFilter(); });
    connect(ui->selectAllButton, &QPushButton::clicked, this, [this]{
        for (int i = 0; i < m_list->count(); ++i) {
            auto *it = m_list->item(i);
            if (!it->isHidden())
                it->setCheckState(Qt::Checked);
        }
    });
    connect(ui->selectNoneButton, &QPushButton::clicked, this, [this]{
        for (int i = 0; i < m_list->count(); ++i)
            m_list->item(i)->setCheckState(Qt::Unchecked);
    });

    populate();
    applyFilter();
    m_filter->setFocus();
}

PrismPickerDialog::~PrismPickerDialog()
{
    delete ui;
}

void PrismPickerDialog::populate()
{
    for (int i = 0; i < m_instances.size(); ++i) {
        const PrismInstance &inst = m_instances.at(i);

        auto *item = new QListWidgetItem(inst.displayName());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, i);

        if (!inst.iconPath.isEmpty())
            item->setIcon(QIcon(inst.iconPath));

        item->setToolTip(inst.instancePath);

        if (m_alreadyImported.contains(inst.desktopFileId()))
            item->setText(item->text()
                          + QLatin1String("  (already imported)"));

        m_list->addItem(item);
    }
}

void PrismPickerDialog::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    const bool hideImported = m_skipImported->isChecked();

    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_instances.size()) continue;
        const PrismInstance &inst = m_instances.at(idx);

        bool visible = text.isEmpty()
            || inst.name.contains(text, Qt::CaseInsensitive)
            || inst.version.contains(text, Qt::CaseInsensitive);

        if (visible && hideImported
            && m_alreadyImported.contains(inst.desktopFileId()))
            visible = false;

        item->setHidden(!visible);
    }
}

QList<PrismInstance> PrismPickerDialog::selectedInstances() const
{
    QList<PrismInstance> out;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_instances.size())
            out.append(m_instances.at(idx));
    }
    return out;
}

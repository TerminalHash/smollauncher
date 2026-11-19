#include "heroicpickerdialog.h"
#include "ui_heroicpickerdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

HeroicPickerDialog::HeroicPickerDialog(const QString &sourceLabel,
                                       const QList<HeroicGame> &games,
                                       const QSet<QString> &alreadyImportedIds,
                                       QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::HeroicPickerDialog)
    , m_sourceLabel(sourceLabel)
    , m_games(games)
    , m_alreadyImported(alreadyImportedIds)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());
    setWindowTitle(tr("Import from %1").arg(m_sourceLabel));

    m_skipImported = ui->skipImportedCheck;
    m_filter       = ui->filterEdit;
    m_list         = ui->gameList;

    ui->hintLabel->setText(
        tr("%1 installed game(s) found. Select which to add:")
            .arg(m_games.size()));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged,
            this, &HeroicPickerDialog::applyFilter);
    connect(m_skipImported, &QCheckBox::toggled, this, [this]{
        applyFilter(m_filter->text());
    });
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
    applyFilter(QString());
    m_filter->setFocus();
}

HeroicPickerDialog::~HeroicPickerDialog()
{
    delete ui;
}

void HeroicPickerDialog::populate()
{
    for (int i = 0; i < m_games.size(); ++i) {
        const HeroicGame &g = m_games.at(i);

        auto *item = new QListWidgetItem(g.title);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, i);

        if (!g.iconPath.isEmpty())
            item->setIcon(QIcon(g.iconPath));

        if (!g.warning.isEmpty())
            item->setToolTip(g.warning);

        if (m_alreadyImported.contains(g.desktopFileId()))
            item->setText(g.title + QLatin1String("  (already imported)"));

        m_list->addItem(item);
    }
}

void HeroicPickerDialog::applyFilter(const QString &text)
{
    const QString t = text.trimmed();
    const bool hideImported = m_skipImported->isChecked();

    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_games.size()) continue;
        const HeroicGame &g = m_games.at(idx);

        const bool imported = m_alreadyImported.contains(g.desktopFileId());

        bool visible = true;
        if (!t.isEmpty() && !g.title.contains(t, Qt::CaseInsensitive))
            visible = false;
        if (hideImported && imported)
            visible = false;

        item->setHidden(!visible);
    }
}

QList<HeroicGame> HeroicPickerDialog::selectedGames() const
{
    QList<HeroicGame> out;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_games.size())
            out.append(m_games.at(idx));
    }
    return out;
}

#include "steampickerdialog.h"
#include "ui_steampickerdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

SteamPickerDialog::SteamPickerDialog(const QList<SteamGame> &games,
                                     const QSet<QString> &alreadyImportedIds,
                                     QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SteamPickerDialog)
    , m_games(games)
    , m_alreadyImported(alreadyImportedIds)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());

    m_skipImported = ui->skipImportedCheck;
    m_filter       = ui->filterEdit;
    m_list         = ui->gameList;

    ui->hintLabel->setText(
        tr("%1 installed Steam game(s) found. Select which to add:")
            .arg(m_games.size()));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged,
            this, &SteamPickerDialog::applyFilter);
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

SteamPickerDialog::~SteamPickerDialog()
{
    delete ui;
}

void SteamPickerDialog::populate()
{
    for (int i = 0; i < m_games.size(); ++i) {
        const SteamGame &g = m_games.at(i);

        auto *item = new QListWidgetItem(g.name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, i);

        if (!g.iconPath.isEmpty())
            item->setIcon(QIcon(g.iconPath));

        if (m_alreadyImported.contains(QStringLiteral("steam-") + g.appId))
            item->setText(g.name + QLatin1String("  (already imported)"));

        m_list->addItem(item);
    }
}

void SteamPickerDialog::applyFilter(const QString &text)
{
    const QString t = text.trimmed();
    const bool hideImported = m_skipImported->isChecked();

    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_games.size()) continue;
        const SteamGame &g = m_games.at(idx);

        const bool imported = m_alreadyImported.contains(
            QStringLiteral("steam-") + g.appId);

        bool visible = true;
        if (!t.isEmpty() && !g.name.contains(t, Qt::CaseInsensitive))
            visible = false;
        if (hideImported && imported)
            visible = false;

        item->setHidden(!visible);
    }
}

QList<SteamGame> SteamPickerDialog::selectedGames() const
{
    QList<SteamGame> out;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_games.size())
            out.append(m_games.at(idx));
    }
    return out;
}

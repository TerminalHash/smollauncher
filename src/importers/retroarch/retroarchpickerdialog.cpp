#include "retroarchpickerdialog.h"
#include "ui_retroarchpickerdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

RetroArchPickerDialog::RetroArchPickerDialog(
        const QList<RetroArchGame> &games,
        const QSet<QString> &alreadyImported,
        QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RetroArchPickerDialog)
    , m_games(games)
    , m_alreadyImported(alreadyImported)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());

    m_playlistFilter = ui->playlistCombo;
    m_skipImported   = ui->skipImportedCheck;
    m_filter         = ui->filterEdit;
    m_list           = ui->gameList;

    ui->hintLabel->setText(
        tr("%1 ROM(s) across all playlists. Select which to add:")
            .arg(m_games.size()));

    m_playlistFilter->addItem(tr("All playlists"), QString());
    {
        QSet<QString> seen;
        for (const auto &g : m_games)
            seen.insert(g.playlistName);
        QStringList sorted = seen.values();
        sorted.sort();
        for (const QString &p : sorted)
            m_playlistFilter->addItem(p, p);
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged,
            this, &RetroArchPickerDialog::applyFilter);
    connect(m_playlistFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ applyFilter(); });
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

RetroArchPickerDialog::~RetroArchPickerDialog()
{
    delete ui;
}

void RetroArchPickerDialog::populate()
{
    for (int i = 0; i < m_games.size(); ++i) {
        const RetroArchGame &g = m_games.at(i);

        auto *item = new QListWidgetItem(
            QStringLiteral("%1    [%2]").arg(g.romTitle, g.playlistName));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, i);

        QString tip = g.romPath;
        if (!g.coreName.isEmpty())
            tip += QStringLiteral("\nCore: %1").arg(g.coreName);
        if (!g.warning.isEmpty())
            tip += QStringLiteral("\n⚠ %1").arg(g.warning);
        item->setToolTip(tip);

        if (m_alreadyImported.contains(g.desktopFileId()))
            item->setText(item->text()
                          + QLatin1String("  (already imported)"));

        m_list->addItem(item);
    }
}

void RetroArchPickerDialog::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    const QString playlist = m_playlistFilter->currentData().toString();
    const bool hideImported = m_skipImported->isChecked();

    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_games.size()) continue;
        const RetroArchGame &g = m_games.at(idx);

        bool visible = true;

        if (!playlist.isEmpty() && g.playlistName != playlist)
            visible = false;

        if (visible && !text.isEmpty()) {
            const bool match =
                g.romTitle.contains(text, Qt::CaseInsensitive)
                || g.playlistName.contains(text, Qt::CaseInsensitive);
            if (!match) visible = false;
        }

        if (visible && hideImported
            && m_alreadyImported.contains(g.desktopFileId()))
            visible = false;

        item->setHidden(!visible);
    }
}

QList<RetroArchGame> RetroArchPickerDialog::selectedGames() const
{
    QList<RetroArchGame> out;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_games.size())
            out.append(m_games.at(idx));
    }
    return out;
}

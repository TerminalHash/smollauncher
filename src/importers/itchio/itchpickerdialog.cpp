#include "itchpickerdialog.h"
#include "ui_itchpickerdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

ItchPickerDialog::ItchPickerDialog(const QStringList &roots,
                                   const QList<ItchGame> &games,
                                   const QSet<QString> &alreadyImported,
                                   QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ItchPickerDialog)
    , m_roots(roots)
    , m_games(games)
    , m_alreadyImported(alreadyImported)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());

    m_skipImported = ui->skipImportedCheck;
    m_filter       = ui->filterEdit;
    m_list         = ui->gameList;

    ui->rootsLabel->setText(
        tr("Scanning: %1").arg(m_roots.join(QStringLiteral(", "))));
    QFont f = ui->rootsLabel->font();
    f.setPointSize(f.pointSize() - 1);
    ui->rootsLabel->setFont(f);

    ui->hintLabel->setText(
        tr("%1 installed game(s) found. Select which to add:")
            .arg(m_games.size()));

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_filter, &QLineEdit::textChanged,
            this, &ItchPickerDialog::applyFilter);
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

ItchPickerDialog::~ItchPickerDialog()
{
    delete ui;
}

void ItchPickerDialog::populate()
{
    for (int i = 0; i < m_games.size(); ++i) {
        const ItchGame &g = m_games.at(i);

        QString label = g.title;
        QStringList notes;
        if (g.windowsOnly)
            notes << tr("Windows");
        if (m_alreadyImported.contains(g.desktopFileId()))
            notes << tr("already imported");
        if (!notes.isEmpty())
            label += QStringLiteral("  (%1)").arg(notes.join(QStringLiteral(", ")));

        auto *item = new QListWidgetItem(label);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, i);

        if (!g.iconPath.isEmpty())
            item->setIcon(QIcon(g.iconPath));

        if (g.windowsOnly) {
            item->setToolTip(tr("Windows build. It will be launched through Wine."));
        } else if (!g.warning.isEmpty()) {
            item->setToolTip(g.warning);
        }

        m_list->addItem(item);
    }
}

void ItchPickerDialog::applyFilter(const QString &text)
{
    const QString t = text.trimmed();
    const bool hideImported = m_skipImported->isChecked();

    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_games.size()) continue;
        const ItchGame &g = m_games.at(idx);

        const bool imported = m_alreadyImported.contains(g.desktopFileId());

        bool visible = true;
        if (!t.isEmpty() &&
            !g.title.contains(t, Qt::CaseInsensitive) &&
            !g.slug.contains(t, Qt::CaseInsensitive))
            visible = false;
        if (hideImported && imported)
            visible = false;

        item->setHidden(!visible);
    }
}

QList<ItchGame> ItchPickerDialog::selectedGames() const
{
    QList<ItchGame> out;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->checkState() != Qt::Checked) continue;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_games.size())
            out.append(m_games.at(idx));
    }
    return out;
}

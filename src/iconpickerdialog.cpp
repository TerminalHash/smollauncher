#include "include/iconpickerdialog.h"
#include "ui_iconpickerdialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

// Larger is better: scalable wins over 512x512, which wins over 48x48, etc.
int sizeRank(const QString &sizeDir)
{
    if (sizeDir == QLatin1String("scalable"))
        return 100000;
    static const QRegularExpression re(QStringLiteral("^(\\d+)x\\1$"));
    const auto m = re.match(sizeDir);
    if (m.hasMatch())
        return m.captured(1).toInt();
    return 0;
}

// Walk every theme directory, dedupe by icon name, keep the highest-res
// path per name. Result: name → best file path.
QHash<QString, QString> scanIcons()
{
    QHash<QString, QString> result;
    QHash<QString, int>     ranks;

    QStringList bases;
    for (const auto &d : QStandardPaths::standardLocations(
             QStandardPaths::GenericDataLocation)) {
        bases << (d.endsWith(QLatin1String("/icons")) ? d : d + "/icons");
    }
    bases << QDir::homePath() + "/.icons"
          << "/usr/local/share/icons"
          << "/usr/share/icons";
    bases.removeDuplicates();

    for (const QString &base : bases) {
        QDir baseDir(base);
        if (!baseDir.exists()) continue;

        for (const QString &theme : baseDir.entryList(
                 QDir::Dirs | QDir::NoDotAndDotDot)) {
            // Skip this whole `for (theme)` block unless theme matches the current one
            static const QString currentTheme = QIcon::themeName();  // "Adwaita" etc.
            if (!currentTheme.isEmpty() && theme != currentTheme && theme != QStringLiteral("hicolor")) continue;

            QDir themeDir(base + '/' + theme);
            for (const QString &size : themeDir.entryList(
                     QDir::Dirs | QDir::NoDotAndDotDot)) {
                const int rank = sizeRank(size);
                if (rank == 0) continue;    // skip unknown subdirs

                QDir sizeDir(themeDir.absoluteFilePath(size));
                for (const QString &cat : sizeDir.entryList(
                         QDir::Dirs | QDir::NoDotAndDotDot)) {
                    QDir catDir(sizeDir.absoluteFilePath(cat));
                    if (!catDir.exists()) continue;

                    const auto files = catDir.entryList(
                        {"*.png", "*.svg", "*.xpm"}, QDir::Files);
                    for (const QString &f : files) {
                        const QString name = QFileInfo(f).completeBaseName();
                        if (rank > ranks.value(name, -1)) {
                            ranks.insert(name, rank);
                            result.insert(name, catDir.absoluteFilePath(f));
                        }
                    }
                }
            }
        }
    }
    return result;
}

} // namespace

IconPickerDialog::IconPickerDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::IconPickerDialog)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());

    m_filter = ui->filterEdit;
    m_list   = ui->iconList;
    m_okBtn  = ui->buttonBox->button(QDialogButtonBox::Ok);
    m_okBtn->setEnabled(false);

    auto *browseBtn = ui->buttonBox->addButton(
        tr("Browse file…"), QDialogButtonBox::ActionRole);

    connect(m_filter, &QLineEdit::textChanged,
            this, &IconPickerDialog::applyFilter);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked,
            this, &IconPickerDialog::browseFile);
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]{
        m_okBtn->setEnabled(!m_list->selectedItems().isEmpty());
    });
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *it){ if (it) accept(); });

    // Scan (cached across openings — first open ~300 ms, subsequent instant)
    static const QHash<QString, QString> icons = scanIcons();

    if (icons.isEmpty()) {
        auto *msg = new QListWidgetItem(
            tr("No themed icons found. Use \"Browse file…\"."));
        msg->setFlags(Qt::NoItemFlags);
        m_list->addItem(msg);
    } else {
        QStringList names = icons.keys();
        names.sort(Qt::CaseInsensitive);
        for (const QString &name : names) {
            const QString path = icons.value(name);
            auto *item = new QListWidgetItem(QIcon(path), name);
            item->setToolTip(QStringLiteral("%1\n%2").arg(name, path));
            item->setData(Qt::UserRole, path);
            item->setTextAlignment(Qt::AlignHCenter);
            m_list->addItem(item);
        }
    }

    m_filter->setFocus();
}

IconPickerDialog::~IconPickerDialog()
{
    delete ui;
}

void IconPickerDialog::applyFilter(const QString &text)
{
    const QString t = text.trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        item->setHidden(!t.isEmpty() &&
                        !item->text().contains(t, Qt::CaseInsensitive));
    }
}

void IconPickerDialog::accept()
{
    auto *item = m_list->currentItem();
    if (item && !item->isHidden() && (item->flags() & Qt::ItemIsSelectable)) {
        m_selected = item->data(Qt::UserRole).toString();
        QDialog::accept();
    }
}

void IconPickerDialog::browseFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose icon file"), QString(),
        tr("Images (*.png *.svg *.xpm *.ico);;All files (*)"));
    if (path.isEmpty()) return;
    m_selected = path;
    QDialog::accept();
}

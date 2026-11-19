#include "include/aboutdialog.h"
#include "ui_aboutdialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>

// ─── Edit these constants to personalise ────────────────────────────────
namespace {
constexpr const char *kIconPath      = ":/icons/graphic/sl_icon.svg";
constexpr const char *kAuthorName    = "TerminalHash";
constexpr const char *kAuthorEmail   = "lyashuk.voxx@gmail.com";
constexpr const char *kLicenseName   = "GPLv3";
constexpr const char *kLicenseUrl    = "https://www.gnu.org/licenses/gpl-3.0.en.html";
constexpr const char *kCopyrightLine = "Copyright © 2026 TerminalHash";
}
// ────────────────────────────────────────────────────────────────────────

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    // Keep the dialog at its designed size; these pickers are not resizable.
    setFixedSize(size());
    setWindowIcon(QIcon(QString::fromUtf8(kIconPath)));

    ui->iconLabel->setPixmap(
        QIcon(QString::fromUtf8(kIconPath)).pixmap(96, 96));

    // Title stands out a little from the rest of the text.
    QFont tf = ui->titleLabel->font();
    tf.setPointSize(tf.pointSize() + 4);
    tf.setBold(true);
    ui->titleLabel->setFont(tf);

    // Dim the "Built with Qt" line.
    QPalette pal = ui->builtWithLabel->palette();
    pal.setColor(QPalette::WindowText,
                 pal.color(QPalette::Disabled, QPalette::WindowText));
    ui->builtWithLabel->setPalette(pal);

    m_aboutQtBtn = ui->buttonBox->addButton(
        tr("About Qt"), QDialogButtonBox::HelpRole);
    m_closeBtn = ui->buttonBox->button(QDialogButtonBox::Close);

    connect(m_aboutQtBtn, &QPushButton::clicked, this, [this]{
        QMessageBox::aboutQt(this);
    });
    if (m_closeBtn)
        connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    retranslate();
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

void AboutDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslate();
    QDialog::changeEvent(event);
}

void AboutDialog::retranslate()
{
    const QString appName = QCoreApplication::applicationName();
    const QString version = QCoreApplication::applicationVersion();

    ui->titleLabel->setText(QStringLiteral(
        "%1 <span style=\"font-weight:normal; color:gray;\">%2</span>")
        .arg(appName, version));

    ui->descriptionLabel->setText(
        tr("Lightweight application launcher — no extra tools, "
           "libraries, or unnecessary complexity."));

    ui->authorLabel->setText(
        tr("Author: %1 &lt;<a href=\"mailto:%2\">%2</a>&gt;")
        .arg(QString::fromUtf8(kAuthorName),
             QString::fromUtf8(kAuthorEmail)));

    ui->licenseLabel->setText(
        tr("License: <a href=\"%1\">%2</a> · %3")
        .arg(QString::fromUtf8(kLicenseUrl),
             QString::fromUtf8(kLicenseName),
             QString::fromUtf8(kCopyrightLine)));

    ui->builtWithLabel->setText(
        tr("Built with Qt %1").arg(QStringLiteral(QT_VERSION_STR)));

    m_aboutQtBtn->setText(tr("About Qt"));
    if (m_closeBtn)
        m_closeBtn->setText(tr("Close"));
}

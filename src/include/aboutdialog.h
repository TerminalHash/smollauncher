#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

class QPushButton;

QT_BEGIN_NAMESPACE
namespace Ui { class AboutDialog; }
QT_END_NAMESPACE

class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override;

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslate();

    Ui::AboutDialog *ui = nullptr;
    QPushButton *m_aboutQtBtn = nullptr;
    QPushButton *m_closeBtn   = nullptr;
};

#endif // ABOUTDIALOG_H

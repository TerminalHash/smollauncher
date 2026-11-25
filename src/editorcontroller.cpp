#include "editorcontroller.h"
#include "include/mainwindow.h"
#include "include/appitem.h"
#include "include/commandline.h"
#include "include/debug.h"
#include "include/iconpickerdialog.h"
#include "ui_smollauncher_UI.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidgetItem>

namespace {

QString itemDescription(const QStandardItem *item)
{
    if (!item)
        return QStringLiteral("null");
    return QStringLiteral("type=%1 text=\"%2\" ptr=%3")
        .arg(item->data(TypeRole).toInt())
        .arg(item->text())
        .arg(static_cast<qulonglong>(reinterpret_cast<quintptr>(item)));
}

// Marks item/model updates that are not user edits as non-dirtying.
struct DirtySuppressor
{
    explicit DirtySuppressor(int &counter) : m_counter(counter) { ++m_counter; }
    ~DirtySuppressor() { --m_counter; }
    int &m_counter;
};

} // namespace

EditorController::EditorController(MainWindow *window)
    : m_w(window)
{
}

void EditorController::applyMode(int type)
{
    const bool isApp = (type == TypeApp);
    const bool isCat = (type == TypeCategory);

    // Categories use their own dedicated page.
    if (isCat) {
        m_w->ui->editorStack->setCurrentWidget(m_w->ui->categoryEditorPage);
        return;
    }

    // Separators and empty selections have nothing to configure.
    if (!isApp) {
        m_w->ui->editorStack->setCurrentWidget(m_w->ui->nothingPage);
        return;
    }

    m_w->ui->editorStack->setCurrentWidget(m_w->ui->editorPage);
    m_w->ui->tabWidget->setEnabled(true);
    m_w->ui->tabWidget->setTabVisible(1, true);
    m_w->ui->tabWidget->setTabVisible(2, true);

    if (m_w->m_currentItem)
        updateScriptState();

    m_w->ui->AppNameLabel->setText(MainWindow::tr("Name of application"));
    m_w->ui->IconLabel->setText(MainWindow::tr("Application icon"));
}

void EditorController::loadItem(QStandardItem *item)
{
    Debug::detail(QStringLiteral("loadItemToEditor: %1").arg(itemDescription(item)));

    const int type = item->data(TypeRole).toInt();

    if (type == TypeCategory) {
        QSignalBlocker b1(m_w->ui->categoryNameField),
                       b2(m_w->ui->categoryIconField);

        m_w->ui->categoryNameField->setText(item->text());
        m_w->ui->categoryIconField->setText(
            item->data(IconPathRole).toString());
        return;
    }

    auto *app = static_cast<AppItem *>(item);

    QSignalBlocker b1(m_w->ui->AppNameField), b2(m_w->ui->ArgField), b3(m_w->ui->IconField),
                   b4(m_w->ui->WorkDirField), b5(m_w->ui->BinField), b6(m_w->ui->isTerminalChk),
                   b7(m_w->ui->isDebugChk), b8(m_w->ui->isSingleChk), b9(m_w->ui->envVars);

    m_w->ui->AppNameField->setText(app->text());
    m_w->ui->ArgField->setText(CommandLine::joinArgs(app->arguments));
    m_w->ui->IconField->setText(app->iconPath);
    m_w->ui->WorkDirField->setText(app->workDir);
    m_w->ui->BinField->setText(app->binaryPath);
    m_w->ui->isTerminalChk->setChecked(app->launchInTerminal);
    m_w->ui->isDebugChk->setChecked(app->launchWithDebugger);
    m_w->ui->isSingleChk->setChecked(app->singleInstance);

    m_w->ui->envVars->setRowCount(0);
    for (auto it = app->env.constBegin(); it != app->env.constEnd(); ++it) {
        int row = m_w->ui->envVars->rowCount();
        m_w->ui->envVars->insertRow(row);
        m_w->ui->envVars->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_w->ui->envVars->setItem(row, 1, new QTableWidgetItem(it.value()));
    }

    // Script → terminal checkbox locked on
    m_w->ui->isTerminalChk->setEnabled(!app->isScript);
    if (app->isScript)
        m_w->ui->isTerminalChk->setChecked(true);

    Debug::detail(QStringLiteral("loadItemToEditor done: editor bin=\"%1\" workDir=\"%2\" icon=\"%3\"")
        .arg(m_w->ui->BinField->text())
        .arg(m_w->ui->WorkDirField->text())
        .arg(m_w->ui->IconField->text()));
}

void EditorController::clear()
{
    Debug::detail(QStringLiteral("clearEditor: current=%1 editor{name=\"%2\" bin=\"%3\" workDir=\"%4\" icon=\"%5\"}")
        .arg(itemDescription(m_w->m_currentItem))
        .arg(m_w->ui->AppNameField->text())
        .arg(m_w->ui->BinField->text())
        .arg(m_w->ui->WorkDirField->text())
        .arg(m_w->ui->IconField->text()));

    m_w->m_currentItem = nullptr;
    applyMode(-1);                  // show the empty page

    QSignalBlocker b1(m_w->ui->AppNameField), b2(m_w->ui->ArgField), b3(m_w->ui->IconField),
                   b4(m_w->ui->WorkDirField), b5(m_w->ui->BinField), b6(m_w->ui->isTerminalChk),
                   b7(m_w->ui->isDebugChk), b8(m_w->ui->isSingleChk), b9(m_w->ui->envVars),
                   b10(m_w->ui->categoryNameField), b11(m_w->ui->categoryIconField);

    m_w->ui->AppNameField->clear();
    m_w->ui->ArgField->clear();
    m_w->ui->IconField->clear();
    m_w->ui->WorkDirField->clear();
    m_w->ui->BinField->clear();
    m_w->ui->isTerminalChk->setChecked(false);
    m_w->ui->isDebugChk->setChecked(false);
    m_w->ui->isSingleChk->setChecked(true);
    m_w->ui->envVars->setRowCount(0);
    m_w->ui->categoryNameField->clear();
    m_w->ui->categoryIconField->clear();
}

void EditorController::save(const QString &reason, bool markDirty)
{
    if (Debug::enabled()) {
        QString line = QStringLiteral("saveEditor(%1): current=%2")
            .arg(reason.isEmpty() ? QStringLiteral("-") : reason)
            .arg(itemDescription(m_w->m_currentItem));
        line += QStringLiteral(" fields{name=\"%1\" bin=\"%2\" workDir=\"%3\" icon=\"%4\" args=\"%5\"}")
            .arg(m_w->ui->AppNameField->text())
            .arg(m_w->ui->BinField->text())
            .arg(m_w->ui->WorkDirField->text())
            .arg(m_w->ui->IconField->text())
            .arg(m_w->ui->ArgField->text());
        Debug::detail(line);
    }

    if (!m_w->m_currentItem) return;
    const int type = m_w->m_currentItem->data(TypeRole).toInt();
    if (type != TypeCategory && type != TypeApp) return;

    // Model updates below are a mirror of the editor, not a separate edit.
    DirtySuppressor suppress(m_w->m_dirtySuppress);

    if (type == TypeCategory) {
        const QString name = m_w->ui->categoryNameField->text();
        const QString icon = m_w->ui->categoryIconField->text();
        m_w->m_currentItem->setText(name);
        m_w->m_currentItem->setData(icon, IconPathRole);
        m_w->applyCategoryIcon(m_w->m_currentItem);
        Debug::detail(QStringLiteral("saveEditor(%1) saved category: %2")
            .arg(reason, itemDescription(m_w->m_currentItem)));
        if (markDirty) m_w->setDirty();
        return;
    }

    auto *app = static_cast<AppItem *>(m_w->m_currentItem);

    // Read every widget before touching the model. Item changes can make the
    // view re-evaluate selection and clear the editor, and reading afterwards
    // would then write those cleared values back over the item.
    const QString name       = m_w->ui->AppNameField->text();
    const QString args       = m_w->ui->ArgField->text();
    const QString iconPath   = m_w->ui->IconField->text();
    const QString workDir    = m_w->ui->WorkDirField->text();
    const QString binaryPath = m_w->ui->BinField->text();
    const bool debugger      = m_w->ui->isDebugChk->isChecked();
    const bool single        = m_w->ui->isSingleChk->isChecked();
    const bool terminal      = m_w->ui->isTerminalChk->isChecked();

    QMap<QString, QString> env;
    for (int r = 0; r < m_w->ui->envVars->rowCount(); ++r) {
        auto *k = m_w->ui->envVars->item(r, 0);
        auto *v = m_w->ui->envVars->item(r, 1);
        if (k && !k->text().isEmpty())
            env.insert(k->text(), v ? v->text() : QString());
    }

    app->setText(name);
    app->arguments          = CommandLine::splitArgs(args);
    app->iconPath           = iconPath;
    app->workDir            = workDir;
    app->binaryPath         = binaryPath;
    app->launchWithDebugger = debugger;
    app->singleInstance     = single;

    // launchInTerminal: scripts are forced on
    app->isScript = MainWindow::isScriptFile(app->binaryPath);
    app->launchInTerminal = app->isScript ? true : terminal;

    app->env = env;

    // Icon
    if (!app->iconPath.isEmpty())
        app->setIcon(QIcon(app->iconPath));
    else
        app->setIcon(QIcon::fromTheme(QStringLiteral("application-x-executable")));

    Debug::detail(QStringLiteral("saveEditor(%1) saved app: binary=\"%2\" workDir=\"%3\" icon=\"%4\" args=%5")
        .arg(reason)
        .arg(app->binaryPath)
        .arg(app->workDir)
        .arg(app->iconPath)
        .arg(app->arguments.size()));

    updateScriptState();
    m_w->updateBinaryIndicator(app);
    m_w->refreshSelectionStatus();
    if (markDirty) m_w->setDirty();
}

void EditorController::saveField(Field field)
{
    if (!m_w->m_currentItem) return;
    const int type = m_w->m_currentItem->data(TypeRole).toInt();

    if (field == Field::CategoryName || field == Field::CategoryIcon) {
        if (type != TypeCategory) return;

        DirtySuppressor suppress(m_w->m_dirtySuppress);
        if (field == Field::CategoryName) {
            m_w->m_currentItem->setText(m_w->ui->categoryNameField->text());
        } else {
            m_w->m_currentItem->setData(
                m_w->ui->categoryIconField->text(), IconPathRole);
            m_w->applyCategoryIcon(m_w->m_currentItem);
        }
        m_w->setDirty();
        return;
    }

    if (type != TypeApp) return;
    auto *app = static_cast<AppItem *>(m_w->m_currentItem);

    DirtySuppressor suppress(m_w->m_dirtySuppress);

    switch (field) {
    case Field::Name:
        app->setText(m_w->ui->AppNameField->text());
        break;
    case Field::Arguments:
        app->arguments = CommandLine::splitArgs(m_w->ui->ArgField->text());
        break;
    case Field::Icon:
        app->iconPath = m_w->ui->IconField->text();
        if (!app->iconPath.isEmpty())
            app->setIcon(QIcon(app->iconPath));
        else
            app->setIcon(QIcon::fromTheme(
                QStringLiteral("application-x-executable")));
        break;
    case Field::WorkDir:
        app->workDir = m_w->ui->WorkDirField->text();
        break;
    case Field::Binary:
        app->binaryPath = m_w->ui->BinField->text();
        app->isScript = MainWindow::isScriptFile(app->binaryPath);
        if (app->isScript)
            app->launchInTerminal = true;
        updateScriptState();
        m_w->updateBinaryIndicator(app);
        break;
    case Field::Terminal:
        app->launchInTerminal = app->isScript
            ? true
            : m_w->ui->isTerminalChk->isChecked();
        break;
    case Field::Debug:
        app->launchWithDebugger = m_w->ui->isDebugChk->isChecked();
        break;
    case Field::Single:
        app->singleInstance = m_w->ui->isSingleChk->isChecked();
        break;
    case Field::Environment: {
        app->env.clear();
        for (int r = 0; r < m_w->ui->envVars->rowCount(); ++r) {
            auto *k = m_w->ui->envVars->item(r, 0);
            auto *v = m_w->ui->envVars->item(r, 1);
            if (k && !k->text().isEmpty())
                app->env.insert(k->text(), v ? v->text() : QString());
        }
        break;
    }
    case Field::CategoryName:
    case Field::CategoryIcon:
        break;      // handled above
    }

    m_w->refreshSelectionStatus();
    m_w->setDirty();
}

void EditorController::updateScriptState()
{
    if (!m_w->m_currentItem || m_w->m_currentItem->data(TypeRole).toInt() != TypeApp) return;
    auto *app = static_cast<AppItem *>(m_w->m_currentItem);

    QSignalBlocker block(m_w->ui->isTerminalChk);
    if (app->isScript) {
        m_w->ui->isTerminalChk->setChecked(true);
        m_w->ui->isTerminalChk->setEnabled(false);      // locked (doc requirement)
    } else {
        m_w->ui->isTerminalChk->setEnabled(true);
        m_w->ui->isTerminalChk->setChecked(app->launchInTerminal);
    }
}

void EditorController::chooseBinary()
{
    const QString path = QFileDialog::getOpenFileName(
        m_w, MainWindow::tr("Select binary"),
        m_w->ui->BinField->text().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            : QFileInfo(m_w->ui->BinField->text()).absolutePath(),
        MainWindow::tr("All files (*)"));
    if (path.isEmpty()) return;
    m_w->ui->BinField->setText(path);
    saveField(Field::Binary);
}

void EditorController::chooseWorkDir()
{
    const QString dir = QFileDialog::getExistingDirectory(
        m_w, MainWindow::tr("Select working directory"),
        m_w->ui->WorkDirField->text().isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
            : m_w->ui->WorkDirField->text());
    if (dir.isEmpty()) return;
    m_w->ui->WorkDirField->setText(dir);
    saveField(Field::WorkDir);
}

void EditorController::chooseIcon()
{
    IconPickerDialog dlg(m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString icon = dlg.selectedIcon();
    if (icon.isEmpty()) return;

    m_w->ui->IconField->setText(icon);
    saveField(Field::Icon);
}

void EditorController::chooseCategoryIcon()
{
    IconPickerDialog dlg(m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString icon = dlg.selectedIcon();
    if (icon.isEmpty()) return;

    m_w->ui->categoryIconField->setText(icon);
    saveField(Field::CategoryIcon);
}

void EditorController::addEnvVar()
{
    int row = m_w->ui->envVars->rowCount();
    m_w->ui->envVars->insertRow(row);
    m_w->ui->envVars->setItem(row, 0, new QTableWidgetItem(QString()));
    m_w->ui->envVars->setItem(row, 1, new QTableWidgetItem(QString()));
    m_w->ui->envVars->editItem(m_w->ui->envVars->item(row, 0));
}

void EditorController::removeEnvVar()
{
    const int row = m_w->ui->envVars->currentRow();
    if (row < 0) return;
    m_w->ui->envVars->removeRow(row);
    // The model copy of env is only written by saveField(); without this the
    // removed variable would silently come back on the next save.
    saveField(Field::Environment);
}

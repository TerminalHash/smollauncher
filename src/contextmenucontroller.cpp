#include "contextmenucontroller.h"
#include "include/mainwindow.h"
#include "ui_smollauncher_UI.h"
#include "include/appitem.h"
#include "include/commandline.h"
#include "launchcontroller.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QUrl>

#include <algorithm>

ContextMenuController::ContextMenuController(MainWindow *window)
    : m_w(window)
{
}

void ContextMenuController::copyCommandLine(AppItem *app)
{
    if (!app) return;

    QString full;

#ifdef Q_OS_WIN
    QStringList parts;

    // Working directory
    if (!app->workDir.isEmpty()) {
        parts << QStringLiteral("cd /d ")
                     + CommandLine::windowsQuote(app->workDir);
    }

    // Environment variables — sorted for deterministic output
    if (!app->env.isEmpty()) {
        QStringList keys = app->env.keys();
        std::sort(keys.begin(), keys.end());

        QStringList assignments;
        for (const QString &k : keys) {
            assignments << QStringLiteral("set \"%1=%2\"")
                               .arg(k, app->env.value(k));
        }
        parts << assignments.join(QStringLiteral(" && "));
    }

    // Command
    QString cmd = CommandLine::windowsQuote(app->binaryPath);
    if (!app->arguments.isEmpty())
        cmd += QLatin1Char(' ') + CommandLine::windowsJoinArgs(app->arguments);
    parts << cmd;

    full = parts.join(QStringLiteral(" && "));
#else
    QStringList parts;

    // 1. Working directory
    if (!app->workDir.isEmpty())
        parts << QStringLiteral("cd %1 &&").arg(CommandLine::shellQuote(app->workDir));

    // 2. Environment variables — sorted for deterministic output
    if (!app->env.isEmpty()) {
        QString envPart = QStringLiteral("env");
        QStringList keys = app->env.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys)
            envPart += QLatin1Char(' ')
                     + CommandLine::shellQuote(k + QLatin1Char('=')
                                               + app->env.value(k));
        parts << envPart;
    }

    // 3. Command
    QString cmd = CommandLine::shellQuote(app->binaryPath);
    if (!app->arguments.isEmpty())
        cmd += QLatin1Char(' ') + CommandLine::joinArgs(app->arguments);
    parts << cmd;

    full = parts.join(' ');
#endif

    QGuiApplication::clipboard()->setText(full);
    m_w->updateStatus(MainWindow::tr("Copied command line for \"%1\"").arg(app->text()));
}

void ContextMenuController::showBinaryInFileManager(AppItem *app)
{
    if (!app) return;

    const QString bin = CommandLine::resolveCommandPath(app->binaryPath);
    const QFileInfo fi(bin);
    if (bin.isEmpty() || !fi.exists()) {
        QMessageBox::warning(m_w, MainWindow::tr("SmolLauncher"),
            MainWindow::tr("Binary does not exist:\n%1").arg(app->binaryPath));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

AppItem *ContextMenuController::duplicateAppItem(AppItem *src)
{
    if (!src || !src->parent()) return nullptr;

    auto *copy = m_w->createAppItem(
        src->text() + QLatin1Char(' ') + MainWindow::tr("(copy)"));
    copy->binaryPath         = src->binaryPath;
    copy->arguments          = src->arguments;
    copy->workDir            = src->workDir;
    copy->iconPath           = src->iconPath;
    copy->env                = src->env;
    copy->launchInTerminal   = src->launchInTerminal;
    copy->launchWithDebugger = src->launchWithDebugger;
    copy->singleInstance     = src->singleInstance;
    copy->isScript           = src->isScript;
    // desktopFileId intentionally left empty — this is a variant, not the same
    // imported entry, so re-importing the source .desktop should still work.

    if (!src->iconPath.isEmpty())
        copy->setIcon(QIcon(src->iconPath));

    src->parent()->insertRow(src->row() + 1, copy);
    m_w->updateBinaryIndicator(copy);
    m_w->ui->AppList->setCurrentIndex(m_w->toProxy(copy->index()));
    m_w->updateStatus(MainWindow::tr("Duplicated: %1").arg(src->text()));
    return copy;
}

void ContextMenuController::showMenu(const QPoint &pos)
{
    const QModelIndex proxyIdx = m_w->ui->AppList->indexAt(pos);   // proxy
    if (!proxyIdx.isValid()) return;

    if (!m_w->ui->AppList->selectionModel()->isSelected(proxyIdx))
        m_w->ui->AppList->setCurrentIndex(proxyIdx);

    auto *item = m_w->m_model->itemFromIndex(m_w->fromProxy(proxyIdx));
    if (!item) return;
    const int type = item->data(TypeRole).toInt();

    QMenu menu(m_w);

    if (type == TypeApp) {
        auto *app = static_cast<AppItem *>(item);

        menu.addAction(MainWindow::tr("Launch"), m_w, [this, app]{
            LaunchController(m_w).launch(app);
        });
        menu.addSeparator();
        auto *copyAct = menu.addAction(MainWindow::tr("Copy command line"), m_w,
                                       [this, app]{ copyCommandLine(app); });
        copyAct->setEnabled(!app->binaryPath.isEmpty());
        menu.addAction(MainWindow::tr("Show binary in file manager"), m_w,
                       [this, app]{ showBinaryInFileManager(app); });
        menu.addSeparator();
        menu.addAction(MainWindow::tr("Duplicate"), m_w,
                       [this, app]{ duplicateAppItem(app); });
        menu.addSeparator();
        menu.addAction(MainWindow::tr("Remove"), m_w, &MainWindow::onRemoveFromList);

    } else if (type == TypeCategory) {
        menu.addAction(MainWindow::tr("Rename"), m_w, [this, proxyIdx]{
            m_w->ui->AppList->edit(proxyIdx);
        });

        // Count apps for the enable check
        int appCount = 0;
        for (int i = 0; i < item->rowCount(); ++i)
            if (item->child(i)->data(TypeRole).toInt() == TypeApp)
                ++appCount;

        auto *sortAct = menu.addAction(MainWindow::tr("Sort alphabetically"));
        sortAct->setEnabled(appCount >= 2);
        QObject::connect(sortAct, &QAction::triggered, m_w, [this, item]{
            m_w->sortCategoryApps(item);
        });

        menu.addSeparator();
        menu.addAction(MainWindow::tr("Remove"), m_w, &MainWindow::onRemoveFromList);
    } else if (type == TypeSeparator) {
        menu.addAction(MainWindow::tr("Remove"), m_w, &MainWindow::onRemoveFromList);
    }

    if (!menu.isEmpty())
        menu.exec(m_w->ui->AppList->viewport()->mapToGlobal(pos));
}

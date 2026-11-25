#include "importcontroller.h"

#include "../include/appitem.h"
#include "ui_smollauncher_UI.h"
#include "../include/configpaths.h"
#include "../include/debug.h"
#include "../include/mainwindow.h"
#include "steam/steamimporter.h"
#include "steam/steampickerdialog.h"
#include "heroic/heroicimporter.h"
#include "heroic/heroicpickerdialog.h"
#include "itchio/itchimporter.h"
#include "itchio/itchpickerdialog.h"
#include "retroarch/retroarchimporter.h"
#include "retroarch/retroarchpickerdialog.h"
#include "prism/prismimporter.h"
#include "prism/prismpickerdialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

ImportController::ImportController(MainWindow *window)
    : m_w(window)
{
}

// Importers set the binary directly, bypassing the editor's script check.
// Keep the persisted flags consistent so "Launch in terminal" is correct.
void ImportController::configureLaunchMode(AppItem *app)
{
    if (!app) return;
    app->isScript = MainWindow::isScriptFile(app->binaryPath);
    if (app->isScript)
        app->launchInTerminal = true;
}

void ImportController::fromSteam()
{
    // ---- Preflight: Steam must be reachable for the entries to launch
    if (!SteamImporter::isSteamAvailable()) {
        QMessageBox::warning(m_w, MainWindow::tr("Import from Steam"),
            MainWindow::tr("Steam was not found on this system.\n"
               "Imported entries would not be able to launch."));
        return;
    }

    // ---- Scan
    const QList<SteamGame> games = SteamImporter::scan();
    if (games.isEmpty()) {
        QMessageBox::information(m_w, MainWindow::tr("Import from Steam"),
            MainWindow::tr("No fully-installed Steam games were found."));
        return;
    }

    // ---- Dedup set
    QSet<QString> alreadyImported;
    for (int i = 0; i < m_w->m_model->rowCount(); ++i) {
        auto *cat = m_w->m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->desktopFileId.isEmpty())
                alreadyImported.insert(app->desktopFileId);
        }
    }

    // ---- Picker
    SteamPickerDialog dlg(games, alreadyImported, m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<SteamGame> picked = dlg.selectedGames();
    if (picked.isEmpty()) return;
    Debug::log(QStringLiteral("import steam: %1 selected").arg(picked.size()));

    // ---- Import
    auto *parent = m_w->categoryForInsert();
    if (!parent) {
        parent = m_w->createCategoryItem(MainWindow::tr("Steam"));
        m_w->m_model->appendRow(parent);
    }

    const LaunchCommand launcher =
        SteamImporter::launchCommand();

    for (const SteamGame &g : picked) {
        auto *app = m_w->createAppItem(g.name);
        app->binaryPath         = launcher.program;
        app->arguments          = launcher.prefix;
        app->arguments         << QStringLiteral("steam://rungameid/%1").arg(g.appId);
        app->launchInTerminal   = false;
        app->launchWithDebugger = false;
        // steam is a thin client: it hands off to the daemon and exits in ~1 s.
        // Single-instance tracking would be meaningless and misleading here;
        // Steam itself enforces per-game instance rules.
        app->singleInstance     = false;
        app->isScript           = false;
        configureLaunchMode(app);
        app->desktopFileId      = QStringLiteral("steam-") + g.appId;
        if (!g.iconPath.isEmpty()) {
            app->iconPath = g.iconPath;
            app->setIcon(QIcon(g.iconPath));
        }
        parent->appendRow(app);
        m_w->updateBinaryIndicator(app);
    }

    m_w->ui->AppList->expand(m_w->toProxy(parent->index()));
    m_w->updateStatus(MainWindow::tr("Imported %1 Steam game(s)").arg(picked.size()));
}

void ImportController::fromHeroicEpic()
{
    const bool hasHeroic  = HeroicImporter::isHeroicAvailable();
    const bool hasLegendary = HeroicImporter::isLegendaryAvailable();
    const bool hasUmuRun  =
        !QStandardPaths::findExecutable(QStringLiteral("umu-run")).isEmpty();

    if (!hasHeroic && !hasUmuRun && !hasLegendary) {
        QMessageBox::warning(m_w, MainWindow::tr("Import from Heroic"),
            MainWindow::tr("None of 'heroic', 'legendary', or 'umu-run' was found in PATH.\n"
               "Imported entries would not be able to launch."));
        return;
    }

    const QList<HeroicGame> games = HeroicImporter::scanEpic();
    if (games.isEmpty()) {
        QMessageBox::information(m_w, MainWindow::tr("Import from Heroic"),
            MainWindow::tr("No installed Epic Games were found via Heroic."));
        return;
    }

    QSet<QString> alreadyImported;
    for (int i = 0; i < m_w->m_model->rowCount(); ++i) {
        auto *cat = m_w->m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->desktopFileId.isEmpty())
                alreadyImported.insert(app->desktopFileId);
        }
    }

    HeroicPickerDialog dlg(MainWindow::tr("Heroic — Epic Games"),
                           games, alreadyImported, m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<HeroicGame> picked = dlg.selectedGames();
    if (picked.isEmpty()) return;

    QList<HeroicGame> usable;
    int skipped = 0;
    for (const HeroicGame &g : picked) {
        if ((g.launchBinary == QLatin1String("heroic") && !hasHeroic) ||
            (g.launchBinary == QLatin1String("legendary") && !hasLegendary) ||
            (g.launchBinary == QLatin1String("umu-run") && !hasUmuRun)) {
            ++skipped;
            continue;
        }
        usable.append(g);
    }
    Debug::log(QStringLiteral("import heroic: %1 selected, %2 usable")
        .arg(picked.size()).arg(usable.size()));
    if (usable.isEmpty()) {
        QMessageBox::warning(m_w, MainWindow::tr("Import from Heroic"),
            MainWindow::tr("The selected entries need 'heroic' or 'legendary', "
               "which were not found in PATH."));
        return;
    }

    auto *parent = m_w->categoryForInsert();
    if (!parent) {
        parent = m_w->createCategoryItem(MainWindow::tr("Epic Games"));
        m_w->m_model->appendRow(parent);
    }

    for (const HeroicGame &g : usable) {
        auto *app = m_w->createAppItem(g.title);
        app->binaryPath         = g.launchBinary;
        app->arguments          = g.launchArgs;
        app->workDir            = g.installPath;
        app->launchInTerminal   = false;
        app->launchWithDebugger = false;
        app->singleInstance     = false;   // legendary/heroic handle their own locking
        app->isScript           = false;
        configureLaunchMode(app);
        app->env = g.env;
        app->desktopFileId      = g.desktopFileId();
        if (!g.iconPath.isEmpty()) {
            app->iconPath = g.iconPath;
            app->setIcon(QIcon(g.iconPath));
        }
        parent->appendRow(app);
        m_w->updateBinaryIndicator(app);
    }

    m_w->ui->AppList->expand(m_w->toProxy(parent->index()));
    m_w->updateStatus(MainWindow::tr("Imported %1 Epic game(s), skipped %2")
                     .arg(usable.size()).arg(skipped));
}

void ImportController::fromHeroicGog()
{
    const bool hasHeroic = HeroicImporter::isHeroicAvailable();

    const QList<HeroicGame> games = HeroicImporter::scanGog();
    if (games.isEmpty()) {
        QMessageBox::information(m_w, MainWindow::tr("Import from Heroic"),
            MainWindow::tr("No installed GOG games were found via Heroic."));
        return;
    }

    QSet<QString> alreadyImported;
    for (int i = 0; i < m_w->m_model->rowCount(); ++i) {
        auto *cat = m_w->m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->desktopFileId.isEmpty())
                alreadyImported.insert(app->desktopFileId);
        }
    }

    HeroicPickerDialog dlg(MainWindow::tr("Heroic — GOG"),
                           games, alreadyImported, m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<HeroicGame> picked = dlg.selectedGames();
    if (picked.isEmpty()) return;

    QList<HeroicGame> usable;
    int skipped = 0;
    for (const HeroicGame &g : picked) {
        if (g.launchBinary == QLatin1String("heroic") && !hasHeroic) {
            ++skipped;
            continue;
        }
        usable.append(g);
    }
    Debug::log(QStringLiteral("import heroic: %1 selected, %2 usable")
        .arg(picked.size()).arg(usable.size()));
    if (usable.isEmpty()) {
        QMessageBox::warning(m_w, MainWindow::tr("Import from Heroic"),
            MainWindow::tr("The selected entries need 'heroic', which was not found in PATH."));
        return;
    }

    auto *parent = m_w->categoryForInsert();
    if (!parent) {
        parent = m_w->createCategoryItem(MainWindow::tr("GOG Games"));
        m_w->m_model->appendRow(parent);
    }

    for (const HeroicGame &g : usable) {
        auto *app = m_w->createAppItem(g.title);
        app->binaryPath         = g.launchBinary;
        app->arguments          = g.launchArgs;
        app->workDir            = g.installPath;
        app->launchInTerminal   = false;
        app->launchWithDebugger = false;
        app->singleInstance     = false;
        app->isScript           = false;
        configureLaunchMode(app);
        app->env = g.env;
        app->desktopFileId      = g.desktopFileId();
        if (!g.iconPath.isEmpty()) {
            app->iconPath = g.iconPath;
            app->setIcon(QIcon(g.iconPath));
        }
        parent->appendRow(app);
        m_w->updateBinaryIndicator(app);
    }

    m_w->ui->AppList->expand(m_w->toProxy(parent->index()));
    m_w->updateStatus(MainWindow::tr("Imported %1 GOG game(s), skipped %2")
                     .arg(usable.size()).arg(skipped));
}

void ImportController::fromItch()
{
    QSettings settings = launcherSettings();
    QStringList roots = settings.value("itchInstallRoots").toStringList();

    // 1. Authoritative: read butler.db.
    QList<ItchGame> games = ItchImporter::scanFromDatabase();

    // 2. DB missing → ask the user. Never silently scan a directory
    //    (would produce duplicates for versioned folders like
    //    "MyGame-0.4.5-pc" / "MyGame-0.4.7-pc").
    if (games.isEmpty()) {
        const auto ans = QMessageBox::question(
            m_w, MainWindow::tr("Import from itch.io"),
            MainWindow::tr("No itch.io installation database was found at\n"
               "  %1\n\n"
               "Would you like to scan a folder manually instead?\n"
               "(Useful if itch is not installed, or if you sideloaded games "
               "into a custom directory.)")
                .arg(ItchImporter::databasePath()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) return;

        // User-remembered roots first, otherwise a folder picker.
        roots = settings.value("itchManualRoots").toStringList();
        if (roots.isEmpty()) {
            const QString chosen = QFileDialog::getExistingDirectory(
                m_w, MainWindow::tr("Select itch.io games folder"), QDir::homePath());
            if (chosen.isEmpty()) return;
            roots << chosen;
            settings.setValue("itchManualRoots", roots);
        }

        games = ItchImporter::scanRoots(roots);
    }

    if (games.isEmpty()) {
        QMessageBox::information(m_w, MainWindow::tr("Import from itch.io"),
            MainWindow::tr("No itch.io games were found."));
        return;
    }

    // ── Dedup set ──────────────────────────────────────────────────────
    QSet<QString> alreadyImported;
    for (int i = 0; i < m_w->m_model->rowCount(); ++i) {
        auto *cat = m_w->m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->desktopFileId.isEmpty())
                alreadyImported.insert(app->desktopFileId);
        }
    }

    // ── Picker ─────────────────────────────────────────────────────────
    ItchPickerDialog dlg(roots, games, alreadyImported, m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<ItchGame> picked = dlg.selectedGames();
    if (picked.isEmpty()) return;
    Debug::log(QStringLiteral("import itch: %1 selected").arg(picked.size()));

    // ── Import ─────────────────────────────────────────────────────────
    auto *parent = m_w->categoryForInsert();
    if (!parent) {
        parent = m_w->createCategoryItem(MainWindow::tr("itch.io"));
        m_w->m_model->appendRow(parent);
    }

    for (const ItchGame &g : picked) {
        auto *app = m_w->createAppItem(g.title);

#if defined(Q_OS_WIN)
        // Native Windows build: run the .exe directly.
        app->binaryPath = g.executable;
        app->arguments  = g.arguments;
#else
        if (g.windowsOnly) {
            // Mirror itch/Heroic: run the Windows build through Wine, with
            // the .exe path relative to the game directory (the working dir).
            app->binaryPath = QStringLiteral("wine");
            QString exe = QDir(g.installPath).relativeFilePath(g.executable);
            if (exe.startsWith(QLatin1String("..")))
                exe = g.executable;
            app->arguments = { exe };
        } else {
            app->binaryPath = g.executable;
            app->arguments  = g.arguments;
        }
#endif

        app->workDir            = g.installPath;   // matches itch's own cwd
        app->launchInTerminal   = false;
        app->launchWithDebugger = false;
        app->singleInstance     = true;
        app->isScript           = false;
        configureLaunchMode(app);
        app->desktopFileId      = g.desktopFileId();
        if (!g.iconPath.isEmpty()) {
            app->iconPath = g.iconPath;
            app->setIcon(QIcon(g.iconPath));
        }
        parent->appendRow(app);
        m_w->updateBinaryIndicator(app);
    }

    m_w->ui->AppList->expand(m_w->toProxy(parent->index()));
    m_w->updateStatus(MainWindow::tr("Imported %1 itch.io game(s)").arg(picked.size()));
}

void ImportController::fromRetroArch()
{
    if (!RetroArchImporter::isAvailable()) {
        QMessageBox::warning(m_w, MainWindow::tr("Import from RetroArch"),
            MainWindow::tr("The 'retroarch' command was not found in PATH.\n"
               "Imported entries would not be able to launch."));
        return;
    }

    const QList<RetroArchGame> games = RetroArchImporter::scan();
    if (games.isEmpty()) {
        QMessageBox::information(m_w, MainWindow::tr("Import from RetroArch"),
            MainWindow::tr("No playlists with existing ROMs were found in:\n%1")
                .arg(RetroArchImporter::playlistsDirectory()));
        return;
    }

    QSet<QString> alreadyImported;
    for (int i = 0; i < m_w->m_model->rowCount(); ++i) {
        auto *cat = m_w->m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->desktopFileId.isEmpty())
                alreadyImported.insert(app->desktopFileId);
        }
    }

    RetroArchPickerDialog dlg(games, alreadyImported, m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<RetroArchGame> picked = dlg.selectedGames();
    if (picked.isEmpty()) return;
    Debug::log(QStringLiteral("import retroarch: %1 selected").arg(picked.size()));

    auto *parent = m_w->categoryForInsert();
    if (!parent) {
        parent = m_w->createCategoryItem(MainWindow::tr("RetroArch"));
        m_w->m_model->appendRow(parent);
    }

    const LaunchCommand launcher = RetroArchImporter::launchCommand();
    for (const RetroArchGame &g : picked) {
        auto *app = m_w->createAppItem(g.romTitle);
        app->binaryPath         = launcher.program;
        app->arguments          = launcher.prefix;
        // Only pass -L for a core that actually exists; otherwise let
        // RetroArch auto-detect (the scan flags that case as a warning).
        if (!g.corePath.isEmpty() && QFileInfo::exists(g.corePath))
            app->arguments << QStringLiteral("-L") << g.corePath;
        app->arguments << g.romPath;
        app->workDir            = QFileInfo(g.romPath).absolutePath();
        app->launchInTerminal   = false;
        app->launchWithDebugger = false;
        app->singleInstance     = false;   // RetroArch can run multiple instances
        app->isScript           = false;
        configureLaunchMode(app);
        app->desktopFileId      = g.desktopFileId();
        parent->appendRow(app);
        m_w->updateBinaryIndicator(app);
    }

    m_w->ui->AppList->expand(m_w->toProxy(parent->index()));
    m_w->updateStatus(MainWindow::tr("Imported %1 ROM(s)").arg(picked.size()));
}

void ImportController::fromPrism()
{
    if (!PrismImporter::isAvailable()) {
        QMessageBox::warning(m_w, MainWindow::tr("Import from Prism Launcher"),
            MainWindow::tr("Prism Launcher was not found on this system.\n"
               "Imported entries would not be able to launch."));
        return;
    }

    const QList<PrismInstance> instances = PrismImporter::scan();
    if (instances.isEmpty()) {
        QMessageBox::information(
            m_w, MainWindow::tr("Import from Prism Launcher"),
            MainWindow::tr("No Prism Launcher instances were found in\n  %1")
                .arg(PrismImporter::instanceDirectory()));
        return;
    }

    // ---- Picker, skipping instances already in the list
    QSet<QString> alreadyImported;
    for (int i = 0; i < m_w->m_model->rowCount(); ++i) {
        auto *cat = m_w->m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->desktopFileId.isEmpty())
                alreadyImported.insert(app->desktopFileId);
        }
    }

    PrismPickerDialog dlg(instances, alreadyImported, m_w);
    if (dlg.exec() != QDialog::Accepted) return;

    const QList<PrismInstance> picked = dlg.selectedInstances();
    if (picked.isEmpty()) return;
    Debug::log(QStringLiteral("import prism: %1 selected").arg(picked.size()));

    auto *parent = m_w->categoryForInsert();
    if (!parent) {
        parent = m_w->createCategoryItem(MainWindow::tr("Minecraft"));
        m_w->m_model->appendRow(parent);
    }

    const LaunchCommand launcher = PrismImporter::launchCommand();
    for (const PrismInstance &inst : picked) {
        auto *app = m_w->createAppItem(inst.displayName());
        app->binaryPath         = launcher.program;
        app->arguments          = launcher.prefix;
        app->arguments         << QStringLiteral("--launch") << inst.uuid;
        app->workDir            = inst.instancePath;
        app->launchInTerminal   = false;
        app->launchWithDebugger = false;
        app->singleInstance     = false;   // Prism can launch other instances
        app->isScript           = false;
        configureLaunchMode(app);
        app->desktopFileId      = inst.desktopFileId();
        if (!inst.iconPath.isEmpty()) {
            app->iconPath = inst.iconPath;
            app->setIcon(QIcon(inst.iconPath));
        }
        parent->appendRow(app);
        m_w->updateBinaryIndicator(app);
    }

    m_w->ui->AppList->expand(m_w->toProxy(parent->index()));
    m_w->updateStatus(MainWindow::tr("Imported %1 Minecraft instance(s)")
                          .arg(picked.size()));
}

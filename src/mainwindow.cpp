// ---------------------------------------------
#include "include/mainwindow.h"
#include "include/appitem.h"
#include "ui_smollauncher_UI.h"
#include "include/desktoppickerdialog.h"
#include "include/aboutdialog.h"
#include "include/commandline.h"
#include "include/configfile.h"
#include "include/configpaths.h"
#include "include/debug.h"
#include "include/catwidget.h"
#include "include/iconpickerdialog.h"
#include "include/terminalsettingsdialog.h"
#include "include/launcherfilterproxy.h"
#include "launchcontroller.h"
#include "contextmenucontroller.h"
#include "editorcontroller.h"
#include "importers/importcontroller.h"

// ---------------------------------------------
#include <algorithm>

// ---------------------------------------------
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QSignalBlocker>
#include <QSettings>
#include <QSet>
#include <QCloseEvent>
#include <QTimer>
#include <QStyle>
#include <QMenu>
#include <QActionGroup>
#include <QLibraryInfo>
#include <QLocale>
#include <QShortcut>
#include <QColor>
#include <QVariant>

// ----------------------------------------------------------------------------------------

namespace {

// Pick the UI language from the system locale on first launch. Only the
// languages shipped with the launcher are considered; everything else
// falls back to English.
QString systemLanguageCode()
{
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &tag : uiLanguages) {
        if (tag.startsWith(QLatin1String("ru"), Qt::CaseInsensitive))
            return QStringLiteral("ru");
        if (tag.startsWith(QLatin1String("en"), Qt::CaseInsensitive))
            return QStringLiteral("en");
    }
    return QStringLiteral("en");
}

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

// ---------------------------------------------------------------------------
// constructor / destructor
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Pages without a QTabWidget paint their own pane. Give them the tab
    // widget's palette so the pane fill matches the tabbed editor exactly.
    ui->tabWidget->ensurePolished();
    ui->categoryPane->setPaletteSource(ui->tabWidget);
    ui->nothingPane->setPaletteSource(ui->tabWidget);

    m_cat = new CatWidget(ui->mainTab);
    ui->mainTabLayout->insertSpacerItem(
        3, new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    ui->mainTabLayout->insertWidget(4, m_cat, 0, Qt::AlignHCenter);
    m_cat->hide();

    // Pages without a tab bar get their framed pane offset by the tab bar
    // height so they line up with the tabbed editor page. The frame overlaps
    // the tab bar base by PM_TabBarBaseOverlap.
    const int paneOffset =
        ui->tabWidget->tabBar()->sizeHint().height()
        - ui->tabWidget->style()->pixelMetric(
              QStyle::PM_TabBarBaseOverlap, nullptr, ui->tabWidget);
    for (QWidget *page : {ui->categoryEditorPage, ui->nothingPage}) {
        if (auto *outer = qobject_cast<QVBoxLayout *>(page->layout())) {
            outer->setContentsMargins(0, paneOffset, 0, 0);
        }
    }

    // Repurpose placeholder checkboxes from the designer file
    ui->isSingleChk->setText(tr("Single instance"));
    ui->isSingleChk->setChecked(true);

    m_recentMenu = new QMenu(this);
    ui->actionRecentLists->setMenu(m_recentMenu);
    connect(m_recentMenu, &QMenu::aboutToShow,
            this, &MainWindow::rebuildRecentMenu);

    setupModel();
    {
        const QString last = launcherSettings().value("lastConfigPath").toString();
        if (!last.isEmpty() && QFileInfo::exists(last))
        loadConfigFile(last);
    }

    // First launch: follow the system locale
    QSettings settings = launcherSettings();
    QString language = settings.value(QStringLiteral("language")).toString();
    if (language.isEmpty()) {
        language = systemLanguageCode();
        settings.setValue(QStringLiteral("language"), language);
        settings.sync();
    }

    setupConnections();
    setupLanguageSubmenu();
    setupIconSizeActions();
    setupImportSubmenu();
    applyIconSize(launcherSettings().value("iconSize", 24).toInt());

    {
        QSignalBlocker block(ui->actionFixedWindowSize);
        ui->actionFixedWindowSize->setChecked(
            launcherSettings().value("fixedWindowSize", true).toBool());
        applyFixedWindowSize(ui->actionFixedWindowSize->isChecked());
    }

    switchLanguage(language);

    ui->tabWidget->setEnabled(false);
    ui->statusbar->showMessage(tr("Ready"));
}

MainWindow::~MainWindow()
{
    clearUndoStack();

    // Detach processes
    // Their finished-handlers use QPointer<MainWindow> and self-delete.
    for (QProcess *p : std::as_const(m_running)) {
        p->disconnect(this);
    }
    delete ui;
}

// ---------------------------------------------------------------------------
// model / connections
// ---------------------------------------------------------------------------
void MainWindow::setupModel()
{
    m_model = new LauncherModel(this);
    m_proxy = new LauncherFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    ui->AppList->setModel(m_proxy);
    ui->AppList->setHeaderHidden(true);
    ui->AppList->setEditTriggers(QAbstractItemView::EditKeyPressed);
    ui->AppList->setSelectionMode(QAbstractItemView::SingleSelection);

    // drag & drop
    ui->AppList->setDragEnabled(true);
    ui->AppList->setAcceptDrops(true);
    ui->AppList->setDropIndicatorShown(true);
    ui->AppList->setDragDropMode(QAbstractItemView::InternalMove);
    ui->AppList->setDefaultDropAction(Qt::MoveAction);
    ui->AppList->setAutoExpandDelay(400);

    // filter field — provided by the designer file below the button row
    m_filterField = ui->searchField;
    m_filterField->setClearButtonEnabled(true);
    m_filterField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_model->appendRow(createCategoryItem(tr("General")));
    ui->AppList->expandAll();
}

void MainWindow::setupConnections()
{
    // ---- toolbar buttons -------------------------------------------------
    connect(ui->AddAppBtn, &QPushButton::clicked, this, &MainWindow::onAddApplication);
    connect(ui->AddCatBtn, &QPushButton::clicked, this, &MainWindow::onAddCategory);
    connect(ui->AddSepBtn, &QPushButton::clicked, this, &MainWindow::onAddSeparator);
    connect(ui->MoveUpBtn, &QPushButton::clicked, this, &MainWindow::onMoveUp);
    connect(ui->MoveDownBtn, &QPushButton::clicked, this, &MainWindow::onMoveDown);
    connect(ui->OpenDirBtn,  &QPushButton::clicked, this, &MainWindow::onOpenDirectory);
    connect(ui->RemoveBtn,  &QPushButton::clicked, this, &MainWindow::onRemoveFromList);
    connect(ui->LaunchAppBtn, &QPushButton::clicked, this, &MainWindow::onLaunchApp);

    // ---- editors ---------------------------------------------------------
    connect(ui->ChooseWorkDir, &QPushButton::clicked, this,
            [this]{ EditorController(this).chooseWorkDir(); });
    connect(ui->ChooseBin, &QPushButton::clicked, this,
            [this]{ EditorController(this).chooseBinary(); });
    connect(ui->IconChooseButton, &QPushButton::clicked, this,
            [this]{ EditorController(this).chooseIcon(); });

    connect(ui->AddEnvBtn, &QPushButton::clicked, this,
            [this]{ EditorController(this).addEnvVar(); });
    connect(ui->RemoveEnvBtn, &QPushButton::clicked, this,
            [this]{ EditorController(this).removeEnvVar(); });

    connect(ui->AppNameField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Name); });
    connect(ui->ArgField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Arguments); });
    connect(ui->IconField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Icon); });
    connect(ui->WorkDirField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::WorkDir); });
    connect(ui->BinField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Binary); });

    connect(ui->isTerminalChk, &QCheckBox::toggled, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Terminal); });
    connect(ui->isDebugChk, &QCheckBox::toggled, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Debug); });
    connect(ui->isSingleChk, &QCheckBox::toggled, this,
            [this]{ EditorController(this).saveField(EditorController::Field::Single); });

    connect(ui->envVars, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem*){ EditorController(this).saveField(EditorController::Field::Environment); });

    // ---- category editor page -------------------------------------------
    connect(ui->categoryNameField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::CategoryName); });
    connect(ui->categoryIconField, &QLineEdit::textChanged, this,
            [this]{ EditorController(this).saveField(EditorController::Field::CategoryIcon); });
    connect(ui->chooseCatIconBtn, &QPushButton::clicked, this,
            [this]{ EditorController(this).chooseCategoryIcon(); });
    connect(ui->categoryRemove, &QPushButton::clicked,
            this, &MainWindow::onRemoveFromList);

    // ---- tree ------------------------------------------------------------
    connect(ui->AppList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onTreeSelectionChanged);
    connect(ui->AppList, &QTreeView::doubleClicked,
            this, &MainWindow::onTreeDoubleClicked);

    auto markDirty = [this]{ setDirty(); };
    // Structural changes (add/remove/move/import) mark the list dirty.
    connect(m_model, &QStandardItemModel::rowsInserted, this, markDirty);
    connect(m_model, &QStandardItemModel::rowsRemoved,  this, markDirty);
    connect(m_model, &QStandardItemModel::rowsMoved,    this, markDirty);

    // ---- context menu on the tree ----------------------------------------
    ui->AppList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->AppList, &QTreeView::customContextMenuRequested,
        this, &MainWindow::onTreeContextMenu);

    // ---- menu -----------------------------------------------------------
    connect(ui->actionFile,         &QAction::triggered, this, &MainWindow::onFileOpen);
    connect(ui->actionSaveList,     &QAction::triggered, this, &MainWindow::onSaveConfig);
    connect(ui->actionSaveListAs, &QAction::triggered, this, &MainWindow::onSaveConfigAs);
    connect(ui->actionNewList,      &QAction::triggered, this, &MainWindow::onNewList);
    connect(ui->actionAddFromMenu,  &QAction::triggered, this, &MainWindow::onAddFromDesktop);
    connect(ui->actionExit,         &QAction::triggered, this, &MainWindow::close);
    connect(ui->actionFixedWindowSize, &QAction::toggled, this, [this](bool fixed){
        if (fixed)
            saveFreeWindowSize();
        applyFixedWindowSize(fixed);
        launcherSettings().setValue("fixedWindowSize", fixed);
    });
    connect(ui->actionAbout, &QAction::triggered, this, [this]{
        AboutDialog dlg(this);
        dlg.exec();
    });
    connect(ui->actionSortApps, &QAction::triggered, this, [this]{
        if (auto *cat = selectedCategory())
            sortCategoryApps(cat);
    });
    connect(ui->actionSortAllCategories, &QAction::triggered,
            this, &MainWindow::sortAllCategories);
    connect(ui->menuSort, &QMenu::aboutToShow, this, [this]{
        auto *cat = selectedCategory();
        int appCount = 0;
        if (cat) {
            for (int i = 0; i < cat->rowCount(); ++i)
                if (cat->child(i)->data(TypeRole).toInt() == TypeApp)
                    ++appCount;
        }
        ui->actionSortApps->setEnabled(appCount >= 2);

        bool anySortable = false;
        for (int i = 0; i < m_model->rowCount() && !anySortable; ++i) {
            auto *item = m_model->item(i);
            if (item->data(TypeRole).toInt() != TypeCategory)
                continue;
            int count = 0;
            for (int j = 0; j < item->rowCount(); ++j)
                if (item->child(j)->data(TypeRole).toInt() == TypeApp)
                    ++count;
            anySortable = count >= 2;
        }
        ui->actionSortAllCategories->setEnabled(anySortable);
    });

#ifdef Q_OS_LINUX
    connect(ui->actionTerminalEmulator, &QAction::triggered, this, [this]{
        TerminalSettingsDialog dlg(this);
        dlg.exec();
    });
#else
    ui->actionTerminalEmulator->setVisible(false);
#endif

    // ---- keyboard shortcuts for menu actions -------------------------------
    ui->actionNewList->setShortcut(QKeySequence::New);             // Ctrl+N
    ui->actionFile->setShortcut(QKeySequence::Open);               // Ctrl+O
    ui->actionSaveList->setShortcut(QKeySequence::Save);           // Ctrl+S
    ui->actionSaveListAs->setShortcut(QKeySequence::SaveAs);       // Ctrl+Shift+S
    ui->actionExit->setShortcut(QKeySequence::Quit);               // Ctrl+Q
    ui->actionAddFromMenu->setShortcut(
    QKeySequence(QStringLiteral("Ctrl+Shift+A")));             // Ctrl+Shift+A

    // ---- model signal ---------------------------------------------------
    connect(m_model, &LauncherModel::itemMoved, this, &MainWindow::onItemMoved);
    connect(m_model, &QStandardItemModel::itemChanged, this,
        [this](QStandardItem *item){
        if (!m_dirtySuppress)
            setDirty();
        if (item == m_currentItem) {
            if (item->data(TypeRole).toInt() == TypeCategory) {
                QSignalBlocker b1(ui->categoryNameField),
                               b2(ui->categoryIconField);
                ui->categoryNameField->setText(item->text());
                ui->categoryIconField->setText(
                    item->data(IconPathRole).toString());
            } else {
                QSignalBlocker b(ui->AppNameField);
                ui->AppNameField->setText(item->text());
            }
        }

        if (item->data(TypeRole).toInt() == TypeCategory) {
            if (!item->toolTip().isEmpty() && item->toolTip() != item->text()) {
                QSignalBlocker blockModel(m_model);
                item->setToolTip(item->text());
            }
        }

        if (auto *app = dynamic_cast<AppItem *>(item))
            updateBinaryIndicator(app);
    });

    // ---- in-tree shortcuts --------------------------------------------------
    auto *returnKey = new QShortcut(QKeySequence(Qt::Key_Return), ui->AppList);
    returnKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(returnKey, &QShortcut::activated, this, &MainWindow::onLaunchApp);

    auto *enterKey = new QShortcut(QKeySequence(Qt::Key_Enter), ui->AppList);
    enterKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(enterKey, &QShortcut::activated, this, &MainWindow::onLaunchApp);

    auto *delKey = new QShortcut(QKeySequence(Qt::Key_Delete), ui->AppList);
    delKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(delKey, &QShortcut::activated, this, &MainWindow::onRemoveFromList);

    auto *undoKey = new QShortcut(QKeySequence::Undo, ui->AppList);
    undoKey->setContext(Qt::WidgetShortcut);   // not the inline editor
    connect(undoKey, &QShortcut::activated, this, [this]{
        undoLastAction();
    });

    auto *focusBin = new QShortcut(
            QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(focusBin, &QShortcut::activated, this, [this]{
        if (!ui->tabWidget->isTabEnabled(1)) return;   // Path tab is app-only
        ui->tabWidget->setCurrentIndex(1);              // Path
        ui->BinField->setFocus();
        ui->BinField->selectAll();
    });

    // ---- filter --------------------------------------------------------------
    connect(m_filterField, &QLineEdit::textChanged,
            this, &MainWindow::onFilterChanged);
    connect(m_filterField, &QLineEdit::returnPressed, this, [this]{
        ui->AppList->setFocus();        // Enter → jump to tree, arrows + Enter to launch
    });

    // Ctrl+F → focus filter
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this]{
        m_filterField->setFocus();
        m_filterField->selectAll();
    });

    // ---- expanded / collapsed state → ExpandedRole ---------------------------
    connect(ui->AppList, &QTreeView::expanded, this, [this](const QModelIndex &proxyIdx){
        if (m_applyingFilter) return;
        auto *item = m_model->itemFromIndex(fromProxy(proxyIdx));
        if (item && item->data(TypeRole).toInt() == TypeCategory) {
            ++m_dirtySuppress;      // UI state, not a user edit
            item->setData(true, ExpandedRole);
            --m_dirtySuppress;
            saveCategoryExpansionState();
        }
    });
    connect(ui->AppList, &QTreeView::collapsed, this, [this](const QModelIndex &proxyIdx){
        if (m_applyingFilter) return;
        auto *item = m_model->itemFromIndex(fromProxy(proxyIdx));
        if (item && item->data(TypeRole).toInt() == TypeCategory) {
            ++m_dirtySuppress;      // UI state, not a user edit
            item->setData(false, ExpandedRole);
            --m_dirtySuppress;
            saveCategoryExpansionState();
        }
    });

    // ---- status bar: restore selection command after transient messages ----
        connect(ui->statusbar, &QStatusBar::messageChanged, this,
            [this](const QString &msg){
        if (!msg.isEmpty()) return;            // a message is showing — leave it
        if (m_selectionCommand.isEmpty()) return;

        QTimer::singleShot(0, this, [this]{
            if (!m_selectionCommand.isEmpty() &&
                ui->statusbar->currentMessage().isEmpty())
                ui->statusbar->showMessage(m_selectionCommand);
        });
    });
}

// ---------------------------------------------------------------------------
// item factories
// ---------------------------------------------------------------------------
QStandardItem *MainWindow::createCategoryItem(const QString &name,
                                              const QString &iconPath)
{
    auto *it = new QStandardItem(name);
    it->setData(TypeCategory, TypeRole);
    it->setData(iconPath, IconPathRole);
    it->setToolTip(name);
    it->setData(true, ExpandedRole);   // expanded by default
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                 | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    applyCategoryIcon(it);
    return it;
}

void MainWindow::applyCategoryIcon(QStandardItem *cat)
{
    if (!cat) return;
    const QString p = cat->data(IconPathRole).toString();
    if (p.isEmpty() || !QFileInfo::exists(p))
        cat->setIcon(QIcon::fromTheme("folder"));
    else
        cat->setIcon(QIcon(p));
}

QStandardItem *MainWindow::createSeparatorItem()
{
    auto *it = new QStandardItem(QStringLiteral("——————"));
    it->setData(TypeSeparator, TypeRole);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                 | Qt::ItemIsDragEnabled);          // draggable, no children
    return it;
}

AppItem *MainWindow::createAppItem(const QString &name)
{
    auto *it = new AppItem(name);
    it->setData(TypeApp, TypeRole);
    it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                 | Qt::ItemIsDragEnabled);          // no DropEnabled → leaf
    it->setIcon(QIcon::fromTheme("application-x-executable"));
    return it;
}

// ---------------------------------------------------------------------------
// add / move / remove
// ---------------------------------------------------------------------------
QStandardItem *MainWindow::categoryForInsert() const
{
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (proxyCur.isValid()) {
        auto *it = m_model->itemFromIndex(fromProxy(proxyCur));
        if (it->data(TypeRole).toInt() == TypeCategory) return it;
        if (it->parent()) return it->parent();
    }
    for (int i = 0; i < m_model->rowCount(); ++i)
        if (m_model->item(i)->data(TypeRole).toInt() == TypeCategory)
            return m_model->item(i);
    return nullptr;
}

QStandardItem *MainWindow::selectedCategory() const
{
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (!proxyCur.isValid()) return nullptr;

    auto *it = m_model->itemFromIndex(fromProxy(proxyCur));
    if (!it) return nullptr;
    return it->data(TypeRole).toInt() == TypeCategory ? it : it->parent();
}

void MainWindow::onAddCategory()
{
    auto *cat = createCategoryItem(tr("New category"));
    m_model->appendRow(cat);
    const QModelIndex proxyIdx = toProxy(cat->index());           // ← toProxy
    ui->AppList->setCurrentIndex(proxyIdx);
    ui->AppList->edit(proxyIdx);
}

void MainWindow::onAddApplication()
{
    auto *parent = categoryForInsert();
    if (!parent) {
        parent = createCategoryItem(tr("General"));
        m_model->appendRow(parent);
    }
    auto *app = createAppItem(tr("New application"));
    int insertRow = parent->rowCount();
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (proxyCur.isValid()) {
        auto *it = m_model->itemFromIndex(fromProxy(proxyCur));
        if (it && it->parent() == parent)
            insertRow = it->row() + 1;
    }
    parent->insertRow(insertRow, app);
    ui->AppList->expand(toProxy(parent->index()));                // ← toProxy
    ui->AppList->setCurrentIndex(toProxy(app->index()));
}

void MainWindow::onAddFromDesktop()
{
    DesktopPickerDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const DesktopEntry e = dlg.selected();
    if (e.exec.isEmpty()) return;

    // ---- Pass 1: exact .desktop re-import -------------------------------
    if (!e.id.isEmpty()) {
        for (int i = 0; i < m_model->rowCount(); ++i) {
            auto *cat = m_model->item(i);
            if (cat->data(TypeRole).toInt() != TypeCategory) continue;
            for (int j = 0; j < cat->rowCount(); ++j) {
                auto *a = dynamic_cast<AppItem *>(cat->child(j));
                if (!a || a->desktopFileId != e.id) continue;

                const auto ans = QMessageBox::question(
                    this, tr("Already imported"),
                    tr("\"%1\" was already imported from the same menu entry.\n\n"
                       "Add another copy?").arg(e.name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (ans != QMessageBox::Yes) {
                    ui->AppList->setCurrentIndex(toProxy(a->index()));
                    ui->AppList->scrollTo(toProxy(a->index()));
                    return;
                }
                break;   // user chose Yes → fall through and add
            }
        }
    }

    // ---- Pass 2: same binary + args under a different .desktop ----------
    const QString binary = e.program;

    if (AppItem *match = findExistingApp(binary, e.arguments)) {
        const auto ans = QMessageBox::question(
            this, tr("Possible duplicate"),
            tr("An entry with the same binary and arguments already exists:\n"
               "  \"%1\"  (%2)\n\nAdd another copy?")
                .arg(match->text(), match->binaryPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ans != QMessageBox::Yes) {
            ui->AppList->setCurrentIndex(toProxy(match->index()));
            ui->AppList->scrollTo(toProxy(match->index()));
            return;
        }
    }

    // ---- Proceed with the import (unchanged from here) ------------------
    auto *parent = categoryForInsert();
    if (!parent) {
        parent = createCategoryItem(tr("General"));
        m_model->appendRow(parent);
    }

    auto *app = createAppItem(e.name);
    app->binaryPath       = e.program;
    app->arguments        = e.arguments;
    app->workDir          = e.path;
    app->launchInTerminal = e.terminal;
    app->singleInstance   = true;
    app->isScript         = isScriptFile(e.program);
    if (app->isScript)
        app->launchInTerminal = true;
    app->desktopFileId    = e.id;

    const QString iconPath = DesktopEntryScanner::resolveIconName(e.icon);
    if (!iconPath.isEmpty()) {
        app->iconPath = iconPath;
        app->setIcon(QIcon(iconPath));
    }

    parent->appendRow(app);
    updateBinaryIndicator(app);
    ui->AppList->expand(toProxy(parent->index()));
    ui->AppList->setCurrentIndex(toProxy(app->index()));
    updateStatus(tr("Imported: %1").arg(e.name));
}

void MainWindow::onAddSeparator()
{
    auto *parent = categoryForInsert();
    Debug::detail(QStringLiteral("onAddSeparator: parent=%1 currentProxyRow=%2")
        .arg(itemDescription(parent))
        .arg(ui->AppList->currentIndex().row()));
    if (!parent) return;

    int insertRow = parent->rowCount();
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (proxyCur.isValid()) {
        auto *it = m_model->itemFromIndex(fromProxy(proxyCur));
        if (it && it->parent() == parent)
            insertRow = it->row() + 1;
    }
    parent->insertRow(insertRow, createSeparatorItem());
}

void MainWindow::onMoveUp()
{
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (!proxyCur.isValid()) return;
    const QModelIndex cur = fromProxy(proxyCur);              // ← fromProxy
    if (!cur.isValid()) return;

    const int row = cur.row();          // valid for top-level AND children
    if (row <= 0) return;
    const int newRow = row - 1;

    auto *it     = m_model->itemFromIndex(cur);
    auto *parent = it->parent();
    QStandardItem *moved = nullptr;

    if (parent) {
        moved = parent->takeRow(row).first();
        parent->insertRow(newRow, moved);
    } else {
        moved = m_model->takeRow(row).first();
        m_model->insertRow(newRow, moved);
    }

    if (moved)
        ui->AppList->setCurrentIndex(toProxy(moved->index()));
}

void MainWindow::onMoveDown()
{
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (!proxyCur.isValid()) return;
    const QModelIndex cur = fromProxy(proxyCur);              // ← fromProxy
    if (!cur.isValid()) return;

    auto *it     = m_model->itemFromIndex(cur);
    auto *parent = it->parent();

    const int row   = cur.row();
    const int count = parent ? parent->rowCount() : m_model->rowCount();
    if (row >= count - 1) return;
    const int newRow = row + 1;

    QStandardItem *moved = nullptr;
    if (parent) {
        moved = parent->takeRow(row).first();
        parent->insertRow(newRow, moved);
    } else {
        moved = m_model->takeRow(row).first();
        m_model->insertRow(newRow, moved);
    }

    if (moved)
        ui->AppList->setCurrentIndex(toProxy(moved->index()));
}

void MainWindow::onRemoveFromList()
{
    const QModelIndex proxyCur = ui->AppList->currentIndex();
    if (!proxyCur.isValid()) return;
    const QModelIndex cur = fromProxy(proxyCur);

    auto *it = m_model->itemFromIndex(cur);
    if (!it) return;
    Debug::detail(QStringLiteral("onRemoveFromList: current=%1").arg(itemDescription(it)));
    if (it->data(TypeRole).toInt() == TypeCategory) {
        int categories = 0;
        for (int i = 0; i < m_model->rowCount(); ++i)
            if (m_model->item(i)->data(TypeRole).toInt() == TypeCategory)
                ++categories;
        if (categories <= 1) {
            QMessageBox::information(
                this, tr("Remove category"),
                tr("At least one category is required."));
            return;
        }

        const auto answer = QMessageBox::question(
            this, tr("Remove category"),
            tr("Remove category \"%1\" and everything in it?").arg(it->text()));
        if (answer != QMessageBox::Yes) return;
    }

    // Pick the row that should become current after the removal
    QStandardItem *parentItem = it->parent();
    const int row = it->row();
    QStandardItem *next = nullptr;
    if (parentItem) {
        if (row + 1 < parentItem->rowCount())
            next = parentItem->child(row + 1);
        else if (row > 0)
            next = parentItem->child(row - 1);
    } else {
        if (row + 1 < m_model->rowCount())
            next = m_model->item(row + 1);
        else if (row > 0)
            next = m_model->item(row - 1);
    }

    UndoEntry undo;
    undo.kind        = UndoKind::Removed;
    undo.parent      = parentItem;
    undo.row         = row;
    undo.item        = it;
    undo.description = tr("Remove \"%1\"").arg(it->text());

    m_currentItem = nullptr;
    EditorController(this).clear();

    if (parentItem) {
        if (parentItem->takeRow(row).isEmpty())
            return;
    } else {
        if (m_model->takeRow(row).isEmpty())
            return;
    }
    pushUndo(undo);

    if (next) {
        const QModelIndex proxyNext = toProxy(next->index());
        if (proxyNext.isValid()) {
            ui->AppList->setCurrentIndex(proxyNext);
            ui->AppList->scrollTo(proxyNext);
            // The view may already have moved to this row during removal, in
            // which case setCurrentIndex() does not emit currentChanged again.
            onTreeSelectionChanged(proxyNext, QModelIndex());
        }
    }

    ui->AppList->setFocus();
    updateStatus(tr("Removed \"%1\" (Ctrl+Z to undo)").arg(undo.item->text()));
}

void MainWindow::onLaunchApp()
{
    if (!m_currentItem || m_currentItem->data(TypeRole).toInt() != TypeApp) {
        QMessageBox::information(this, tr("SmolLauncher"),
            tr("Select an application in the list first."));
        return;
    }

    // Pick up any edits the user typed but hasn't confirmed elsewhere
    EditorController(this).save(QStringLiteral("launch"));

    LaunchController(this).launch(static_cast<AppItem *>(m_currentItem));
}

// ---------------------------------------------------------------------------
// selection / editors
// ---------------------------------------------------------------------------
void MainWindow::onTreeSelectionChanged(const QModelIndex &current,
                                        const QModelIndex &previous)
{
    Q_UNUSED(previous);

    Debug::detail(QStringLiteral("selectionChanged: current=%1 previous=%2 currentItem=%3")
        .arg(current.isValid() ? QStringLiteral("row=%1").arg(current.row())
                               : QStringLiteral("invalid"))
        .arg(previous.isValid() ? QStringLiteral("row=%1").arg(previous.row())
                                : QStringLiteral("invalid"))
        .arg(itemDescription(m_currentItem)));

    if (m_currentItem &&
        m_currentItem->data(TypeRole).toInt() == TypeApp) {
        EditorController(this).save(QStringLiteral("selection-change"));
    }

    if (!current.isValid()) {
        m_currentItem = nullptr;
        ui->tabWidget->setEnabled(false);
        EditorController(this).clear();
        refreshSelectionStatus();
        return;
    }

    auto *item = m_model->itemFromIndex(fromProxy(current));
    if (!item) {
        Debug::detail(QStringLiteral("selectionChanged: itemFromIndex returned null"));
        m_currentItem = nullptr;
        EditorController(this).clear();
        refreshSelectionStatus();
        return;
    }
    const int type = item->data(TypeRole).toInt();
    Debug::detail(QStringLiteral("selectionChanged: resolved %1").arg(itemDescription(item)));

    if (type == TypeApp || type == TypeCategory) {
        m_currentItem = item;
        EditorController editor(this);
        editor.loadItem(item);
        editor.applyMode(type);
    } else {
        ui->tabWidget->setEnabled(false);
        EditorController(this).clear();
    }

    refreshSelectionStatus();
}

// ---------------------------------------------------------------------------
// file pickers
// ---------------------------------------------------------------------------
void MainWindow::onOpenDirectory()
{
    if (!m_currentItem || m_currentItem->data(TypeRole).toInt() != TypeApp) return;
    auto *app = static_cast<AppItem *>(m_currentItem);

    QString dir = app->workDir;
    if (dir.isEmpty()) {
        const QString bin = CommandLine::resolveCommandPath(app->binaryPath);
        if (!bin.isEmpty() && QFileInfo(bin).isFile())
            dir = QFileInfo(bin).absolutePath();
    }
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::warning(this, tr("Open directory"),
                             tr("Directory does not exist."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

// ---------------------------------------------------------------------------
// environment variable table
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// launching
// ---------------------------------------------------------------------------
void MainWindow::onTreeDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    auto *item = m_model->itemFromIndex(fromProxy(index));     // ← fromProxy
    if (!item) return;
    const int type = item->data(TypeRole).toInt();

    if (type == TypeApp) {
        LaunchController(this).launch(static_cast<AppItem *>(item));
    } else if (type == TypeCategory) {
        ui->AppList->edit(index);      // index is proxy — correct
    }
}

bool MainWindow::isScriptFile(const QString &path)
{
    if (path.isEmpty()) return false;

    static const QStringList exts = {
        "sh", "bash", "zsh", "py", "pl", "rb", "bat", "cmd", "ps1"
    };
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (exts.contains(suffix)) return true;

    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray first = f.read(2);
        return first == "#!";
    }
    return false;
}

void MainWindow::updateStatus(const QString &msg)
{
    ui->statusbar->showMessage(msg, 8000);
}

// ---------------------------------------------------------------------------
// config persistence (File → Open / Save config)
// ---------------------------------------------------------------------------
void MainWindow::onFileOpen()
{
    // Start in the launcher's own Lists directory so choosing a list is
    // one click away.
    const QString listsDir = launcherConfigDir() + QStringLiteral("/Lists");
    QDir().mkpath(listsDir);

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open launcher config"),
        listsDir,
        tr("TOML config (*.toml);;All files (*)"));
    if (path.isEmpty()) return;
    if (!maybeSave()) return;
    loadConfigFile(path);
}

void MainWindow::loadConfigFile(const QString &path)
{
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        ui->statusbar->showMessage(
            tr("This is a JSON config. SmolLauncher reads TOML only; "
               "convert it to .toml before opening."),
            10000);
        return;
    }

    // Programmatic expand/collapse during loading must not autosave.
    struct LoadingGuard {
        bool &flag;
        explicit LoadingGuard(bool &f) : flag(f) { flag = true; }
        ~LoadingGuard() { flag = false; }
    } loadingGuard(m_loadingConfig);

    ConfigFile::ReadResult parsed = ConfigFile::read(path);
    Debug::log(QStringLiteral("loadConfigFile: \"%1\" error=%2")
        .arg(path)
        .arg(static_cast<int>(parsed.error)));

    bool recovered = false;
    if (parsed.error != ConfigFile::ReadError::None) {
        QString reason;
        switch (parsed.error) {
        case ConfigFile::ReadError::ParseError:
            reason = tr("Invalid TOML (%1:%2): %3")
                         .arg(parsed.errorPath)
                         .arg(static_cast<qulonglong>(parsed.errorLine))
                         .arg(parsed.errorDescription);
            break;
        case ConfigFile::ReadError::MissingCategories:
            reason = tr("Config has no [[categories]]: %1").arg(path);
            break;
        case ConfigFile::ReadError::UnsupportedTopLevelKey:
            reason = tr("Unsupported key in config; not loaded: %1").arg(path);
            break;
        case ConfigFile::ReadError::UnsupportedStructure:
            reason = tr("Unsupported config structure; not loaded: %1").arg(path);
            break;
        case ConfigFile::ReadError::None:
            break;
        }

        const QString backup = ConfigFile::backupPath(path);
        if (QFileInfo::exists(backup)) {
            const auto answer = QMessageBox::question(
                this, tr("Config error"),
                tr("Could not load the list:\n\n%1\n\n"
                   "A backup exists:\n%2\n\nLoad the backup instead?")
                    .arg(reason, backup),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes) {
                const ConfigFile::ReadResult fromBackup =
                    ConfigFile::read(backup);
                if (fromBackup.error == ConfigFile::ReadError::None) {
                    parsed = fromBackup;
                    recovered = true;
                }
            }
        }

        if (!recovered) {
            ui->statusbar->showMessage(reason, 8000);
            return;
        }
    }

    QList<QStandardItem *> staged;
    QList<bool> collapsedFlags;

    const QVariantList savedExpanded =
        launcherSettings().value(
            QStringLiteral("categoryExpanded/") + path).toList();

    int categoryIndex = 0;
    for (const ConfigFile::Category &category : parsed.categories) {
        auto *catItem = createCategoryItem(category.name, category.iconPath);

        for (const ConfigFile::App &entry : category.items) {
            if (entry.separator) {
                catItem->appendRow(createSeparatorItem());
                continue;
            }

            auto *app = createAppItem(entry.name);
            app->binaryPath = entry.binary;
            app->arguments = entry.arguments;
            app->workDir = entry.workDir;
            app->iconPath = entry.iconPath;
            app->desktopFileId = entry.desktopFileId;
            app->launchInTerminal = entry.launchInTerminal;
            app->launchWithDebugger = entry.launchWithDebugger;
            app->singleInstance = entry.singleInstance;
            app->isScript = entry.isScript;
            app->env = entry.env;

            if (!app->iconPath.isEmpty())
                app->setIcon(QIcon(app->iconPath));

            catItem->appendRow(app);
            updateBinaryIndicator(app);
        }

        bool expanded = !category.collapsed;
        if (categoryIndex < savedExpanded.size())
            expanded = savedExpanded.at(categoryIndex).toBool();
        ++categoryIndex;

        catItem->setData(expanded, ExpandedRole);
        staged.append(catItem);
        collapsedFlags.append(!expanded);
    }

    if (staged.isEmpty()) {
        // A config may contain `categories = []`; keep the UI usable and the
        // next save valid by giving it one empty category.
        auto *general = createCategoryItem(tr("General"));
        general->setData(true, ExpandedRole);
        staged.append(general);
        collapsedFlags.append(false);
    }

    clearUndoStack();
    m_filterField->clear();   // a new list should start unfiltered
    m_currentItem = nullptr;
    m_model->clear();
    EditorController(this).clear();
    for (int i = 0; i < staged.size(); ++i) {
        auto *cat = staged.at(i);
        m_model->appendRow(cat);
        const QModelIndex proxyIdx = toProxy(cat->index());
        if (proxyIdx.isValid()) {
            if (collapsedFlags.at(i)) ui->AppList->collapse(proxyIdx);
            else                      ui->AppList->expand(proxyIdx);
        }
    }

    m_configPath = path;
    m_dirty = recovered;
    launcherSettings().setValue("lastConfigPath", path);
    addToRecentLists(path);
    // Count broken entries for the status message
    int broken = 0;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto *cat = m_model->item(i);
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (app && !app->binaryPath.isEmpty() && !isBinaryValid(app->binaryPath))
            ++broken;
        }
    }

    QString msg = tr("Config loaded: %1").arg(path);
    if (recovered)
        msg += QLatin1Char(' ') + tr("(recovered from backup; save to keep it)");
    if (broken > 0)
        msg += QLatin1Char(' ') + tr("(%n broken)", "", broken);
    ui->statusbar->showMessage(msg, 8000);
    Debug::log(QStringLiteral("loadConfigFile done: categories=%1 broken=%2 recovered=%3 current=%4")
        .arg(parsed.categories.size())
        .arg(broken)
        .arg(recovered)
        .arg(itemDescription(m_currentItem)));
}

bool MainWindow::writeConfigTo(const QString &path)
{
    QString error;
    if (!ConfigFile::write(*m_model, path, &error)) {
        QMessageBox::warning(this, tr("SmolLauncher"), error);
        return false;
    }
    return true;
}

void MainWindow::setDirty(bool dirty)
{
    if (m_dirty != dirty)
        Debug::detail(QStringLiteral("dirty: %1 -> %2").arg(m_dirty).arg(dirty));
    m_dirty = dirty;
}

void MainWindow::saveCategoryExpansionState()
{
    if (m_loadingConfig || m_configPath.isEmpty())
        return;

    QVariantList states;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto *cat = m_model->item(i);
        if (!cat || cat->data(TypeRole).toInt() != TypeCategory)
            continue;
        const QVariant expanded = cat->data(ExpandedRole);
        states << (expanded.isValid() ? expanded.toBool() : true);
    }

    launcherSettings().setValue(
        QStringLiteral("categoryExpanded/") + m_configPath, states);
}

bool MainWindow::saveCurrentConfig()
{
    Debug::log(QStringLiteral("saveCurrentConfig: path=\"%1\"").arg(m_configPath));
    if (m_currentItem &&
        m_currentItem->data(TypeRole).toInt() == TypeApp) {
        EditorController(this).save(QStringLiteral("save-config"));  // flush the editor before serializing
    }

    QString path = m_configPath;
    if (path.isEmpty()) {
        path = launcherConfigFile();
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    if (!writeConfigTo(path))
        return false;           // keep the previous path/settings on failure

    m_configPath = path;
    m_dirty = false;
    launcherSettings().setValue("lastConfigPath", path);
    addToRecentLists(path);
    Debug::log(QStringLiteral("saveCurrentConfig done: \"%1\"").arg(path));
    return true;
}

bool MainWindow::maybeSave()
{
    Debug::detail(QStringLiteral("maybeSave: dirty=%1").arg(m_dirty));
    if (!m_dirty)
        return true;

    const auto ans = QMessageBox::warning(
        this, tr("Unsaved changes"),
        tr("The current list has unsaved changes.\n"
           "Do you want to save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    Debug::detail(QStringLiteral("maybeSave: answer=%1").arg(static_cast<int>(ans)));
    if (ans == QMessageBox::Cancel)
        return false;
    if (ans == QMessageBox::Discard)
        return true;
    return saveCurrentConfig();
}

void MainWindow::onSaveConfig()
{
    if (!saveCurrentConfig())
        return;
    ui->statusbar->showMessage(tr("Saved: %1").arg(m_configPath), 8000);
}

void MainWindow::onSaveConfigAs()
{
    const QString suggested = m_configPath.isEmpty()
        ? launcherConfigFile()
        : m_configPath;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save list as"),
        suggested,
        tr("TOML config (*.toml);;All files (*)"));
    if (path.isEmpty()) return;

    if (!path.endsWith(".toml", Qt::CaseInsensitive))
        path += ".toml";

    if (m_currentItem &&
        m_currentItem->data(TypeRole).toInt() == TypeApp) {
        EditorController(this).save(QStringLiteral("save-config-as"));  // flush before writing
    }

    if (!writeConfigTo(path)) return;      // keep previous m_configPath on failure

    m_configPath = path;
    m_dirty = false;
    launcherSettings().setValue("lastConfigPath", path);
    addToRecentLists(path);
    Debug::log(QStringLiteral("saved list as: \"%1\"").arg(path));
    ui->statusbar->showMessage(tr("Saved as: %1").arg(path), 8000);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!ui->actionFixedWindowSize->isChecked())
        saveFreeWindowSize();

    Debug::detail(QStringLiteral("closeEvent: dirty=%1 current=%2")
        .arg(m_dirty)
        .arg(itemDescription(m_currentItem)));
    if (!maybeSave()) {
        e->ignore();
        Debug::detail(QStringLiteral("closeEvent: cancelled"));
        return;
    }
    QMainWindow::closeEvent(e);
}

void MainWindow::onItemMoved(QStandardItem *item)
{
    if (!item) return;
    Debug::detail(QStringLiteral("onItemMoved: %1").arg(itemDescription(item)));
    ui->AppList->selectionModel()->clearSelection();
    ui->AppList->selectionModel()->clearCurrentIndex();

    QPersistentModelIndex srcIdx(item->index());
    QTimer::singleShot(0, this, [this, srcIdx]{
        if (!srcIdx.isValid()) return;
        const QModelIndex proxyIdx = toProxy(QModelIndex(srcIdx));  // ← toProxy
        if (!proxyIdx.isValid()) return;    // filtered out — silently OK
        const QModelIndex proxyParent = proxyIdx.parent();
        if (proxyParent.isValid())
            ui->AppList->expand(proxyParent);
        ui->AppList->setCurrentIndex(proxyIdx);
        ui->AppList->scrollTo(proxyIdx);
    });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateCatWidth();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        // retranslateUi() resets some designer line edits (e.g. BinField,
        // WorkDirField) to empty strings. Their textChanged handlers write
        // straight into the selected app, which used to erase its paths.
        // Block them during retranslation and reload the item afterwards.
        {
            QSignalBlocker b1(ui->AppNameField), b2(ui->ArgField),
                           b3(ui->IconField), b4(ui->WorkDirField),
                           b5(ui->BinField), b6(ui->categoryNameField),
                           b7(ui->categoryIconField), b8(ui->envVars);

            ui->retranslateUi(this);
        }

        // Code-created widgets are not covered by retranslateUi().
        m_filterField->setPlaceholderText(tr("Filter…"));
        retranslateCodeMenus();

        // Restore the editor from the item — it owns the real values.
        if (m_currentItem) {
            const int type = m_currentItem->data(TypeRole).toInt();
            if (type == TypeApp || type == TypeCategory) {
                EditorController editor(this);
                editor.loadItem(m_currentItem);
                editor.applyMode(type);  // re-applies label_5 / label_6
            }
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::onNewList()
{
    Debug::detail(QStringLiteral("onNewList: current=%1").arg(itemDescription(m_currentItem)));

    if (!maybeSave())
        return;

    // Ask for a name so the new list gets its own file instead of later
    // overwriting the list the user started from.
    bool ok = false;
    QString name = QInputDialog::getText(
        this, tr("New list"), tr("List name:"), QLineEdit::Normal,
        QString(), &ok).trimmed();
    if (!ok)
        return;

    QString safeName;
    for (QChar c : name) {
        safeName += (c == QLatin1Char('/') || c == QLatin1Char('\\')
                     || !c.isPrint()) ? QLatin1Char('-') : c;
    }
    safeName = safeName.trimmed();
    if (safeName.isEmpty())
        safeName = tr("new list");

    const QString listsDir = launcherConfigDir() + QStringLiteral("/Lists");
    QDir().mkpath(listsDir);
    const QString path = listsDir + QLatin1Char('/') + safeName
                         + QStringLiteral(".toml");

    if (QFileInfo::exists(path)) {
        const auto answer = QMessageBox::question(
            this, tr("New list"),
            tr("A list named \"%1\" already exists.\n"
               "Overwrite it?").arg(safeName));
        if (answer != QMessageBox::Yes)
            return;
    }

    m_currentItem = nullptr;
    EditorController(this).clear();

    m_filterField->clear();   // triggers onFilterChanged("") → filter + drag re-enabled
    clearUndoStack();
    m_model->clear();
    m_configPath = path;

    m_model->appendRow(createCategoryItem(tr("General")));
    ui->AppList->expandAll();
    m_selectionCommand.clear();
    ui->statusbar->clearMessage();

    if (!writeConfigTo(path)) {
        // Keep the in-memory list, but do not point it at a file we failed
        // to create.
        m_configPath.clear();
        setDirty(true);
        return;
    }

    launcherSettings().setValue("lastConfigPath", path);
    addToRecentLists(path);
    setDirty(false);

    Debug::log(QStringLiteral("new list: \"%1\"").arg(path));
    ui->statusbar->showMessage(tr("New list: %1").arg(safeName), 4000);
}

void MainWindow::setupLanguageSubmenu()
{
    m_languageMenu = new QMenu(tr("Language"), this);
    QMenu *langMenu = m_languageMenu;

    struct Lang { const char *label; const char *code; };
    const Lang langs[] = {
        { "English",    "en" },
        { "Русский",    "ru" }
    };

    auto *group = new QActionGroup(langMenu);
    group->setExclusive(true);

    const QString current = launcherSettings().value("language", "en").toString();

    for (const auto &l : langs) {
        auto *a = langMenu->addAction(QString::fromUtf8(l.label));
        a->setCheckable(true);
        a->setChecked(current == QLatin1String(l.code));
        a->setData(QString::fromLatin1(l.code));
        group->addAction(a);
    }

    connect(group, &QActionGroup::triggered, this, [this](QAction *a){
        const QString code = a->data().toString();
        launcherSettings().setValue("language", code);
        switchLanguage(code);
    });

    // Attach it to the menubar action — this is what makes it a real submenu
    ui->actionLanguage->setMenu(langMenu);
}

void MainWindow::switchLanguage(const QString &code)
{
    // ---- app translator -------------------------------------------------
    qApp->removeTranslator(m_appTranslator);
    delete m_appTranslator;
    m_appTranslator = nullptr;

    if (code != QLatin1String("en")) {
        m_appTranslator = new QTranslator(this);
        if (m_appTranslator->load(":/i18n/smollauncher_" + code + ".qm"))
            qApp->installTranslator(m_appTranslator);
        else {
            delete m_appTranslator;
            m_appTranslator = nullptr;
        }
    }

    // ---- Qt's own strings (file dialogs, message box buttons) -----------
    static QTranslator *qtTrans = nullptr;
    if (qtTrans) {
        qApp->removeTranslator(qtTrans);
        delete qtTrans;
        qtTrans = nullptr;
    }
    if (code != QLatin1String("en")) {
        qtTrans = new QTranslator(qApp);
        if (qtTrans->load(QLocale(code), "qt", "_",
                QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            qApp->installTranslator(qtTrans);
        else {
            delete qtTrans;
            qtTrans = nullptr;
        }
    }
}

void MainWindow::applyFixedWindowSize(bool fixed)
{
    if (fixed) {
        // Always return to the original 900x600 design when this is enabled.
        resize(QSize(900, 600));
        setFixedSize(QSize(900, 600));
    } else {
        // Let the window shrink to the layout's minimum and grow freely.
        setMinimumSize(QSize(0, 0));
        setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));

        const QSettings settings = launcherSettings();
        const int width = settings.value("freeWindowWidth", 0).toInt();
        const int height = settings.value("freeWindowHeight", 0).toInt();
        if (width > 0 && height > 0)
            resize(width, height);
    }
    updateCatVisibility();
}

void MainWindow::saveFreeWindowSize()
{
    const QSize size = (isMaximized() || isFullScreen())
        ? normalGeometry().size()
        : QSize(width(), height());

    QSettings settings = launcherSettings();
    settings.setValue("freeWindowWidth", size.width());
    settings.setValue("freeWindowHeight", size.height());
}

void MainWindow::updateCatWidth()
{
    if (!m_cat)
        return;

    // Natural size in the default 900 px window, then widen 1:1 with it,
    // but never past the editor page.
    const int natural = m_cat->sizeHint().width();
    const int available = qMax(natural, ui->mainTab->width() - 20);
    const int target = qBound(natural, natural + qMax(0, width() - 900),
                              available);
    if (m_cat->width() != target)
        m_cat->setFixedWidth(target);
}

void MainWindow::updateCatVisibility()
{
    if (!m_cat)
        return;

    const bool requested =
        qEnvironmentVariable("SL_EBAKOT") == QLatin1String("1");
    m_cat->setVisible(requested && !ui->actionFixedWindowSize->isChecked());
    updateCatWidth();
}

void MainWindow::applyIconSize(int px)
{
    if (px < 16) px = 16;
    if (px > 64) px = 64;

    ui->AppList->setIconSize(QSize(px, px));

    // Grow the row so icons aren't clipped by the font-derived height.
    const int rowHeight = px + 8;
    ui->AppList->setStyleSheet(
        QStringLiteral("QTreeView::item { min-height: %1px; }").arg(rowHeight));
}

void MainWindow::setupIconSizeActions()
{
    struct Entry { QAction *action; int px; };
    const Entry entries[] = {
        { ui->actionIconS16, 16 },
        { ui->actionIconS24, 24 },
        { ui->actionIconS32, 32 },
        { ui->actionIconS48, 48 },
    };

    // Exclusivity is what makes checkable actions behave like a radio group:
    // checking one auto-unchecks the others, and re-clicking the checked one
    // does nothing. This CANNOT be expressed in the .ui alone.
    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    const int saved = launcherSettings().value("iconSize", 24).toInt();

    for (const auto &e : entries) {
        e.action->setData(e.px);
        group->addAction(e.action);
        if (e.px == saved)
            e.action->setChecked(true);
    }

    connect(group, &QActionGroup::triggered, this, [this](QAction *a){
        const int px = a->data().toInt();
        applyIconSize(px);
        launcherSettings().setValue("iconSize", px);
    });
}

void MainWindow::addToRecentLists(const QString &path)
{
    if (path.isEmpty()) return;

    QSettings s = launcherSettings();
    QStringList list = s.value("recentLists").toStringList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecent)
        list.removeLast();
    s.setValue("recentLists", list);
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();

    QSettings settings = launcherSettings();
    QStringList list = settings.value("recentLists").toStringList();

    // Drop entries whose files are gone; the menu must not accumulate stale
    // paths forever.
    QStringList existing;
    for (const QString &path : list) {
        if (QFileInfo(path).isFile())
            existing << path;
    }
    if (existing != list) {
        settings.setValue("recentLists", existing);
        list = existing;
    }

    if (list.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("(no recent lists)"));
        empty->setEnabled(false);
        return;
    }

    for (const QString &path : list) {
        const QFileInfo fi(path);
        const bool exists = fi.exists();

        QAction *a = m_recentMenu->addAction(fi.fileName());
        a->setToolTip(path);
        a->setData(path);
        a->setEnabled(exists);

        if (path == m_configPath) {
            a->setCheckable(true);
            a->setChecked(true);
        }

        connect(a, &QAction::triggered, this, [this, path, exists]{
            if (!exists) {
                QMessageBox::warning(
                    this, tr("SmolLauncher"),
                    tr("File not found:\n%1").arg(path));
                return;
            }
            if (!maybeSave()) return;
            loadConfigFile(path);
        });
    }

    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(tr("Clear recent lists"));
    connect(clear, &QAction::triggered, this, [this]{
        launcherSettings().remove("recentLists");
    });
}

void MainWindow::onTreeContextMenu(const QPoint &pos)
{
    ContextMenuController(this).showMenu(pos);
}

bool MainWindow::isBinaryValid(const QString &path) const
{
    if (path.isEmpty()) return false;

    // Absolute path or path with a slash → check the file directly
    if (path.contains('/') || QFileInfo(path).isAbsolute()) {
        const QFileInfo fi(path);
        return fi.exists() && fi.isFile() && fi.isExecutable();
    }

    // Bare name → resolve against PATH
    return !QStandardPaths::findExecutable(path).isEmpty();
}

void MainWindow::updateBinaryIndicator(AppItem *app)
{
    if (!app) return;

    // Work out the desired state first and only touch roles that actually
    // change. Writing unconditionally re-emits itemChanged and recurses
    // through the itemChanged handler.
    QVariant colour;
    QString tooltip;

    if (!app->binaryPath.isEmpty()) {
        if (isBinaryValid(app->binaryPath)) {
            tooltip = app->text();
        } else {
            // Invalid — red name, diagnostic tooltip
            colour = QColor(QStringLiteral("#d32f2f"));
            tooltip = tr("Binary not found or not executable:\n%1")
                          .arg(app->binaryPath);
        }
    }

    if (app->data(Qt::ForegroundRole) != colour)
        app->setData(colour, Qt::ForegroundRole);
    if (app->toolTip() != tooltip)
        app->setToolTip(tooltip);
}

QModelIndex MainWindow::toProxy(const QModelIndex &sourceIdx) const
{
    return m_proxy ? m_proxy->mapFromSource(sourceIdx) : sourceIdx;
}

QModelIndex MainWindow::fromProxy(const QModelIndex &proxyIdx) const
{
    return m_proxy ? m_proxy->mapToSource(proxyIdx) : proxyIdx;
}

void MainWindow::onFilterChanged(const QString &text)
{
    m_applyingFilter = true;
    m_proxy->setFilterText(text);
    const bool filtering = !m_proxy->filterText().isEmpty();

    // Dragging while filtered is confusing (the dropped item may be hidden)
    ui->AppList->setDragEnabled(!filtering);

    // While filtering, show everything that matches. On clear, restore the
    // user's own expand/collapse state instead of keeping expandAll().
    if (filtering) {
        ui->AppList->expandAll();
    } else {
        for (int i = 0; i < m_model->rowCount(); ++i) {
            auto *cat = m_model->item(i);
            if (cat->data(TypeRole).toInt() != TypeCategory)
                continue;
            const QModelIndex proxy = toProxy(cat->index());
            if (!proxy.isValid())
                continue;
            if (cat->data(ExpandedRole).toBool())
                ui->AppList->expand(proxy);
            else
                ui->AppList->collapse(proxy);
        }
    }
    m_applyingFilter = false;
}

void MainWindow::sortCategoryApps(QStandardItem *cat)
{
    if (!cat || cat->data(TypeRole).toInt() != TypeCategory) return;

    int appCount = 0;
    for (int i = 0; i < cat->rowCount(); ++i)
        if (cat->child(i)->data(TypeRole).toInt() == TypeApp)
            ++appCount;
    if (appCount < 2) return;

    UndoEntry undo;
    undo.kind = UndoKind::Sorted;
    undo.description = tr("Sort \"%1\"").arg(cat->text());
    QList<QStandardItem *> order;
    for (int i = 0; i < cat->rowCount(); ++i)
        order << cat->child(i);
    undo.orders.append({ cat, order });
    pushUndo(undo);

    const int sorted = sortCategoryAppsCore(cat);
    if (sorted >= 2) {
        ui->AppList->setFocus();
        updateStatus(tr("Sorted %1 entries in \"%2\"")
                         .arg(sorted).arg(cat->text()));
    }
}

int MainWindow::sortCategoryAppsCore(QStandardItem *cat)
{
    if (!cat || cat->data(TypeRole).toInt() != TypeCategory) return 0;

    // Count apps — used both for the early-out and the status message
    int appCount = 0;
    for (int i = 0; i < cat->rowCount(); ++i)
        if (cat->child(i)->data(TypeRole).toInt() == TypeApp)
            ++appCount;
    if (appCount < 2) return 0;

    // Preserve the currently-selected app (if it's inside this category)
    QStandardItem *selected = nullptr;
    if (m_currentItem && m_currentItem->parent() == cat)
        selected = m_currentItem;

    // Detach all children — taking them detaches them from the model, so
    // re-appending later re-parents them without any item re-creation.
    QList<QStandardItem *> all;
    while (cat->rowCount() > 0)
        all.append(cat->takeRow(0).first());

    // Walk the original order; every contiguous run of apps is sorted, then
    // the separator that ended the run is re-appended in place. This keeps
    // the visual sections the user created by hand.
    QList<QStandardItem *> result;
    QList<QStandardItem *> buffer;

    auto flush = [&]() {
        std::sort(buffer.begin(), buffer.end(),
                  [](QStandardItem *a, QStandardItem *b){
            // Case-insensitive, locale-aware; falls back to case-sensitive
            // for stable ordering of "Alpha" vs "alpha".
            const int c = QString::localeAwareCompare(
                a->text().toLower(), b->text().toLower());
            if (c != 0) return c < 0;
            return QString::localeAwareCompare(a->text(), b->text()) < 0;
        });
        result.append(buffer);
        buffer.clear();
    };

    for (auto *it : all) {
        if (it->data(TypeRole).toInt() == TypeSeparator) {
            flush();
            result.append(it);
        } else {
            buffer.append(it);
        }
    }
    flush();

    // Reattach in the sorted order
    for (auto *it : result)
        cat->appendRow(it);

    // Restore selection + scroll so the user's position is preserved
    if (selected) {
        const QModelIndex proxyIdx = toProxy(selected->index());
        if (proxyIdx.isValid()) {
            ui->AppList->setCurrentIndex(proxyIdx);
            ui->AppList->scrollTo(proxyIdx);
        }
    }

    return appCount;
}

void MainWindow::sortAllCategories()
{
    QList<QPair<QStandardItem *, QList<QStandardItem *>>> orders;

    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto *cat = m_model->item(i);
        if (cat->data(TypeRole).toInt() != TypeCategory)
            continue;

        int appCount = 0;
        for (int j = 0; j < cat->rowCount(); ++j)
            if (cat->child(j)->data(TypeRole).toInt() == TypeApp)
                ++appCount;
        if (appCount < 2)
            continue;

        QList<QStandardItem *> order;
        for (int j = 0; j < cat->rowCount(); ++j)
            order << cat->child(j);
        orders.append({ cat, order });
    }

    if (orders.isEmpty())
        return;

    UndoEntry undo;
    undo.kind = UndoKind::Sorted;
    undo.description = tr("Sort all categories");
    undo.orders = orders;
    pushUndo(undo);

    int categories = 0;
    int apps = 0;
    for (const auto &pair : orders) {
        const int sorted = sortCategoryAppsCore(pair.first);
        if (sorted >= 2) {
            ++categories;
            apps += sorted;
        }
    }

    if (categories > 0) {
        ui->AppList->setFocus();
        updateStatus(tr("Sorted %1 entries in %2 categories")
                         .arg(apps).arg(categories));
    }
}

void MainWindow::pushUndo(const UndoEntry &entry)
{
    m_undoStack.append(entry);
    while (m_undoStack.size() > kMaxUndo) {
        const UndoEntry old = m_undoStack.takeFirst();
        if (old.kind == UndoKind::Removed && old.item)
            delete old.item;
    }
}

void MainWindow::clearUndoStack()
{
    for (const UndoEntry &entry : std::as_const(m_undoStack)) {
        if (entry.kind == UndoKind::Removed && entry.item)
            delete entry.item;
    }
    m_undoStack.clear();
}

void MainWindow::undoLastAction()
{
    if (m_undoStack.isEmpty()) {
        updateStatus(tr("Nothing to undo"));
        return;
    }

    const UndoEntry undo = m_undoStack.takeLast();

    if (undo.kind == UndoKind::Removed) {
        if (undo.parent && undo.parent->model() != m_model) {
            // Should not happen with the stack order; keep the entry so the
            // item is not lost.
            m_undoStack.append(undo);
            QMessageBox::warning(this, tr("Undo"),
                                 tr("Cannot undo this removal."));
            return;
        }

        if (undo.parent) {
            const int row = qBound(0, undo.row, undo.parent->rowCount());
            undo.parent->insertRow(row, undo.item);
        } else {
            const int row = qBound(0, undo.row, m_model->rowCount());
            m_model->insertRow(row, undo.item);
        }

        const QModelIndex proxy = toProxy(undo.item->index());
        if (proxy.isValid()) {
            ui->AppList->setCurrentIndex(proxy);
            ui->AppList->scrollTo(proxy);
        }
    } else {
        for (const auto &pair : undo.orders) {
            auto *cat = pair.first;
            if (!cat || cat->model() != m_model)
                continue;

            QList<QStandardItem *> current;
            while (cat->rowCount() > 0)
                current.append(cat->takeRow(0).first());

            QSet<QStandardItem *> currentSet;
            for (QStandardItem *item : current)
                currentSet.insert(item);

            QSet<QStandardItem *> ordered;
            for (QStandardItem *item : pair.second) {
                if (currentSet.contains(item)) {
                    cat->appendRow(item);
                    ordered.insert(item);
                }
            }
            // Items added after the sort (or moved here) stay at the end.
            for (QStandardItem *item : current) {
                if (!ordered.contains(item))
                    cat->appendRow(item);
            }
        }

        if (m_currentItem) {
            const QModelIndex proxy = toProxy(m_currentItem->index());
            if (proxy.isValid()) {
                ui->AppList->setCurrentIndex(proxy);
                ui->AppList->scrollTo(proxy);
            }
        }
    }

    setDirty();
    updateStatus(tr("Undone: %1").arg(undo.description));
}

void MainWindow::refreshSelectionStatus()
{
    // Only apps have a command line to display
    if (!m_currentItem || m_currentItem->data(TypeRole).toInt() != TypeApp) {
        m_selectionCommand.clear();
        ui->statusbar->clearMessage();
        return;
    }

    auto *app = static_cast<AppItem *>(m_currentItem);

    // Configured command = binary + args. This is what the user wrote, not
    // the wrapper that LaunchController::launch() might add (terminal / gdb). The wrapper
    // is an execution detail, not part of the entry's identity.
    QString cmd = app->binaryPath;
    if (!app->arguments.isEmpty())
        cmd += QLatin1Char(' ') + CommandLine::joinArgs(app->arguments);

    if (cmd.isEmpty()) {
        // Fresh app with no binary yet — nothing to show
        m_selectionCommand.clear();
        ui->statusbar->clearMessage();
        return;
    }

    m_selectionCommand = cmd;
    ui->statusbar->showMessage(cmd);   // no timeout → persistent
}

AppItem *MainWindow::findExistingApp(const QString &binary,
                                     const QStringList &arguments) const
{
    if (binary.isEmpty()) return nullptr;

    for (int i = 0; i < m_model->rowCount(); ++i) {
        auto *cat = m_model->item(i);
        if (cat->data(TypeRole).toInt() != TypeCategory) continue;
        for (int j = 0; j < cat->rowCount(); ++j) {
            auto *app = dynamic_cast<AppItem *>(cat->child(j));
            if (!app) continue;
            if (app->binaryPath == binary && app->arguments == arguments)
                return app;
        }
    }
    return nullptr;
}

void MainWindow::onImportFromSteam()
{
    ImportController(this).fromSteam();
}

void MainWindow::onImportFromHeroicEpic()
{
    ImportController(this).fromHeroicEpic();
}

void MainWindow::onImportFromHeroicGog()
{
    ImportController(this).fromHeroicGog();
}

void MainWindow::onImportFromItch()
{
    ImportController(this).fromItch();
}

void MainWindow::onImportFromRetroArch()
{
    ImportController(this).fromRetroArch();
}

void MainWindow::onImportFromPrism()
{
    ImportController(this).fromPrism();
}

void MainWindow::setupImportSubmenu()
{
    m_importMenu = new QMenu(tr("Import games"), this);
    QMenu *importMenu = m_importMenu;

    importMenu->addAction(tr("Steam…"),
        this, &MainWindow::onImportFromSteam);
#ifndef Q_OS_WIN
    importMenu->addAction(tr("Heroic — Epic Games…"),
        this, &MainWindow::onImportFromHeroicEpic);
    importMenu->addAction(tr("Heroic — GOG…"),
        this, &MainWindow::onImportFromHeroicGog);
#endif
    importMenu->addAction(tr("itch.io…"),
        this, &MainWindow::onImportFromItch);
    importMenu->addAction(tr("RetroArch…"),
        this, &MainWindow::onImportFromRetroArch);
    importMenu->addAction(tr("Prism Launcher…"),
        this, &MainWindow::onImportFromPrism);

    ui->actionImportApps->setMenu(importMenu);
}

void MainWindow::retranslateCodeMenus()
{
    if (m_languageMenu)
        m_languageMenu->setTitle(tr("Language"));

    if (!m_importMenu)
        return;

    m_importMenu->setTitle(tr("Import games"));

    // Keep this order in sync with setupImportSubmenu().
    const QStringList texts = {
        tr("Steam…"),
#ifndef Q_OS_WIN
        tr("Heroic — Epic Games…"),
        tr("Heroic — GOG…"),
#endif
        tr("itch.io…"),
        tr("RetroArch…"),
        tr("Prism Launcher…"),
    };
    const QList<QAction *> actions = m_importMenu->actions();
    for (int i = 0; i < actions.size() && i < texts.size(); ++i)
        actions[i]->setText(texts[i]);
}

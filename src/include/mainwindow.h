#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QHash>
#include <QProcess>
#include <QTranslator>
#include <QLineEdit>

#include "appitem.h"
#include "launchermodel.h"
#include "launcherfilterproxy.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class ImportController;
class LaunchController;
class ContextMenuController;
class EditorController;
class CatWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onAddApplication();
    void onAddCategory();
    void onAddSeparator();
    void onAddFromDesktop();
    void onMoveUp();
    void onMoveDown();
    void onRemoveFromList();
    void onOpenDirectory();

    void onNewList();

    void onTreeSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void onTreeDoubleClicked(const QModelIndex &index);

    void onFileOpen();
    void onSaveConfig();
    void onSaveConfigAs();

    void onItemMoved(QStandardItem *item);

    void onLaunchApp();
    void onTreeContextMenu(const QPoint &pos);
    void onFilterChanged(const QString &text);

    // importers
    void onImportFromSteam();
    void onImportFromHeroicEpic();
    void onImportFromHeroicGog();
    void onImportFromItch();
    void onImportFromRetroArch();
    void onImportFromPrism();

private:
    friend class ImportController;
    friend class LaunchController;
    friend class ContextMenuController;
    friend class EditorController;

    Ui::MainWindow *ui;
    LauncherModel *m_model = nullptr;      // was QStandardItemModel*
    QStandardItem *m_currentItem = nullptr;
    QHash<QString, QProcess *> m_running;

    QString m_configPath;               // last loaded / saved path
    bool    m_dirty = false;
    int     m_dirtySuppress = 0;   // >0 while applying internal updates
    bool    m_loadingConfig = false;   // expand/collapse during load is not autosaved

    void loadConfigFile(const QString &path);   // extracted from onFileOpen
    void setDirty(bool dirty = true);
    void saveCategoryExpansionState();
    bool maybeSave();                   // false when the user cancels
    bool saveCurrentConfig();           // flush editor + atomic write

    // helpers ------------------------------------------------------------
    void setupModel();
    void setupConnections();
    void setupLanguageSubmenu();

    QStandardItem *createCategoryItem(const QString &name,
                                      const QString &iconPath = QString());
    void applyCategoryIcon(QStandardItem *cat);

    QStandardItem *createSeparatorItem();
    AppItem       *createAppItem(const QString &name);

    void updateStatus(const QString &msg);
    bool writeConfigTo(const QString &path);

    static bool isScriptFile(const QString &path);

    QStandardItem *categoryForInsert() const;
    QStandardItem *selectedCategory() const;

    void switchLanguage(const QString &code);
    QTranslator *m_appTranslator = nullptr;

    CatWidget *m_cat = nullptr;

    void setupIconSizeActions();
    void setupImportSubmenu();
    void applyIconSize(int px);

    void applyFixedWindowSize(bool fixed);
    void updateCatVisibility();
    void updateCatWidth();

    void addToRecentLists(const QString &path);
    void rebuildRecentMenu();

    QMenu *m_recentMenu = nullptr;
    static constexpr int kMaxRecent = 10;

    bool isBinaryValid(const QString &path) const;
    void updateBinaryIndicator(AppItem *app);

    QModelIndex toProxy(const QModelIndex &sourceIdx) const;
    QModelIndex fromProxy(const QModelIndex &proxyIdx) const;

    LauncherFilterProxy *m_proxy       = nullptr;
    QLineEdit           *m_filterField = nullptr;
    
    void sortCategoryApps(QStandardItem *cat);
    void refreshSelectionStatus();

    QString m_selectionCommand;      // empty when no app is selected

    AppItem *findExistingApp(const QString &binary,
                             const QStringList &arguments) const;
};

#endif // MAINWINDOW_H
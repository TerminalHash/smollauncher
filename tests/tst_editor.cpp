#include <QtTest>

#include "include/mainwindow.h"
#include "include/appitem.h"
#include "include/launcherfilterproxy.h"
#include "include/launchermodel.h"
#include "include/terminalsettings.h"
#include "include/terminalsettingsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeView>

namespace {

const char kConfig[] = R"(# test list
[[categories]]
name = 'Test'

[[categories.items]]
name = 'Game A'
binary = '/opt/gameA/bin'
workDir = '/opt/gameA'

[categories.items.env]
A = '1'
B = '2'

[[categories.items]]
name = 'Game B'
binary = '/opt/gameB/bin'
workDir = '/opt/gameB'
)";

} // namespace

class EditorTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();

    void languageChangeKeepsEditorPaths();
    void removingEnvVarUpdatesEntry();
    void terminalPrefixesAreCorrect();
    void terminalSettingsRoundTrip();
    void terminalDialogReflectsSettings();

private:
    AppItem *selectFirstApp(MainWindow &window);

    QTemporaryDir m_dir;
};

void EditorTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // Keep logs, settings and test lists away from the real user config.
    qputenv("SL_CONFIG_DIR", m_dir.path().toLocal8Bit());
}

void EditorTest::init()
{
    const QString listPath = m_dir.path() + QStringLiteral("/list.toml");
    QFile list(listPath);
    QVERIFY(list.open(QIODevice::WriteOnly));
    list.write(kConfig);
    list.close();

    QSettings settings(m_dir.path() + QStringLiteral("/SmolLauncher.conf"),
                       QSettings::IniFormat);
    settings.clear();
    settings.setValue(QStringLiteral("lastConfigPath"), listPath);
    settings.setValue(QStringLiteral("language"), QStringLiteral("en"));
    settings.setValue(QStringLiteral("fixedWindowSize"), true);
    settings.sync();
}

AppItem *EditorTest::selectFirstApp(MainWindow &window)
{
    auto *tree = window.findChild<QTreeView *>(QStringLiteral("AppList"));
    if (!tree)
        return nullptr;

    tree->expandAll();
    QAbstractItemModel *viewModel = tree->model();
    auto *proxy = qobject_cast<LauncherFilterProxy *>(viewModel);
    auto *source = proxy ? qobject_cast<QStandardItemModel *>(proxy->sourceModel())
                         : nullptr;
    if (!proxy || !source)
        return nullptr;

    const QModelIndex category = proxy->index(0, 0);
    const QModelIndex appIndex = proxy->index(0, 0, category);
    if (!category.isValid() || !appIndex.isValid())
        return nullptr;

    tree->setCurrentIndex(appIndex);
    return dynamic_cast<AppItem *>(
        source->itemFromIndex(proxy->mapToSource(appIndex)));
}

void EditorTest::languageChangeKeepsEditorPaths()
{
    MainWindow window;
    AppItem *app = selectFirstApp(window);
    QVERIFY(app);

    auto *binary = window.findChild<QLineEdit *>(QStringLiteral("BinField"));
    auto *workDir = window.findChild<QLineEdit *>(QStringLiteral("WorkDirField"));
    QVERIFY(binary);
    QVERIFY(workDir);
    QCOMPARE(binary->text(), QStringLiteral("/opt/gameA/bin"));
    QCOMPARE(workDir->text(), QStringLiteral("/opt/gameA"));

    binary->setText(QStringLiteral("/opt/gameA/bin2"));
    QCOMPARE(app->binaryPath, QStringLiteral("/opt/gameA/bin2"));

    QEvent event(QEvent::LanguageChange);
    QApplication::sendEvent(&window, &event);

    QCOMPARE(binary->text(), QStringLiteral("/opt/gameA/bin2"));
    QCOMPARE(workDir->text(), QStringLiteral("/opt/gameA"));
    QCOMPARE(app->binaryPath, QStringLiteral("/opt/gameA/bin2"));
    QCOMPARE(app->workDir, QStringLiteral("/opt/gameA"));
}

void EditorTest::removingEnvVarUpdatesEntry()
{
    MainWindow window;
    AppItem *app = selectFirstApp(window);
    QVERIFY(app);

    auto *env = window.findChild<QTableWidget *>(QStringLiteral("envVars"));
    auto *remove = window.findChild<QPushButton *>(QStringLiteral("RemoveEnvBtn"));
    QVERIFY(env);
    QVERIFY(remove);
    QCOMPARE(env->rowCount(), 2);
    QVERIFY(app->env.contains(QStringLiteral("A")));
    QVERIFY(app->env.contains(QStringLiteral("B")));

    env->setCurrentCell(0, 0);
    remove->click();

    QCOMPARE(env->rowCount(), 1);
    QVERIFY(!app->env.contains(QStringLiteral("A")));
    QVERIFY(app->env.contains(QStringLiteral("B")));
}

void EditorTest::terminalPrefixesAreCorrect()
{
    QCOMPARE(TerminalSettings::commandPrefix(QStringLiteral("gnome-terminal")),
             QStringList({ QStringLiteral("--") }));
    QCOMPARE(TerminalSettings::commandPrefix(QStringLiteral("xfce4-terminal")),
             QStringList({ QStringLiteral("-x") }));
    QCOMPARE(TerminalSettings::commandPrefix(QStringLiteral("konsole")),
             QStringList({ QStringLiteral("-e") }));
    QCOMPARE(TerminalSettings::commandPrefix(QStringLiteral("xterm")),
             QStringList({ QStringLiteral("-e") }));
}

void EditorTest::terminalSettingsRoundTrip()
{
    TerminalSettings::Config config;
    config.program    = QStringLiteral("konsole");
    config.custom     = true;
    config.customBin  = QStringLiteral("/opt/term");
    config.customArgs = QStringLiteral("-e --hold");
    TerminalSettings::save(config);

    const TerminalSettings::Config loaded = TerminalSettings::load();
    QCOMPARE(loaded.program, config.program);
    QCOMPARE(loaded.custom, true);
    QCOMPARE(loaded.customBin, config.customBin);
    QCOMPARE(loaded.customArgs, config.customArgs);
}

void EditorTest::terminalDialogReflectsSettings()
{
    TerminalSettings::Config config;
    config.program    = QStringLiteral("xterm");
    config.custom     = true;
    config.customBin  = QStringLiteral("/bin/term");
    config.customArgs = QStringLiteral("-e");
    TerminalSettings::save(config);

    TerminalSettingsDialog dlg;
    auto *combo = dlg.findChild<QComboBox *>(QStringLiteral("terminalCombo"));
    auto *custom = dlg.findChild<QCheckBox *>(QStringLiteral("customCheck"));
    auto *bin = dlg.findChild<QLineEdit *>(QStringLiteral("binField"));
    auto *args = dlg.findChild<QLineEdit *>(QStringLiteral("argsField"));
    QVERIFY(combo);
    QVERIFY(custom);
    QVERIFY(bin);
    QVERIFY(args);

    QCOMPARE(combo->currentData().toString(), QStringLiteral("xterm"));
    QCOMPARE(custom->isChecked(), true);
    QCOMPARE(combo->isEnabled(), false);
    QCOMPARE(bin->text(), QStringLiteral("/bin/term"));
    QCOMPARE(bin->isEnabled(), true);
    QCOMPARE(args->text(), QStringLiteral("-e"));
    QCOMPARE(args->isEnabled(), true);

    custom->setChecked(false);
    QCOMPARE(combo->isEnabled(), true);
    QCOMPARE(bin->isEnabled(), false);
    QCOMPARE(args->isEnabled(), false);
}

QTEST_MAIN(EditorTest)
#include "tst_editor.moc"

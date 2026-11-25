#include <QtTest>

#include "include/configfile.h"
#include "include/appitem.h"
#include "include/launchermodel.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardItemModel>
#include <QTemporaryDir>

namespace {

QString writeFile(QTemporaryDir &dir, const QByteArray &body,
                  const QString &name = QStringLiteral("config.toml"))
{
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(body);
    return path;
}

ConfigFile::ReadError errorFor(QTemporaryDir &dir, const QByteArray &body)
{
    return ConfigFile::read(writeFile(dir, body)).error;
}

} // namespace

class ConfigFileTest : public QObject
{
    Q_OBJECT
private slots:
    void writesAndReadsRoundTrip();
    void writesEmptyCategoryList();
    void keepsPreviousVersionAsBackup();
    void roundTripsControlCharacters();
    void rejectsMissingCategories();
    void rejectsUnknownKeys();
    void rejectsWrongTypes();
    void rejectsBadArguments();
    void rejectsBadEnvironment();
    void reportsParseErrors();
};

void ConfigFileTest::writesAndReadsRoundTrip()
{
    QStandardItemModel model;

    auto *cat = new QStandardItem(QStringLiteral("Cat"));
    cat->setData(TypeCategory, TypeRole);
    cat->setData(QStringLiteral("/icons/cat.png"), IconPathRole);
    cat->setData(false, ExpandedRole);      // collapsed
    model.appendRow(cat);

    auto *app = new AppItem(QStringLiteral("Game"));
    app->setData(TypeApp, TypeRole);
    app->binaryPath     = QStringLiteral("/opt/game/bin");
    app->workDir        = QStringLiteral("/opt/game");
    app->arguments      = { QStringLiteral("--fullscreen"),
                            QStringLiteral("a b"),
                            QStringLiteral("c'd") };
    app->iconPath       = QStringLiteral("/opt/game/icon.png");
    app->desktopFileId  = QStringLiteral("game.desktop");
    app->env.insert(QStringLiteral("A"), QStringLiteral("1"));
    app->env.insert(QStringLiteral("WEIRD KEY"), QStringLiteral("v=1"));
    app->isScript           = true;
    app->launchInTerminal   = true;
    app->launchWithDebugger = false;
    app->singleInstance     = false;
    cat->appendRow(app);

    auto *separator = new QStandardItem;
    separator->setData(TypeSeparator, TypeRole);
    cat->appendRow(separator);

    auto *emptyCat = new QStandardItem(QStringLiteral("Empty"));
    emptyCat->setData(TypeCategory, TypeRole);
    model.appendRow(emptyCat);

    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/list.toml");
    QString error;
    QVERIFY2(ConfigFile::write(model, path, &error), qPrintable(error));

    const ConfigFile::ReadResult result = ConfigFile::read(path);
    QCOMPARE(result.error, ConfigFile::ReadError::None);
    QCOMPARE(result.categories.size(), 2);

    const ConfigFile::Category &readCat = result.categories.at(0);
    QCOMPARE(readCat.name, QStringLiteral("Cat"));
    QCOMPARE(readCat.iconPath, QStringLiteral("/icons/cat.png"));
    QCOMPARE(readCat.collapsed, true);
    QCOMPARE(readCat.items.size(), 2);

    const ConfigFile::App &readApp = readCat.items.at(0);
    QCOMPARE(readApp.separator, false);
    QCOMPARE(readApp.name, QStringLiteral("Game"));
    QCOMPARE(readApp.binary, QStringLiteral("/opt/game/bin"));
    QCOMPARE(readApp.workDir, QStringLiteral("/opt/game"));
    QCOMPARE(readApp.arguments, app->arguments);
    QCOMPARE(readApp.iconPath, QStringLiteral("/opt/game/icon.png"));
    QCOMPARE(readApp.desktopFileId, QStringLiteral("game.desktop"));
    QCOMPARE(readApp.env.value(QStringLiteral("A")), QStringLiteral("1"));
    QCOMPARE(readApp.env.value(QStringLiteral("WEIRD KEY")), QStringLiteral("v=1"));
    QCOMPARE(readApp.isScript, true);
    QCOMPARE(readApp.launchInTerminal, true);
    QCOMPARE(readApp.launchWithDebugger, false);
    QCOMPARE(readApp.singleInstance, false);

    QCOMPARE(readCat.items.at(1).separator, true);
    QCOMPARE(result.categories.at(1).name, QStringLiteral("Empty"));
    QCOMPARE(result.categories.at(1).collapsed, false);
}

void ConfigFileTest::writesEmptyCategoryList()
{
    QStandardItemModel model;
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/empty.toml");

    QString error;
    QVERIFY2(ConfigFile::write(model, path, &error), qPrintable(error));

    const ConfigFile::ReadResult result = ConfigFile::read(path);
    QCOMPARE(result.error, ConfigFile::ReadError::None);
    QCOMPARE(result.categories.size(), 0);
}

void ConfigFileTest::keepsPreviousVersionAsBackup()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/list.toml");
    const QString backup = ConfigFile::backupPath(path);

    QStandardItemModel oldModel;
    auto *oldCat = new QStandardItem(QStringLiteral("Old"));
    oldCat->setData(TypeCategory, TypeRole);
    oldModel.appendRow(oldCat);

    QStandardItemModel newModel;
    auto *newCat = new QStandardItem(QStringLiteral("New"));
    newCat->setData(TypeCategory, TypeRole);
    newModel.appendRow(newCat);

    QString error;
    QVERIFY2(ConfigFile::write(oldModel, path, &error), qPrintable(error));
    QVERIFY2(ConfigFile::write(newModel, path, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(backup));

    const ConfigFile::ReadResult fromBackup = ConfigFile::read(backup);
    QCOMPARE(fromBackup.error, ConfigFile::ReadError::None);
    QCOMPARE(fromBackup.categories.size(), 1);
    QCOMPARE(fromBackup.categories.first().name, QStringLiteral("Old"));

    // A corrupt current file must not replace the good backup.
    QFile broken(path);
    QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
    broken.write("[[categories\n");
    broken.close();
    QVERIFY2(ConfigFile::write(newModel, path, &error), qPrintable(error));

    const ConfigFile::ReadResult stillOld = ConfigFile::read(backup);
    QCOMPARE(stillOld.error, ConfigFile::ReadError::None);
    QCOMPARE(stillOld.categories.first().name, QStringLiteral("Old"));
}

void ConfigFileTest::roundTripsControlCharacters()
{
    QStandardItemModel model;
    auto *cat = new QStandardItem(QStringLiteral("Cat"));
    cat->setData(TypeCategory, TypeRole);
    model.appendRow(cat);

    auto *app = new AppItem(QStringLiteral("App"));
    app->setData(TypeApp, TypeRole);
    app->binaryPath = QStringLiteral("/opt/x\x01y");
    app->workDir    = QStringLiteral("/tmp/\x7fdir");
    app->env.insert(QStringLiteral("WEIRD\x02NAME"), QStringLiteral("v\x03"));
    cat->appendRow(app);

    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/control.toml");
    QString error;
    QVERIFY2(ConfigFile::write(model, path, &error), qPrintable(error));

    const ConfigFile::ReadResult result = ConfigFile::read(path);
    QCOMPARE(result.error, ConfigFile::ReadError::None);
    QCOMPARE(result.categories.size(), 1);
    QCOMPARE(result.categories.first().items.size(), 1);

    const ConfigFile::App &readApp = result.categories.first().items.first();
    QCOMPARE(readApp.binary, app->binaryPath);
    QCOMPARE(readApp.workDir, app->workDir);
    QCOMPARE(readApp.env.value(QStringLiteral("WEIRD\x02NAME")),
             QStringLiteral("v\x03"));
}

void ConfigFileTest::rejectsMissingCategories()
{
    QTemporaryDir dir;
    QCOMPARE(errorFor(dir, "name = 'x'\n"),
             ConfigFile::ReadError::MissingCategories);
}

void ConfigFileTest::rejectsUnknownKeys()
{
    QTemporaryDir dir;
    QCOMPARE(errorFor(dir, "categories = []\nextra = 1\n"),
             ConfigFile::ReadError::UnsupportedTopLevelKey);

    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\nfoo = 1\n"),
             ConfigFile::ReadError::UnsupportedStructure);

    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\n"
                            "binary = 'b'\nfoo = 1\n"),
             ConfigFile::ReadError::UnsupportedStructure);
}

void ConfigFileTest::rejectsWrongTypes()
{
    QTemporaryDir dir;
    const auto want = ConfigFile::ReadError::UnsupportedStructure;

    QCOMPARE(errorFor(dir, "[[categories]]\nname = 1\n"), want);
    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\ncollapsed = 'yes'\n"), want);
    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\nbinary = 123\n"), want);
    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\nbinary = 'b'\n"
                            "singleInstance = 1\n"), want);
    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nseparator = 'true'\n"), want);
}

void ConfigFileTest::rejectsBadArguments()
{
    QTemporaryDir dir;
    const auto want = ConfigFile::ReadError::UnsupportedStructure;

    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\n"
                            "binary = 'b'\narguments = 5\n"), want);
    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\n"
                            "binary = 'b'\narguments = [1, 2]\n"), want);
}

void ConfigFileTest::rejectsBadEnvironment()
{
    QTemporaryDir dir;
    const auto want = ConfigFile::ReadError::UnsupportedStructure;

    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\n"
                            "binary = 'b'\nenv = 'not-a-table'\n"), want);
    QCOMPARE(errorFor(dir, "[[categories]]\nname = 'c'\n\n"
                            "[[categories.items]]\nname = 'a'\n"
                            "binary = 'b'\n[categories.items.env]\nA = 1\n"), want);
}

void ConfigFileTest::reportsParseErrors()
{
    QTemporaryDir dir;
    QCOMPARE(errorFor(dir, "[[categories\n"),
             ConfigFile::ReadError::ParseError);
}

QTEST_MAIN(ConfigFileTest)
#include "tst_configfile.moc"

#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QStandardItemModel;

namespace ConfigFile {

struct App
{
    bool separator = false;

    QString name;
    QString binary;
    QStringList arguments;
    QString workDir;
    QString iconPath;
    QString desktopFileId;
    QMap<QString, QString> env;

    bool isScript = false;
    bool launchInTerminal = false;
    bool launchWithDebugger = false;
    bool singleInstance = true;
};

struct Category
{
    QString name;
    QString iconPath;
    bool collapsed = false;
    QList<App> items;
};

enum class ReadError {
    None,
    ParseError,
    MissingCategories,
    UnsupportedTopLevelKey,
    UnsupportedStructure,
};

struct ReadResult
{
    ReadError error = ReadError::None;

    // ParseError details
    QString errorPath;
    int errorLine = 0;
    QString errorDescription;

    // UnsupportedTopLevelKey detail
    QString unsupportedKey;

    QList<Category> categories;
};

// Path of the single backup kept beside a config file.
QString backupPath(const QString &path);

// Parse a TOML config file. Returns ReadError::None on success.
ReadResult read(const QString &path);

// Serialize the launcher model to a TOML config file.
// Returns false and sets *error on failure.
bool write(const QStandardItemModel &model, const QString &path, QString *error);

} // namespace ConfigFile

#endif // CONFIGFILE_H

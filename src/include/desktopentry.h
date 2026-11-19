#ifndef DESKTOPENTRY_H
#define DESKTOPENTRY_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QIcon>

struct DesktopEntry
{
    QString id;                 // filename without .desktop
    QString desktopFilePath;    // absolute path to the .desktop file
    QString name;
    QString comment;
    QString exec;               // raw Exec= value
    QString tryExec;
    QString icon;               // raw Icon= value (name or path)
    QString path;               // working directory
    QStringList categories;
    bool terminal  = false;
    bool noDisplay = false;
    bool hidden    = false;

    // Derived from exec:
    QString     program;
    QStringList arguments;
};

class DesktopEntryScanner
{
public:
    // Parses every .desktop file under the standard XDG dirs.
    // Returns only Type=Application entries that are displayable.
    static QList<DesktopEntry> scan();

    // Resolve an Icon= value ("firefox", "/opt/x/y.png") to a real file path.
    // Returns empty if nothing found. Does NOT trigger QIcon::fromTheme, so
    // no theme cache is loaded.
    static QString resolveIconName(const QString &name);

    // Split an Exec= string into program + args, stripping %-codes
    // (%f %F %u %U %i %c %k) and honouring double quotes.
    static void splitExec(const QString &raw, QString &program, QStringList &args);
};

#endif // DESKTOPENTRY_H
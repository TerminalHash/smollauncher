#ifndef ITCHIMPORTER_H
#define ITCHIMPORTER_H

#include <QString>
#include <QStringList>
#include <QList>

struct ItchGame
{
    QString slug;          // folder name, also itch internal ID
    QString title;         // display name (derived from slug)
    QString installPath;   // absolute path to game folder
    QString executable;    // absolute path to runnable program
    QStringList arguments;     // space-joined args
    QString iconPath;      // resolved icon (may be empty)
    QString warning;       // non-empty if detection was uncertain
    bool    windowsOnly = false;  // Windows build; launched via Wine

    QString desktopFileId() const
    {
        return QStringLiteral("itch-") + slug;
    }
};

namespace ItchImporter {

QList<ItchGame> scanRoots(const QStringList &roots);
QList<ItchGame> scanFromDatabase();
QString databasePath();

} // namespace ItchImporter

#endif // ITCHIMPORTER_H
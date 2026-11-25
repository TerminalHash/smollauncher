#ifndef HEROICIMPORTER_H
#define HEROICIMPORTER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

#include "../../include/launchcommand.h"

struct HeroicGame
{
    QString runner;        // "legendary" or "gog"
    QString appName;       // internal ID (legendary app_name / gog appName)
    QString title;         // display name
    QString installPath;   // game install directory
    QString launchBinary;  // program to run
    QStringList launchArgs; // args for the program
    QString warning;       // non-empty if Heroic URI fallback is used
    QString iconPath;      // resolved icon (may be empty)
    bool    isNative = false;  // true for GOG Linux games
    QString executablePath;    // absolute path to the game .exe
    QMap<QString, QString> env; // WINEPREFIX, PROTONPATH, etc

    QString desktopFileId() const
    {
        return QStringLiteral("heroic-") + runner + QLatin1Char('-') + appName;
    }
};

namespace HeroicImporter {

bool isLegendaryAvailable();
bool isHeroicAvailable();

// Program used for heroic:// fallback entries on this platform.
LaunchCommand launchCommand();

QList<HeroicGame> scanEpic();
QList<HeroicGame> scanGog();

} // namespace HeroicImporter

#endif // HEROICIMPORTER_H
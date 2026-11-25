#ifndef STEAMIMPORTER_H
#define STEAMIMPORTER_H

#include <QString>
#include <QStringList>
#include <QList>

#include "../../include/launchcommand.h"

struct SteamGame
{
    QString appId;
    QString name;
    QString installDir;
    QString iconPath;    // resolved header image, may be empty
};

namespace SteamImporter {

// Returns true if Steam can be launched on this system.
bool isSteamAvailable();

// Program and arguments used for steam://rungameid/<appid> entries.
LaunchCommand launchCommand();

// Scans all libraries under the standard Steam roots.
// Sorted alphabetically by name.
QList<SteamGame> scan();

} // namespace SteamImporter

#endif // STEAMIMPORTER_H
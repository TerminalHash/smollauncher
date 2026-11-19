#ifndef STEAMIMPORTER_H
#define STEAMIMPORTER_H

#include <QString>
#include <QList>

struct SteamGame
{
    QString appId;
    QString name;
    QString installDir;
    QString iconPath;    // resolved header image, may be empty
};

namespace SteamImporter {

// Returns true if `steam` is resolvable from PATH.
bool isSteamAvailable();

// Scans all libraries under the standard Steam roots.
// Sorted alphabetically by name.
QList<SteamGame> scan();

} // namespace SteamImporter

#endif // STEAMIMPORTER_H
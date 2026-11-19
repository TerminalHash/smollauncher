#ifndef RETROARCHIMPORTER_H
#define RETROARCHIMPORTER_H

#include <QString>
#include <QStringList>
#include <QList>

struct RetroArchGame
{
    QString playlistName;   // "Nintendo - SNES", "Arcade", …
    QString playlistPath;   // absolute path to the .lpl
    QString romPath;
    QString romTitle;       // file basename without extension, prettified
    QString corePath;       // absolute path to the core .so, may be empty
    QString coreName;       // "Snes9x", "Mupen64Plus", …
    QString warning;        // non-empty when core is unresolved

    QString desktopFileId() const
    {
        // Path-based; unique per ROM, stable across sessions.
        return QStringLiteral("retroarch:") + romPath;
    }
};

namespace RetroArchImporter {

bool isAvailable();

QList<RetroArchGame> scan();        // scans the default playlist dir
QString playlistsDirectory();       // resolved path, for diagnostics

} // namespace RetroArchImporter

#endif // RETROARCHIMPORTER_H
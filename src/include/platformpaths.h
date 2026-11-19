#ifndef PLATFORMPATHS_H
#define PLATFORMPATHS_H

#include <QString>

// Per-user locations used by the Linux-first application importers.
// Windows/macOS apps generally keep their data under the roaming profile or
// ~/Library/Application Support, so GenericConfigLocation is not always right.
namespace PlatformPaths {

// Windows: %APPDATA%; macOS: ~/Library/Application Support; Linux: ~/.config.
QString configHome();

// Windows: %LOCALAPPDATA%; macOS: ~/Library/Application Support;
// Linux: ~/.local/share.
QString dataHome();

} // namespace PlatformPaths

#endif // PLATFORMPATHS_H

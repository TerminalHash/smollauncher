#ifndef CONFIGPATHS_H
#define CONFIGPATHS_H

#include <QString>
#include <QSettings>

// Base directory for the launcher's own config.
// Returns $SL_CONFIG_DIR if set, otherwise <user config>/SmolLauncher
// (~/.config on Linux, %APPDATA% on Windows, Application Support on macOS).
QString launcherConfigDir();

// Full path to the current list inside that directory.
QString launcherConfigFile();

// QSettings object for the launcher.
// - If SL_CONFIG_DIR is unset: default QSettings() (native format) — existing
//   behaviour, no migration required.
// - If SL_CONFIG_DIR is set: portable INI file at $SL_CONFIG_DIR/SmolLauncher.conf.
QSettings launcherSettings();

#endif // CONFIGPATHS_H
#include "include/configpaths.h"
#include "include/platformpaths.h"

namespace {

QString readEnvConfigDir()
{
    const QByteArray raw = qgetenv("SL_CONFIG_DIR");
    return QString::fromLocal8Bit(raw).trimmed();
}

} // namespace

QString launcherConfigDir()
{
    static const QString cached = []() -> QString {
        const QString env = readEnvConfigDir();
        if (!env.isEmpty())
            return env;

        // PlatformPaths already points at the per-user config root without
        // appending the organisation/application name; add one fixed folder.
        return PlatformPaths::configHome() + QStringLiteral("/SmolLauncher");
    }();
    return cached;
}

QString launcherConfigFile()
{
    return launcherConfigDir() + QStringLiteral("/Lists/mainlist.toml");
}

QSettings launcherSettings()
{
    // Portable mode: single INI file inside SL_CONFIG_DIR
    if (!readEnvConfigDir().isEmpty())
        return QSettings(launcherConfigDir() + QStringLiteral("/SmolLauncher.conf"),
                         QSettings::IniFormat);

    // Normal mode: platform default (native format) — identical to what the
    // code has been using so far.
    return QSettings();
}
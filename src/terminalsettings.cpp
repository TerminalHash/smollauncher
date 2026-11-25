#include "include/terminalsettings.h"
#include "include/configpaths.h"

#include <QSettings>

namespace TerminalSettings {

QStringList defaultTerminals()
{
    return {
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("konsole"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("xterm"),
    };
}

QStringList commandPrefix(const QString &program)
{
    if (program == QLatin1String("gnome-terminal"))
        return { QStringLiteral("--") };
    if (program == QLatin1String("xfce4-terminal"))
        return { QStringLiteral("-x") };
    // xterm, konsole and x-terminal-emulator all accept -e.
    return { QStringLiteral("-e") };
}

Config load()
{
    const QSettings settings = launcherSettings();
    Config config;
    config.program    = settings.value(QStringLiteral("terminalProgram")).toString();
    config.custom     = settings.value(QStringLiteral("terminalCustom"), false).toBool();
    config.customBin  = settings.value(QStringLiteral("terminalCustomBin")).toString();
    config.customArgs = settings.value(QStringLiteral("terminalCustomArgs")).toString();
    return config;
}

void save(const Config &config)
{
    QSettings settings = launcherSettings();
    settings.setValue(QStringLiteral("terminalProgram"), config.program);
    settings.setValue(QStringLiteral("terminalCustom"), config.custom);
    settings.setValue(QStringLiteral("terminalCustomBin"), config.customBin);
    settings.setValue(QStringLiteral("terminalCustomArgs"), config.customArgs);
}

} // namespace TerminalSettings

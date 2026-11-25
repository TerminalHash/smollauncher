#ifndef TERMINALSETTINGS_H
#define TERMINALSETTINGS_H

#include <QString>
#include <QStringList>

// Terminal-emulator preferences used by the Linux launch path and the
// Misc → Terminal emulator dialog.
namespace TerminalSettings {

struct Config
{
    QString program;       // selected default terminal (e.g. "konsole")
    bool    custom = false;
    QString customBin;     // used when custom is true
    QString customArgs;    // terminal's own arguments, e.g. "-e"
};

// Standard terminal emulators offered in the dialog.
QStringList defaultTerminals();

// Arguments a known terminal needs before the command it should run.
QStringList commandPrefix(const QString &program);

Config load();
void   save(const Config &config);

} // namespace TerminalSettings

#endif // TERMINALSETTINGS_H

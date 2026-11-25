#ifndef LAUNCHCOMMAND_H
#define LAUNCHCOMMAND_H

#include <QString>
#include <QStringList>

// Program plus arguments used to invoke another launcher/application.
// The URL or entry-specific arguments are appended after `prefix`.
struct LaunchCommand
{
    QString program;
    QStringList prefix;
};

#endif // LAUNCHCOMMAND_H

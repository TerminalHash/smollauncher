#ifndef COMMANDLINE_H
#define COMMANDLINE_H

#include <QString>
#include <QStringList>

namespace CommandLine {

QString shellQuote(const QString &s);
QString windowsQuote(const QString &s);
QStringList splitArgs(const QString &line);
QString joinArgs(const QStringList &args);
QString windowsJoinArgs(const QStringList &args);
QString resolveCommandPath(const QString &path);

} // namespace CommandLine

#endif // COMMANDLINE_H
